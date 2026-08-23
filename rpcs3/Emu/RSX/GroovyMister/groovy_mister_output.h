#pragma once

#include "util/types.hpp"
#include "Emu/RSX/GroovyMister/groovy_mister_modeline.h"
#include "Utilities/geometry.h" // areai

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace groovy_mister
{
	// One frame of pixel data queued for the sender thread.
	struct queued_frame
	{
		std::vector<u8> bgr; // packed pixels: width*height*m_bpp (BGR888 or RGB565 LE)
		u32 frame_idx = 0;
	};

	// Backend-neutral Groovy_MiSTer output core, shared by the Vulkan and
	// OpenGL renderers. Owns everything except the GPU capture itself:
	// connect/retry, the switchres modeline state machine, the SPSC frame
	// ring, the sender thread (sole owner of the Groovy UDP socket after
	// init), audio-tap arming, telemetry and the close protocol.
	//
	// A renderer backend derives from this and implements:
	//   - its own record_capture(...) entry point (signatures differ per
	//     backend, so it is not a virtual here) which must call
	//     consume_dump_hotkey(), drain its matured captures into
	//     push_to_sender(), and gate on ensure_mode();
	//   - on_mode_resources(dst_w, dst_h): (re)allocate capture resources
	//     for a new modeline active area (called from ensure_mode() on the
	//     RSX thread);
	//   - release_capture_resources(): drop all capture resources + pending
	//     captures (called from shutdown()).
	//
	// Threading: record_capture()/ensure_mode() are RSX-thread only. The
	// sender thread owns all Groovy API calls after init.
	class output_base
	{
	public:
		output_base() = default;
		virtual ~output_base() = default;

		output_base(const output_base&) = delete;
		output_base& operator=(const output_base&) = delete;

		void shutdown();

		bool is_active() const { return m_active; }

		// Called from the flip path every present. No-op unless a prior
		// init left the connect "pending" (MiSTer enabled but not yet
		// reachable — e.g. RPCS3 started before the Groovy core). Throttled;
		// retries the handshake and brings the stream up without an RPCS3
		// restart.
		void poll_connect_on_flip();

	protected:
		// Config read + switchres init + bounded connect retry. Backends call
		// this from their public init() after storing their device handle.
		bool init_common();

		void sender_loop();
		void push_to_sender(queued_frame&& f);

		// Holds an idle session open against the core's idle timeout. Called from
		// the sender loop's idle tick only; a no-op unless we have been silent for
		// KEEPALIVE_IDLE_MS. Sender thread only (it touches the gmw socket).
		void send_keepalive_if_idle();

		// Forwards every non-empty g_cfg.groovy_mister.sr_* value to
		// sr_set_option() under switchres's own option name. Must run after the
		// preset and before sr_init_disp() — see the ordering note at the call
		// site. Empty values are skipped so switchres's defaults stand.
		void apply_switchres_options();

		// One fully-clean connect attempt: gmw_close() (pristine object) ->
		// input subscribe (pre-CMD_INIT) -> gmw_init -> codec knobs. Returns
		// true on a live connection. Never reuses a torn-down gmw object, so
		// a prior failed gmw_init can't poison this attempt (input always
		// re-registers; CMD_INIT is never corrupt).
		bool try_connect();

		// Post-successful-connect bring-up shared by init_common() and the
		// flip-path retry: reset the close once-flag, start the sender
		// thread, mark active, arm the audio tap.
		void finalize_connect_bringup();

		// Reads the PS3 source res/refresh (avconf), and if it changed (or on
		// first use) runs switchres to compute a monitor-correct modeline,
		// asks the backend to (re)allocate capture resources to the modeline
		// active area (on_mode_resources), and queues a gmw_switchres for the
		// sender thread. Returns true if capture resources are ready for this
		// frame.
		bool ensure_mode();

		// Backend capture-resource hooks (see class comment).
		virtual void on_mode_resources(u16 dst_w, u16 dst_h) = 0;
		virtual void release_capture_resources() = 0;

		// Forwards the GUI's edge-triggered raw-frame-dump request
		// (Ctrl+Shift+D / settings button) to the sender thread. Call at the
		// top of record_capture() on the RSX thread.
		void consume_dump_hotkey();

		// Aspect-aware capture geometry, shared by both backends so the CRT
		// framing is identical regardless of renderer. Content aspect comes
		// from the PS3 (g_cfg.video.aspect_ratio — the runtime aspect the game
		// was told it has); we compute (a) the centred sub-rect of the source
		// that actually holds game content (the framebuffer may be pillar/
		// letterboxed) and (b) the matching sub-rect of dst (m_dst_w x m_dst_h)
		// so the downscale preserves aspect and adds black bars to the dst
		// when needed. Integer cross-multiply throughout — no float drift.
		struct capture_rects
		{
			areai src_content{}; // sub-rect of the (clamped) source holding game content
			areai dst_active{};  // sub-rect of dst the content maps onto
			bool needs_dst_clear = false; // dst has black bars -> clear before the scale
			bool fast_path = false; // src == dst size and aspect-native: direct copy, no scale
			int content_an = 4;  // content aspect numerator (16 or 4)
			int content_ad = 3;  // content aspect denominator (9 or 3)
		};
		capture_rects compute_capture_rects(u16 src_w, u16 src_h) const;

		// Packs a B8G8R8A8 readback row-major buffer into the wire format
		// (BGR888 or RGB565 LE when bpp == 2). `out` must hold pixels*bpp
		// bytes. Shared by both backends so the wire bytes are bit-identical
		// regardless of renderer.
		static void pack_bgra_to_wire(u8* out, const u8* in, size_t pixels, u32 bpp);

		// Idempotent: sends CMD_CLOSE a few times (UDP-loss insurance) then
		// gmw_close() exactly once, so the MiSTer returns to its
		// connection-search screen on stream stop instead of freezing on the
		// last frame. Must run on the sender thread (the Groovy-socket owner);
		// shutdown() calls it only as a fallback. The once-flag makes repeated
		// calls safe.
		void do_groovy_close();
		std::atomic<bool> m_closed{ false };

		// Connect parameters cached in init_common() (read once from g_cfg) so
		// the background flip-retry can reconnect without re-reading config.
		std::string m_host;
		u8  m_lz4 = 1;
		u16 m_mtu = 1500;
		u32 m_sound_rate = 0;
		u8  m_sound_chan = 0;
		u8  m_rgb_mode = 0;
		bool m_tap_audio = false;

		// Set when init_common()'s bounded retry exhausted without a
		// connection. poll_connect_on_flip() keeps retrying (throttled) until
		// it links.
		std::atomic<bool> m_connect_pending{ false };
		std::chrono::steady_clock::time_point m_next_connect_try{};

		// Raw-frame dump (codec corpus). The Ctrl+Shift+D hotkey (gs_frame) and
		// the settings "Dump Raw Frames Now" button both set the global atomic
		// g_user_asked_for_mister_frame_dump; record_capture() (RSX thread)
		// consumes that flag (consume_dump_hotkey) and sets m_dump_request;
		// sender_loop() (sole owner of the gmw object) reads m_dump_request,
		// resolves the configured dump dir + frame count, and calls
		// gmw_set_frame_dump() — keeping all gmw access on the sender thread to
		// match every other CmdBlit-adjacent member write. Count/dir live in
		// g_cfg.groovy_mister (default 120).
		std::atomic<bool> m_dump_request{ false };

		std::atomic<bool> m_active{ false };
		std::atomic<bool> m_quit{ false };

		// switchres state. The modeline is computed from (PS3 source res,
		// refresh, monitor preset), not hardcoded — exactly like RetroArch.
		bool m_sr_inited = false;
		// Last PS3 source signature, to detect cellVideoOut mode changes.
		u32 m_src_w = 0;
		u32 m_src_h = 0;
		double m_src_hz = 0.0;
		bool m_resources_ready = false;

		// Modeline pending a gmw_switchres. Set by the RSX thread (ensure_mode);
		// the sender thread issues gmw_switchres before its next blit so the
		// Groovy UDP socket is only ever driven from the sender thread.
		struct pending_modeline
		{
			double pclock_mhz = 0.0;
			u16 ha = 0, hb = 0, he = 0, ht = 0;
			u16 va = 0, vb = 0, ve = 0, vt = 0;
			u8 interlace = 0;
		};
		std::mutex m_sr_mutex;
		bool m_switchres_pending = false;
		pending_modeline m_pending_modeline{};
		// Last modeline actually handed to gmw_switchres. Retained (unlike
		// m_pending_modeline, which is cleared on consume) so the sender thread
		// can re-arm it after an auto-reconnect. The vendored client replays its
		// own stash internally, but that replay can itself fail its ACK — and the
		// C wrapper drops CmdSwitchres's return, so we cannot observe either
		// outcome. Re-arming unconditionally on every reconnect epoch is the
		// belt-and-braces that makes a lost modeline self-correcting instead of
		// permanent (a core with no modeline silently discards ALL video).
		pending_modeline m_last_modeline{};
		bool m_have_modeline = false;

		// Active modeline active-area = GPU downscale target = blit size.
		u16 m_dst_w = 0;
		u16 m_dst_h = 0;

		// Packed bytes per pixel sent to the MiSTer, from
		// g_cfg.groovy_mister.rgb_mode: 3 = RGB888 (default), 4 = RGBA888,
		// 2 = RGB565. Set in init_common() and must match the vendored lib's
		// m_RGBSize, which it derives from the same negotiated mode.
		u32 m_bpp = 3;

		// Cumulative + last-heartbeat sender-drop counters (bandwidth
		// telemetry). Drops happen in push_to_sender when the ring is full.
		u64 m_dropped_total = 0;
		u64 m_dropped_at_last_hb = 0;

		u32 m_frame_counter = 0;

		// FPGA-synced blit frame number. The protocol requires it to increase
		// monotonically from 1; the client's raster servo (DiffTimeRaster)
		// derives its sub-frame correction from the difference between this and
		// the FPGA's own free-running counter, so the two must stay within a
		// frame or two. The sender thread bumps it per blit and resyncs forward
		// if the FPGA has run ahead (mirrors RetroArch gfx_mister.c).
		// MUST be reset whenever the core starts a fresh session (reconnect):
		// CmdInit zeroes the core's counter, and a host that keeps counting from
		// the dead session diverges by thousands of frames, which the client
		// clamps away (RASTER_MAX_FRAME_SPREAD) — no hang, but no raster
		// alignment either, so pacing silently degrades to coarse frame-time.
		// NB: this is the number sent to gmw_blit, NOT the RSX capture index.
		u32 m_blit_frame = 0;

		// Monotonic ms of the last datagram WE put on the wire (blit, audio,
		// switchres, or a keepalive). The keepalive is gated on this rather than
		// on a free-running timer, so it is structurally impossible for it to
		// fire during normal play: at 60 fps the blit path refreshes it every
		// ~16 ms. Sender-thread-only after the thread starts; seeded in
		// finalize_connect_bringup() before it is spawned.
		u64 m_last_wire_ms = 0;

		// Keepalive tuning. The core drops a session that sends nothing on the
		// video socket for its idle timeout (OSD Server -> Idle timeout, default
		// 5 s) and frees the CRT — which would otherwise kill the session every
		// time the user pauses, opens a menu, or sits on a long load.
		// Worst-case silence is (idle threshold + poll period), so these must not
		// be equal: 2000 + 250 = 2.25 s, and 4.5 s even if one keepalive is lost
		// to UDP, both inside the 5 s default.
		static constexpr u64 KEEPALIVE_IDLE_MS = 2000;
		static constexpr u64 KEEPALIVE_POLL_MS = 250;

		// "The core moved past us" is a one- or two-frame condition. Beyond this
		// the lead is stale session state, not a resync — matches the vendored
		// client's own RASTER_MAX_FRAME_SPREAD, past which it refuses to derive a
		// raster correction at all.
		static constexpr u32 RASTER_RESYNC_WARN_FRAMES = 8;

		// Whether to hold an idle session open (g_cfg keepalive).
		bool m_keepalive = true;

		// Round-robin readback slot count, shared policy for both backends.
		// K must exceed the maximum number of flips whose GPU work can still
		// be in flight so the slot the CPU maps is never the slot the GPU is
		// currently writing.
		static constexpr u32 K_READBACK = 4;
		// FALLBACK maturity bound: a capture is force-drained after this many
		// flips even if its completion signal was never observed (the original,
		// proven-safe heuristic). The primary readiness check is per-backend
		// (VK: flip-CB fence poke; GL: glFenceSync), which typically matures a
		// capture at age 1 — 2 flips (33ms @60Hz) sooner than this bound, and
		// independent of flip cadence for sub-60fps titles.
		// Must be < K_READBACK and >= the deepest possible GPU pipelining.
		static constexpr u32 DEFER_FRAMES = 3;

		// SPSC: RSX thread pushes, sender thread pops.
		static constexpr size_t MAX_RING = 2;
		std::mutex m_queue_mutex;
		std::condition_variable m_queue_cv;
		std::vector<queued_frame> m_queue;

		std::thread m_sender_thread;
	};
}
