#pragma once

#include "util/types.hpp"
#include "Emu/RSX/GroovyMister/groovy_mister_output.h"

#include "glutils/buffer_object.h"
#include "glutils/fbo.h"
#include "glutils/sync.hpp"

#include <array>
#include <deque>
#include <memory>

namespace gl
{
	class texture;
	class command_context;
}

namespace gl_groovy_mister
{
	// Per-GLGSRender Groovy_MiSTer output manager. The transport/sender core
	// lives in groovy_mister::output_base; this subclass owns only the OpenGL
	// capture. Mirrors the Vulkan backend's design one-for-one:
	//
	//   record_capture() (RSX thread, GL context current) blits the visible
	//   sub-rect of the flip image into a scratch texture (aspect-aware
	//   downscale + black bars), then queues an async texture->PBO readback
	//   and drops a glFenceSync behind it. Nothing blocks: the readback is
	//   consumed on a later flip, once its fence reports the GPU finished
	//   (age >= 1), or after DEFER_FRAMES as the proven-safe fallback.
	//   Round-robin PBOs guarantee the slot the CPU maps is never the slot
	//   the GPU is still writing. Worst case if the GPU is unusually far
	//   behind is a slightly stale frame — never a crash, hang or RSX stall.
	//
	// Threading: record_capture() is RSX-thread only (all GL calls live
	// there). The sender thread owns all Groovy API calls after init.
	class output final : public groovy_mister::output_base
	{
	public:
		output() = default;
		~output();

		bool init();

		// Drains matured captures (maps + packs + queues to sender), then
		// records this frame's downscale/blit + PBO readback.
		//
		// valid_w/valid_h = the visible sub-rect of `src` (the flip path's
		// buffer_width/buffer_height AFTER the avconf clamp + get_present_source
		// — exactly the rect the host-window blit samples). `src` is the game's
		// full render-target surface and may be LARGER than the video mode
		// (e.g. Gran Turismo); capturing the whole surface streams stale/aux
		// surface content as a duplicated image. 0 = use the full image
		// dimensions.
		void record_capture(gl::command_context& cmd, gl::texture* src, u16 valid_w, u16 valid_h);

	private:
		void drain_matured_captures();

		// output_base capture-resource hooks.
		void on_mode_resources(u16 dst_w, u16 dst_h) override;
		void release_capture_resources() override;

		// Scratch texture used by the aspect-aware downscale path (BGRA8 at
		// the modeline active area, same layout as the VK scratch image).
		std::unique_ptr<gl::texture> m_scratch_tex;

		// FBO pair for the scaled blit: src texture attached to the read fbo,
		// scratch to the draw fbo (glBlitFramebuffer with linear filter).
		gl::fbo m_blit_src_fbo;
		gl::fbo m_blit_dst_fbo;

		// Round-robin pixel-pack buffers (K_READBACK/DEFER_FRAMES policy
		// documented in output_base) + one completion fence per slot. The
		// fence is (re)armed right after the texture->PBO copy each time the
		// slot is recorded.
		std::array<gl::buffer, K_READBACK> m_readback;
		std::array<gl::fence, K_READBACK> m_fences;

		struct pending_capture
		{
			u32 slot = 0;
			u32 frame_idx = 0;
			u32 recorded_at = 0; // m_frame_counter when recorded
		};
		std::deque<pending_capture> m_pending;
	};
}
