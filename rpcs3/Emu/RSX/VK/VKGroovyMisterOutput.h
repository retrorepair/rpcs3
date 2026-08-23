#pragma once

#include "util/types.hpp"
#include "Emu/RSX/GroovyMister/groovy_mister_output.h"

#include <array>
#include <deque>
#include <memory>

namespace vk
{
	class command_buffer;
	struct command_buffer_chunk; // VKGSRenderTypes.hpp — the flip CB type (fence poke + reset_id)
	class viewable_image;
	class render_device;
	struct buffer; // NB: buffer is a struct, not a class — the class-key is part of the MSVC mangled name
	class image;
}

namespace vk_groovy_mister
{
	// Per-VKGSRender Groovy_MiSTer output manager. The transport/sender core
	// lives in groovy_mister::output_base; this subclass owns only the Vulkan
	// capture.
	//
	// Design (correctness-first, no mid-flip GPU stall):
	//   record_capture() records the downscale + image->buffer copy into the
	//   *flip* command buffer (which RPCS3 submits as part of its normal
	//   present — we never flush or sync it ourselves). The readback is
	//   deferred: a capture is only mapped/packed several flips later, by
	//   which point RPCS3 has certainly submitted and completed that flip's
	//   command buffer. Round-robin readback buffers guarantee the slot the
	//   CPU reads is never one the GPU is still writing. Worst case if the
	//   GPU is unusually far behind is a slightly stale frame — never a
	//   crash, hang, or RSX stall.
	//
	// Threading: record_capture() is RSX-thread only. The sender thread owns
	// all Groovy API calls after init.
	class output final : public groovy_mister::output_base
	{
	public:
		output() = default;
		~output();

		bool init(vk::render_device& dev);

		// Drains matured captures (maps + packs + queues to sender), then
		// records this frame's downscale/copy into the flip command buffer.
		// No flush, no sync — rides along on RPCS3's normal present submit.
		//
		// valid_w/valid_h = the visible sub-rect of `src` (the flip path's
		// buffer_width/buffer_height AFTER the avconf clamp + get_present_source
		// — exactly the rect the host-window blit samples via srcOffsets).
		// `src` is the game's full render-target surface and may be LARGER than
		// the video mode (e.g. Gran Turismo); capturing the whole surface
		// streams stale/aux surface content as a duplicated image. 0 = use the
		// full image dimensions (legacy behavior).
		void record_capture(vk::command_buffer_chunk& cmd, vk::viewable_image* src, u16 valid_w, u16 valid_h);

	private:
		void drain_matured_captures();

		// output_base capture-resource hooks.
		void on_mode_resources(u16 dst_w, u16 dst_h) override;
		void release_capture_resources() override;

		vk::render_device* m_dev = nullptr;
		// Scratch image used by the aspect-aware downscale path. The
		// scaled branch of record_capture() puts it in TRANSFER_DST_OPTIMAL
		// at the top of every iteration; that single change_layout call
		// both escapes the freshly-created UNDEFINED layout (so
		// copy_scaled_image's internal push/pop has a valid prior layout
		// to restore to) and is the right layout for the optional black-bar
		// clear before the scale.
		std::unique_ptr<vk::image> m_scratch_image;

		// Round-robin host-visible readback buffers (K_READBACK/DEFER_FRAMES
		// policy documented in output_base). K exceeds the maximum number of
		// flip command buffers RPCS3 can have in flight so the slot the CPU
		// maps is never the slot the GPU is currently writing.
		std::array<std::unique_ptr<vk::buffer>, K_READBACK> m_readback;

		struct pending_capture
		{
			u32 slot = 0;
			u32 frame_idx = 0;
			u32 recorded_at = 0; // m_frame_counter when recorded
			// Fence identity of the flip CB this capture was recorded into.
			// cb->poke() (thread-safe, non-blocking) reports submission
			// completion; a reset_id mismatch means the chunk was recycled,
			// which itself guarantees our submission completed (chunk::reset()
			// waits for pending work first). Chunks live in VKGSRender's fixed
			// CB list for the renderer's lifetime, and m_pending is cleared on
			// resource teardown, so the pointer cannot dangle.
			vk::command_buffer_chunk* cb = nullptr;
			u64 cb_reset_id = 0;
		};
		std::deque<pending_capture> m_pending;
	};
}
