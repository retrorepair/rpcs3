#include "stdafx.h"
#include "VKGroovyMisterOutput.h"
#include "VKHelpers.h"
#include "VKGSRenderTypes.hpp"   // vk::command_buffer_chunk (flip CB fence poke/reset_id)
#include "vkutils/buffer_object.h"
#include "vkutils/image.h"
#include "vkutils/memory.h"
#include "vkutils/device.h"
#include "Emu/RSX/rsx_utils.h"

#include "util/logs.hpp"
#include "util/asm.hpp" // utils::align

#include <algorithm>

LOG_CHANNEL(groovy_log, "GROOVY");

namespace vk_groovy_mister
{
	output::~output()
	{
		shutdown();
	}

	bool output::init(vk::render_device& dev)
	{
		m_dev = &dev;
		return init_common();
	}

	void output::on_mode_resources(u16 dst_w, u16 dst_h)
	{
		m_scratch_image = std::make_unique<vk::image>(
			*m_dev,
			m_dev->get_memory_mapping().device_local,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			VK_IMAGE_TYPE_2D,
			VK_FORMAT_B8G8R8A8_UNORM,
			dst_w, dst_h, 1,
			1, 1,
			VK_SAMPLE_COUNT_1_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
			0,
			vk::VMM_ALLOCATION_POOL_UNDEFINED);

		const u64 readback_bytes = static_cast<u64>(dst_w) * dst_h * 4ull;
		for (u32 i = 0; i < K_READBACK; ++i)
		{
			m_readback[i] = std::make_unique<vk::buffer>(
				*m_dev,
				utils::align(readback_bytes, 0x1000),
				m_dev->get_memory_mapping().host_visible_coherent,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				VK_BUFFER_USAGE_TRANSFER_DST_BIT,
				0,
				vk::VMM_ALLOCATION_POOL_UNDEFINED);
		}
	}

	void output::release_capture_resources()
	{
		m_pending.clear();

		// Resources released last so any in-flight GPU ref is gone.
		for (auto& b : m_readback)
			b.reset();
		m_scratch_image.reset();
		m_dev = nullptr;
	}

	void output::drain_matured_captures()
	{
		// A capture is safe to read once the flip command buffer it was
		// recorded into has completed on the GPU. Primary signal: the CB
		// chunk's completion fence (poke() — non-blocking, thread-safe), valid
		// from age >= 1 (its submit happens at the end of the flip that
		// recorded it; drain runs at the start of the next flip). A reset_id
		// mismatch means the chunk was already recycled, which guarantees our
		// submission completed (chunk::reset() waits for pending work first).
		// Fallback: after DEFER_FRAMES flips, drain unconditionally — the
		// original proven-safe bound, so worst case equals the old behavior.
		// We never block: if it's not ready yet, leave it for the next flip.
		while (!m_pending.empty())
		{
			const pending_capture& pc = m_pending.front();
			const u32 age = m_frame_counter - pc.recorded_at;
			if (age < 1)
				break; // recorded this flip; its CB is not even submitted yet

			if (age < DEFER_FRAMES)
			{
				const bool cb_done = pc.cb && (pc.cb->reset_id != pc.cb_reset_id || pc.cb->poke());
				if (!cb_done)
					break; // fence not signalled yet; newest entries are behind this one
			}

			const bool trace = (pc.frame_idx <= 3);
			const u64 size = static_cast<u64>(m_dst_w) * m_dst_h * 4ull;

			vk::buffer* buf = m_readback[pc.slot].get();
			const u8* in = static_cast<const u8*>(buf->map(0, size));

			groovy_mister::queued_frame f;
			f.frame_idx = pc.frame_idx;
			const size_t pixels = static_cast<size_t>(m_dst_w) * m_dst_h;
			f.bgr.resize(pixels * m_bpp); // == vendored lib's m_RGBSize

			// Capture is VK_FORMAT_B8G8R8A8_UNORM; shared packer emits the
			// wire format (BGR888 / RGB565 LE) bit-identically to the GL path.
			pack_bgra_to_wire(f.bgr.data(), in, pixels, m_bpp);

			buf->unmap();

			if (trace)
				groovy_log.notice("consume #%u: slot=%u age=%u (%s) packed %zu BGR bytes, queueing to sender",
					f.frame_idx, pc.slot, age, (age < DEFER_FRAMES) ? "fence" : "fallback", f.bgr.size());

			m_pending.pop_front();
			push_to_sender(std::move(f));
		}
	}

	void output::record_capture(vk::command_buffer_chunk& cmd, vk::viewable_image* src, u16 valid_w, u16 valid_h)
	{
		if (!m_active || !src)
			return;

		consume_dump_hotkey();

		// First: ship any capture whose flip CB has certainly completed.
		drain_matured_captures();

		// Compute / refresh the switchres modeline from the live PS3 video
		// mode and (re)allocate resources if it changed. Skip the frame until
		// a valid modeline + resources exist.
		if (!ensure_mode())
			return;

		// Source geometry = the VISIBLE sub-rect of the surface, not the whole
		// image. `src` is the game's render-target surface and can be larger
		// than the video mode (Gran Turismo flips such surfaces); the host
		// window blits only the top-left valid_w x valid_h sub-rect, and we
		// must sample exactly the same rect or the surface's stale/aux region
		// is streamed as a duplicated image. valid_* == 0 falls back to the
		// full image (legacy behavior, correct for well-behaved titles).
		u16 src_w = static_cast<u16>(src->width());
		u16 src_h = static_cast<u16>(src->height());
		if (valid_w && valid_h)
		{
			src_w = std::min<u16>(src_w, valid_w);
			src_h = std::min<u16>(src_h, valid_h);
		}
		if (src_w == 0 || src_h == 0)
			return;

		// Aspect-aware geometry (shared with the GL backend so the CRT framing
		// is renderer-independent): content rect of src, active rect of dst,
		// black-bar clear flag and the direct-copy fast path.
		const capture_rects rc = compute_capture_rects(src_w, src_h);
		const areai& src_content = rc.src_content;
		const areai& dst_active = rc.dst_active;
		const bool needs_dst_clear = rc.needs_dst_clear;
		const bool fast_path = rc.fast_path;

		const bool trace = (m_frame_counter < 3);
		if (trace)
		{
			groovy_log.notice("record_capture #%u: surface=%ux%u valid=%ux%u fmt=0x%x dst=%ux%u path=%s ps3_aspect=%d:%d src_content=(%d,%d-%d,%d) dst_active=(%d,%d-%d,%d) clear=%s",
				m_frame_counter + 1,
				static_cast<u32>(src->width()), static_cast<u32>(src->height()),
				src_w, src_h, static_cast<u32>(src->format()),
				m_dst_w, m_dst_h, fast_path ? "fast" : "scaled",
				rc.content_an, rc.content_ad,
				src_content.x1, src_content.y1, src_content.x2, src_content.y2,
				dst_active.x1, dst_active.y1, dst_active.x2, dst_active.y2,
				needs_dst_clear ? "yes" : "no");
		}

		const u32 slot = m_frame_counter % K_READBACK;
		vk::buffer* readback = m_readback[slot].get();

		VkBufferImageCopy copy_info{};
		copy_info.bufferOffset = 0;
		copy_info.bufferRowLength = 0;
		copy_info.bufferImageHeight = 0;
		copy_info.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copy_info.imageSubresource.baseArrayLayer = 0;
		copy_info.imageSubresource.layerCount = 1;
		copy_info.imageSubresource.mipLevel = 0;
		copy_info.imageOffset = { 0, 0, 0 };
		copy_info.imageExtent = { m_dst_w, m_dst_h, 1 };

		if (fast_path)
		{
			// Identical shape to the proven screenshot/recording readback path
			// in VKPresent.cpp: single push -> copy_image_to_buffer -> pop.
			// copy_image_to_buffer requires the source in TRANSFER_SRC_OPTIMAL.
			src->push_layout(cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
			vk::copy_image_to_buffer(cmd, src, readback, copy_info);
			src->pop_layout(cmd);
		}
		else
		{
			// Put the scratch image in TRANSFER_DST_OPTIMAL first. This both
			// escapes the freshly-created scratch's initial UNDEFINED layout
			// (so copy_scaled_image's internal push/pop has a valid prior
			// layout to restore to) AND is the right layout for the optional
			// black-bar clear below. change_layout is a no-op when current
			// already equals target, so the per-frame cost is one barrier
			// at most.
			m_scratch_image->change_layout(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

			if (needs_dst_clear)
			{
				// Clear the dst to black so any letter/pillarbox region the
				// active sub-rect does not cover renders as solid black on
				// the MiSTer.
				VkClearColorValue black{};
				VkImageSubresourceRange full_range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
				vkCmdClearColorImage(cmd, m_scratch_image->value,
					VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &full_range);
			}

			// IMPORTANT: vk::copy_scaled_image fully owns src + dst layout
			// transitions internally (push at entry, pop at exit; see
			// VKTexture.cpp). We must NOT push/pop or change_layout these
			// images ourselves around it — double layout management corrupts
			// the layout/barrier tracking and produces an invalid GPU stream.
			vk::copy_scaled_image(cmd, src, m_scratch_image.get(), src_content, dst_active, 1,
				/*compatible_formats=*/false, VK_FILTER_LINEAR);

			// copy_scaled_image popped the scratch image back to its prior
			// layout (TRANSFER_DST_OPTIMAL). Stage exactly like the
			// screenshot readback: single push -> copy_image_to_buffer -> pop.
			m_scratch_image->push_layout(cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
			vk::copy_image_to_buffer(cmd, m_scratch_image.get(), readback, copy_info);
			m_scratch_image->pop_layout(cmd);
		}

		// Recorded into the flip CB only — RPCS3 submits it during its normal
		// present. We do NOT flush or sync here. The readback is consumed by
		// drain_matured_captures once that CB's fence reports completion
		// (typically the very next flip), or after DEFER_FRAMES as a fallback.
		const u32 frame_idx = ++m_frame_counter;
		m_pending.push_back({ slot, frame_idx, frame_idx /* recorded_at */, &cmd, cmd.reset_id });

		if (trace)
			groovy_log.notice("record_capture #%u: GPU commands recorded into flip CB (slot=%u)", frame_idx, slot);
	}
}
