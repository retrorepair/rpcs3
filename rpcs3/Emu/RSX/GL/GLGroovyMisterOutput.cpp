#include "stdafx.h"
#include "GLGroovyMisterOutput.h"
#include "GLHelpers.h"

#include "util/logs.hpp"
#include "util/asm.hpp" // utils::align

#include <algorithm>

LOG_CHANNEL(groovy_log, "GROOVY");

namespace gl_groovy_mister
{
	output::~output()
	{
		shutdown();
	}

	bool output::init()
	{
		return init_common();
	}

	void output::on_mode_resources(u16 dst_w, u16 dst_h)
	{
		// Scratch for the aspect-aware downscale. BGRA8 so the PBO readback
		// bytes are B,G,R,A — the same memory layout as the VK backend's
		// B8G8R8A8 readback, feeding the shared wire packer.
		m_scratch_tex = std::make_unique<gl::texture>(
			GL_TEXTURE_2D, dst_w, dst_h, 1, 1, 1, GL_BGRA8, RSX_FORMAT_CLASS_COLOR);

		if (!m_blit_src_fbo.created())
			m_blit_src_fbo.create();
		if (!m_blit_dst_fbo.created())
			m_blit_dst_fbo.create();

		const u64 readback_bytes = static_cast<u64>(dst_w) * dst_h * 4ull;
		for (u32 i = 0; i < K_READBACK; ++i)
		{
			if (m_readback[i].created())
				m_readback[i].remove();
			m_readback[i].create(gl::buffer::target::pixel_pack,
				utils::align(readback_bytes, 0x1000), nullptr,
				gl::buffer::memory_type::host_visible, gl::buffer::usage::host_read);

			// Stale fences reference captures into the OLD buffers; drop them.
			m_fences[i].destroy();
			m_fences[i] = gl::fence{};
		}
	}

	void output::release_capture_resources()
	{
		m_pending.clear();

		// GL objects must be released with the context current — shutdown()
		// runs on the RSX thread (GLGSRender::on_exit), same thread that
		// created them. Every release below is guarded so the destructor's
		// fallback shutdown() (which may run without a current context) is a
		// strict no-op after on_exit already released everything.
		for (auto& f : m_fences)
		{
			if (!f.is_empty())
			{
				f.destroy();
				f = gl::fence{};
			}
		}
		for (auto& b : m_readback)
		{
			if (b.created())
				b.remove();
		}
		m_blit_src_fbo.remove();
		m_blit_dst_fbo.remove();
		m_scratch_tex.reset();
	}

	void output::drain_matured_captures()
	{
		// A capture is safe to read once the GPU passed the glFenceSync armed
		// right behind its texture->PBO copy. Primary signal: the slot fence
		// (check_signaled() — non-blocking), valid from age >= 1. Fallback:
		// after DEFER_FRAMES flips, drain unconditionally — the proven-safe
		// bound, so worst case equals the VK backend's fallback behavior.
		// We never block: if it's not ready yet, leave it for the next flip.
		while (!m_pending.empty())
		{
			const pending_capture& pc = m_pending.front();
			const u32 age = m_frame_counter - pc.recorded_at;
			if (age < 1)
				break; // recorded this flip; the GPU may not even have started

			if (age < DEFER_FRAMES)
			{
				gl::fence& fence = m_fences[pc.slot];
				const bool gpu_done = !fence.is_empty() && fence.check_signaled();
				if (!gpu_done)
					break; // fence not signalled yet; newest entries are behind this one
			}

			const bool trace = (pc.frame_idx <= 3);
			const u64 size = static_cast<u64>(m_dst_w) * m_dst_h * 4ull;

			gl::buffer& buf = m_readback[pc.slot];
			const u8* in = buf.map(0, size, gl::buffer::access::read);

			groovy_mister::queued_frame f;
			f.frame_idx = pc.frame_idx;
			const size_t pixels = static_cast<size_t>(m_dst_w) * m_dst_h;
			f.bgr.resize(pixels * m_bpp); // == vendored lib's m_RGBSize

			// PBO holds BGRA bytes (GL_BGRA + GL_UNSIGNED_BYTE); shared packer
			// emits the wire format (BGR888 / RGB565 LE) bit-identically to
			// the VK path.
			pack_bgra_to_wire(f.bgr.data(), in, pixels, m_bpp);

			buf.unmap();

			if (trace)
				groovy_log.notice("consume #%u: slot=%u age=%u (%s) packed %zu BGR bytes, queueing to sender",
					f.frame_idx, pc.slot, age, (age < DEFER_FRAMES) ? "fence" : "fallback", f.bgr.size());

			m_pending.pop_front();
			push_to_sender(std::move(f));
		}
	}

	void output::record_capture(gl::command_context& cmd, gl::texture* src, u16 valid_w, u16 valid_h)
	{
		if (!m_active || !src)
			return;

		consume_dump_hotkey();

		// First: ship any capture whose GPU work has certainly completed.
		drain_matured_captures();

		// Compute / refresh the switchres modeline from the live PS3 video
		// mode and (re)allocate resources if it changed. Skip the frame until
		// a valid modeline + resources exist.
		if (!ensure_mode())
			return;

		// Source geometry = the VISIBLE sub-rect of the surface, not the whole
		// image (see the VK backend / header comment: Gran Turismo flips
		// surfaces larger than the video mode). valid_* == 0 falls back to the
		// full image.
		u16 src_w = static_cast<u16>(src->width());
		u16 src_h = static_cast<u16>(src->height());
		if (valid_w && valid_h)
		{
			src_w = std::min<u16>(src_w, valid_w);
			src_h = std::min<u16>(src_h, valid_h);
		}
		if (src_w == 0 || src_h == 0)
			return;

		// Aspect-aware geometry (shared with the VK backend so the CRT framing
		// is renderer-independent): content rect of src, active rect of dst,
		// black-bar clear flag and the direct-copy fast path.
		const capture_rects rc = compute_capture_rects(src_w, src_h);

		const bool trace = (m_frame_counter < 3);
		if (trace)
		{
			groovy_log.notice("record_capture #%u: surface=%ux%u valid=%ux%u dst=%ux%u path=%s ps3_aspect=%d:%d src_content=(%d,%d-%d,%d) dst_active=(%d,%d-%d,%d) clear=%s",
				m_frame_counter + 1,
				static_cast<u32>(src->width()), static_cast<u32>(src->height()),
				src_w, src_h,
				m_dst_w, m_dst_h, rc.fast_path ? "fast" : "scaled",
				rc.content_an, rc.content_ad,
				rc.src_content.x1, rc.src_content.y1, rc.src_content.x2, rc.src_content.y2,
				rc.dst_active.x1, rc.dst_active.y1, rc.dst_active.x2, rc.dst_active.y2,
				rc.needs_dst_clear ? "yes" : "no");
		}

		const u32 slot = m_frame_counter % K_READBACK;

		// Readback source: the src texture directly on the fast path (its
		// top-left dst-sized rect IS the frame), else the scratch after the
		// aspect-aware blit.
		gl::texture* readback_src = src;

		if (!rc.fast_path)
		{
			if (rc.needs_dst_clear)
			{
				// Clear the scratch to black so any letter/pillarbox region
				// the active sub-rect does not cover renders as solid black
				// on the MiSTer. fbo::clear() saves/restores the FB binding.
				cmd->clear_color(0, 0, 0, 255);
				m_blit_dst_fbo.color = m_scratch_tex->id();
				m_blit_dst_fbo.clear(gl::buffers::color);
			}

			// Aspect-preserving downscale of the content rect into the active
			// rect. fbo::blit binds read/draw itself; downstream present code
			// (screen blit, overlays) rebinds everything it needs, same as the
			// screenshot path that also repoints FBOs mid-flip.
			m_blit_src_fbo.color = src->id();
			m_blit_src_fbo.read_buffer(m_blit_src_fbo.color);
			m_blit_dst_fbo.color = m_scratch_tex->id();
			m_blit_dst_fbo.draw_buffer(m_blit_dst_fbo.color);
			m_blit_src_fbo.blit(m_blit_dst_fbo, rc.src_content, rc.dst_active,
				gl::buffers::color, gl::filter::linear);

			readback_src = m_scratch_tex.get();
		}

		// Async texture->PBO readback of the dst-sized top-left rect, then a
		// fence right behind it. GL rows come back top-first (same order the
		// screenshot path ships to take_screenshot), matching the VK readback
		// — no vertical flip. copy_to leaves the PBO bound to PIXEL_PACK;
		// save_binding_state restores the previous binding so the screenshot/
		// recording path (client-memory readback) is not redirected into our
		// PBO.
		{
			static const gl::pixel_pack_settings pack_settings{};
			const coord3u region = { {}, { m_dst_w, m_dst_h, 1 } };

			gl::buffer::save_binding_state pack_save(gl::buffer::target::pixel_pack);
			readback_src->copy_to(m_readback[slot], 0,
				gl::texture::format::bgra, gl::texture::type::ubyte, 0, region, pack_settings);
		}

		// (Re)arm the slot fence. NB: gl::fence caches a one-way 'signaled'
		// latch that create()/destroy() do NOT reset — reassign a fresh object
		// after destroying the old sync or a reused slot would report
		// completion instantly.
		m_fences[slot].destroy();
		m_fences[slot] = gl::fence{};
		m_fences[slot].create();

		// No flush, no sync — the pending copy rides the RSX thread's normal
		// command stream. The readback is consumed by drain_matured_captures
		// once the fence reports completion (typically the very next flip),
		// or after DEFER_FRAMES as a fallback.
		const u32 frame_idx = ++m_frame_counter;
		m_pending.push_back({ slot, frame_idx, frame_idx /* recorded_at */ });

		if (trace)
			groovy_log.notice("record_capture #%u: GL blit + PBO readback queued (slot=%u)", frame_idx, slot);
	}
}
