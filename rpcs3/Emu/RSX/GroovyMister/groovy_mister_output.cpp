#include "stdafx.h"
#include "groovy_mister_output.h"
#include "Emu/system_config.h"
#include "Emu/IdManager.h"
#include "Emu/RSX/rsx_utils.h"
#include "Emu/Cell/Modules/cellVideoOut.h"

#include "groovymister_wrapper.h"
#include "switchres_wrapper.h"

#include "Emu/Audio/Groovy/GroovyMisterAudioTap.h"

#include "Utilities/Thread.h"
#include "Utilities/File.h"          // fs::get_config_dir / fs::create_path
#include "util/logs.hpp"
#include "util/asm.hpp"
#include "util/atomic.hpp"

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <thread>

// Set by the in-game shortcut handler (gs_frame::handle_shortcut →
// gw_mister_dump_frames). Consumed on the RSX thread inside record_capture()
// which forwards the request to the sender thread via m_dump_request.
extern atomic_t<bool> g_user_asked_for_mister_frame_dump;

LOG_CHANNEL(groovy_log, "GROOVY");

namespace groovy_mister
{
	// Forwards the vendored Groovy library's internal trace into RPCS3's log.
	// Installed before gmw_init so every CmdInit step/failure is visible.
	// May be called from socket/sender threads — groovy_log is thread-safe.
	static void groovy_lib_log_sink(const char* msg)
	{
		if (!msg)
			return;

		std::string s = msg;
		while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
			s.pop_back();

		if (!s.empty())
			groovy_log.notice("[gmw] %s", s);
	}

	// switchres logs via printf-style callbacks (like RetroArch's RARCH_LOG).
	// Forward into the GROOVY channel so modeline calculation is visible.
	static void sr_log_sink(const char* fmt, ...)
	{
		if (!fmt)
			return;

		char buf[1024];
		va_list ap;
		va_start(ap, fmt);
		std::vsnprintf(buf, sizeof(buf), fmt, ap);
		va_end(ap);

		std::string s = buf;
		while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
			s.pop_back();

		if (!s.empty())
			groovy_log.notice("[sr] %s", s);
	}

	// Monotonic milliseconds. Used only for the keepalive's "time since our last
	// outbound datagram" gate, so a steady (never wall) clock is required.
	static u64 monotonic_ms()
	{
		return static_cast<u64>(std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count());
	}

	// The MTU enum formats as the literal number for config readability, so map
	// it back explicitly rather than relying on the enumerator's ordinal.
	static u16 mtu_to_bytes(groovy_mister_mtu mtu)
	{
		switch (mtu)
		{
		case groovy_mister_mtu::_1500: return 1500;
		case groovy_mister_mtu::_3800: return 3800;
		}
		return 1500;
	}

	// CMD_INIT byte[4] + the bytes/px we pack into the blit buffer. The two must
	// agree: the vendored client derives its own frame size from the mode.
	static void rgb_mode_to_wire(groovy_mister_rgb_mode mode, u8& rgb_mode_out, u32& bpp_out)
	{
		switch (mode)
		{
		case groovy_mister_rgb_mode::rgb888:  rgb_mode_out = 0; bpp_out = 3; return;
		case groovy_mister_rgb_mode::rgba888: rgb_mode_out = 1; bpp_out = 4; return;
		case groovy_mister_rgb_mode::rgb565:  rgb_mode_out = 2; bpp_out = 2; return;
		}
		rgb_mode_out = 0;
		bpp_out = 3;
	}

	static u8 lz4_to_wrapper(groovy_mister_lz4 mode)
	{
		switch (mode)
		{
		case groovy_mister_lz4::off:          return 0;
		case groovy_mister_lz4::lz4:          return 1;
		case groovy_mister_lz4::lz4_hc:       return 3;
		case groovy_mister_lz4::nlc_tiled:    return 7;   // Lz4FramesCode::NLC (pack is a separate knob)
		}
		return 7;   // default to NLC
	}

	bool output_base::try_connect()
	{
		// Guarantee a pristine client object for THIS attempt. A prior
		// FAILED gmw_init (e.g. a freshly-started MiSTer core not ACKing
		// within CmdInit's single 60 ms window) runs CmdClose() — which
		// closes both UDP sockets + WSACleanup + deregisters the RIO
		// buffers — but leaves the global `gmw` object and
		// `gmw_inputsBinded` set. Reusing that torn-down object is
		// why gmw_bindInputs gets skipped (input socket
		// stays closed → controllers never register) and CmdInit
		// re-registers RIO on dead state (corrupt CMD_INIT → the core
		// mis-parses it and the whole stream desyncs/collapses). gmw_close()
		// deletes the object and NULLs both globals so the steps below
		// rebuild everything from scratch. On the very first attempt of the
		// process `gmw` is already NULL → harmless. The idempotent CmdClose
		// vendored patch makes the (possibly second) teardown safe.
		gmw_close();

		// Send the input "subscribe" BEFORE CMD_INIT: the core latches the
		// subscriber's address in its CMD_INIT handler (setInit), so it must
		// already be buffered there — the decoupled pad thread (binds
		// post-CMD_INIT-ACK) is structurally too late for that first read.
		// Convergence update: the /78 core drains ALL queued subscribes and
		// keeps the NEWEST address, and its per-poll sweep refreshes the
		// address mid-session, so the old fixed-local-port patch is gone from
		// the vendored lib (ephemeral port is fine). The resends are cheap
		// 1-byte UDP-loss insurance and keep a pre-/78 core working too.
		gmw_bindInputs(m_host.c_str());
		for (int i = 0; i < 3; ++i)
			gmw_resubscribe_inputs();
		groovy_log.notice("MiSTer input subscribe sent (pre CMD_INIT)");

		// NLC codec tuning rides the CMD_INIT byte[1] packing, so it MUST be
		// applied here before gmw_init (the wrapper passthroughs are no-ops
		// after CMD_INIT). The vendored client only consults these when m_lz4
		// resolves to NLC (= 7); LZ4-family runs ignore them.
		//   byte[1] = [1:0] codec | [3:2] near | [4] colour (YCoCg, fixed) |
		//             [6:5] dispMode (2) | [7] pack (1 = Rice)
		const u8 nlc_near = static_cast<u8>(g_cfg.groovy_mister.nlc_near_level.get());
		const bool nlc_rice = (g_cfg.groovy_mister.nlc_pack.get() == groovy_mister_nlc_pack::rice);
		gmw_set_near_level(nlc_near);
		gmw_set_nlc_pack(nlc_rice ? 2 : 1);   // 2 = NLC_PACK_RICE, 1 = NLC_PACK_TILED
		if (m_lz4 == 7) // Lz4FramesCode::NLC
		{
			groovy_log.notice("MiSTer NLC codec selected: pack=%s NEAR=%u (colour=YCoCg-R fixed)%s",
				nlc_rice ? "Rice" : "Tiled", nlc_near,
				nlc_rice
					? ". Rice requires a core with the Rice decoder (rbf_rice_r3 kit or newer); an older core ignores the negotiation bit and will misparse the stream."
					: ".");
		}

		// Inputs protocol v2 + rumble opt-in (CMD_INIT byte[5], set before
		// gmw_init like the NLC knobs). v2 = 32-bit button masks + analog
		// L2/R2 triggers in the joystick packets; RUMBLE lets the pad handler
		// forward game vibration to the physical pad. Safe unconditionally:
		// the lib probes CMD_GET_VERSION first and a pre-v2 core (GROOVY_VERSION
		// < 2, which would silently DISCARD a 6-byte init) gets the legacy
		// 5-byte init with caps dropped — gmw_get_input_caps() reports what was
		// actually negotiated, and gmw_send_rumble self-gates on it. Rumble
		// additionally needs the game + pad config + MiSTer OSD Rumble=On.
		gmw_set_input_caps(GMW_CAP_INPUTS_V2 | GMW_CAP_RUMBLE);

		// Opt in to the vendored ACK watchdog (now canonical but DEFAULT OFF):
		// >=10 blits with no frameEcho advance -> transparent video-side
		// reconnect + stashed-modeline replay; inputs socket untouched. This
		// preserves the resilience behavior every prior session was validated
		// with; sender_loop still watches gmw_reconnect_epoch() for telemetry.
		gmw_set_auto_reconnect(g_cfg.groovy_mister.auto_reconnect.get() ? 1 : 0);

		const int rc = gmw_init(m_host.c_str(), m_lz4, m_sound_rate, m_sound_chan, m_rgb_mode, m_mtu);
		if (rc < 0)
			return false;
		return true;
	}

	void output_base::finalize_connect_bringup()
	{
		// Allow a later close to actually send CMD_CLOSE (the once-flag is
		// per-instance and may have latched on a prior no-op shutdown).
		m_closed = false;

		// gmw_switchres + GPU resources are allocated lazily in ensure_mode()
		// once the PS3 video mode is known (first frame / on mode change).
		m_quit = false;

		// Seed the keepalive clock BEFORE the sender thread exists (thread
		// creation gives us the happens-before, so no atomic is needed). CMD_INIT
		// has just gone out, so the core's idle timer is freshly armed; without
		// this seed a zero-initialised value would make the very first idle poll
		// think we had been silent since the epoch and fire a needless keepalive.
		m_last_wire_ms = monotonic_ms();

		m_sender_thread = std::thread([this] { sender_loop(); });
		m_active = true;

		// Arm the cell_audio-thread tap; the sender thread (sole owner of
		// the Groovy socket) drains it. Only when negotiated at CMD_INIT.
		groovy_mister_audio_tap::set_active(m_tap_audio);

		groovy_log.success("MiSTer connection up (host=%s). Awaiting first frame to compute the modeline.", m_host);
	}

	void output_base::apply_switchres_options()
	{
		// One row per exposed switchres option. The key is switchres's own
		// SR_OPT_* constant (3rdparty/switchres/switchres_defines.h) so the
		// names cannot drift from what set_option() actually accepts, and the
		// value is passed through verbatim — RPCS3 parses none of it.
		//
		// Empty means "never call sr_set_option for this", which is what keeps a
		// default config byte-identical to a build without these knobs.
		//
		// Only calculation-affecting options are listed; see the note in
		// system_config.h for why the host-display ones are omitted.
		const auto& gm = g_cfg.groovy_mister;
		const std::pair<const char*, const cfg::string*> options[] =
		{
			// Monitor
			{ SR_OPT_CRT_RANGE0,             &gm.sr_crt_range0 },
			{ SR_OPT_LCD_RANGE,              &gm.sr_lcd_range },
			{ SR_OPT_MODELINE,               &gm.sr_modeline },
			{ SR_OPT_USER_MODE,              &gm.sr_user_mode },
			// Modeline generation
			{ SR_OPT_INTERLACE,              &gm.sr_interlace },
			{ SR_OPT_DOUBLESCAN,             &gm.sr_doublescan },
			{ SR_OPT_INTERLACE_FORCE_EVEN,   &gm.sr_interlace_force_even },
			{ SR_OPT_DOTCLOCK_MIN,           &gm.sr_dotclock_min },
			{ SR_OPT_SYNC_REFRESH_TOLERANCE, &gm.sr_sync_refresh_tolerance },
			{ SR_OPT_SUPER_WIDTH,            &gm.sr_super_width },
			{ SR_OPT_ASPECT,                 &gm.sr_aspect },
			{ SR_OPT_H_SIZE,                 &gm.sr_h_size },
			{ SR_OPT_H_SHIFT,                &gm.sr_h_shift },
			{ SR_OPT_V_SHIFT,                &gm.sr_v_shift },
			{ SR_OPT_V_SHIFT_CORRECT,        &gm.sr_v_shift_correct },
			{ SR_OPT_PIXEL_PRECISION,        &gm.sr_pixel_precision },
			{ SR_OPT_SCALE_PROPORTIONAL,     &gm.sr_scale_proportional },
		};

		u32 applied = 0;
		for (const auto& [key, entry] : options)
		{
			const std::string value = entry->to_string();
			if (value.empty())
				continue;

			sr_set_option(key, value.c_str());
			groovy_log.notice("switchres option: %s = %s", key, value);
			++applied;
		}

		if (applied)
		{
			// Worth flagging: these sit on top of the monitor preset, so an
			// unexpected modeline is most likely one of them rather than the
			// preset being wrong.
			groovy_log.notice("switchres: %u advanced override(s) applied on top of the monitor preset.", applied);
		}

		// A forced modeline replaces the preset's monitor range entirely
		// (switchres derives the range from the modeline), so every CRT limit
		// the preset carried is gone. That is a big enough foot-gun to say out
		// loud rather than bury in a notice.
		if (!gm.sr_modeline.to_string().empty())
		{
			groovy_log.warning("switchres 'modeline' override is set: the monitor range is now derived from that modeline "
				"and the selected preset's CRT limits DO NOT APPLY. Make sure the timings are safe for your display.");
		}
	}

	bool output_base::init_common()
	{
		if (m_active)
			return true;

		if (!g_cfg.groovy_mister.enabled.get())
			return false;

		// Cache the connect parameters once (the background flip-retry
		// reconnects without re-reading config).
		m_host = g_cfg.groovy_mister.host.to_string();
		m_lz4  = lz4_to_wrapper(g_cfg.groovy_mister.lz4.get());
		m_mtu  = mtu_to_bytes(g_cfg.groovy_mister.mtu.get());

		if (m_mtu > 1500)
		{
			groovy_log.notice("MTU %u requires OSD 'Server -> Jumbo frames = On' on the MiSTer; "
				"CMD_INIT will not be ACKed otherwise.", m_mtu);
		}

		// Audio is negotiated at CMD_INIT. RPCS3 mixes at a fixed 48 kHz
		// (AudioFreq::FREQ_48K) and we always feed the MiSTer stereo
		// (downmixed if needed), so 48000/2 when the audio tap is enabled,
		// else off. The tap shares this gmw connection (owned by the video
		// path), so audio only flows while MiSTer video is connected.
		m_tap_audio  = g_cfg.groovy_mister.tap_audio.get();
		m_sound_rate = m_tap_audio ? 48000u : 0u; // -> SoundRateCode 3 / OFF
		m_sound_chan = m_tap_audio ? u8{2} : u8{0}; // stereo / OFF
		// Wire pixel format. The vendored lib derives its per-frame size from the
		// mode we negotiate at CMD_INIT, so m_bpp must agree with it exactly.
		const auto rgb_cfg = g_cfg.groovy_mister.rgb_mode.get();
		rgb_mode_to_wire(rgb_cfg, m_rgb_mode, m_bpp);
		groovy_log.notice("Pixel format: %s (%u bytes/px)", rgb_cfg, m_bpp);

		// The frame-delay pair is read live in the blit path (see there); only
		// the keepalive flag is cached, since it gates a per-idle-tick decision.
		m_keepalive = g_cfg.groovy_mister.keepalive.get();

		if (g_cfg.groovy_mister.frame_delay_line.get() == 0)
		{
			groovy_log.warning("Frame delay is set to auto (vsync line 0). The client predicts the sync line from its own "
				"measured emulation time, which assumes a synchronous frame loop — RPCS3's sender is deferred, so this is "
				"experimental. Set it back to 1 if pacing gets worse.");
		}
		if (!m_keepalive)
		{
			groovy_log.warning("MiSTer keepalive is disabled: the core will drop the session after its idle timeout "
				"(default 5 s) whenever RPCS3 stops producing frames — pausing or opening a menu will end it.");
		}

		// --- switchres: modeline calculator (calc-only / "dummy" backend) ---
		// We do NOT hardcode a modeline. switchres computes one from the PS3
		// source res/refresh + the monitor preset, exactly like RetroArch.
		// Empty preset == switchres default "generic_15" — the same thing the
		// reference RetroArch setup uses (crt_switch_resolution=4 -> no
		// sr_set_monitor). The actual sr_add_mode happens lazily in
		// ensure_mode() once the PS3 video mode is known.
		//
		// Ordering here is load-bearing. sr_set_option()/sr_set_monitor() only
		// write settings fields; the preset and CRT ranges are RESOLVED when
		// parse_options() runs, and that happens inside sr_init_disp()
		// (add_display -> parse_options). sr_load_ini() additionally re-runs
		// parse_options() itself. So everything must be set BEFORE sr_init_disp,
		// in least- to most-specific order, and resolved exactly once:
		//
		//     sr_init -> ini (bulk) -> preset -> per-option overrides -> init_disp
		//
		// (Previously the ini was loaded AFTER sr_init_disp, whose second
		// parse_options() silently made the ini beat the chosen preset.)
		//
		sr_init();
		sr_set_log_callback_info(reinterpret_cast<void*>(&sr_log_sink));
		sr_set_log_callback_debug(reinterpret_cast<void*>(&sr_log_sink));
		sr_set_log_callback_error(reinterpret_cast<void*>(&sr_log_sink));

		// Heads-up: sr_init() itself parses a "switchres.ini" from the process
		// working directory, before we get a say. A stray file next to the
		// executable therefore looks exactly like RPCS3 ignoring these settings —
		// so say so plainly if one is there.
		if (fs::is_file("switchres.ini"))
		{
			groovy_log.warning("A 'switchres.ini' in the working directory was auto-loaded by switchres itself. "
				"It is applied before the settings below and may override them — remove it if the modeline is not what you configured.");
		}

		const std::string sr_ini = g_cfg.groovy_mister.switchres_ini.to_string();
		if (!sr_ini.empty())
		{
			sr_load_ini(const_cast<char*>(sr_ini.c_str()));
			groovy_log.notice("switchres ini loaded: %s", sr_ini);
		}

		const std::string preset = g_cfg.groovy_mister.monitor_preset.to_string();
		if (!preset.empty())
		{
			sr_set_monitor(preset.c_str());
			groovy_log.notice("switchres monitor preset = '%s'", preset);
		}
		else
		{
			groovy_log.notice("switchres monitor preset = (switchres default)");
		}

		apply_switchres_options();

		sr_init_disp("dummy", nullptr);

		// Report what switchres actually resolved. Two reasons this is worth a
		// log line: an unknown preset name is NOT an error to switchres — it
		// falls back to generic_15 — and every advanced override defaults to
		// "unset", so this is the only way to see the values in force.
		{
			sr_state st{};
			sr_get_state(&st);
			// st.monitor is a fixed char[32]; copy it out so the logger sees a
			// std::string rather than an array.
			st.monitor[std::size(st.monitor) - 1] = '\0';
			const std::string resolved_monitor = st.monitor;

			groovy_log.notice("switchres state: monitor='%s' interlace=%d doublescan=%d dotclock_min=%.3f "
				"refresh_tolerance=%.3f super_width=%d aspect=%.4f h_size=%.3f h_shift=%.1f v_shift=%.1f pixel_precision=%d",
				resolved_monitor, st.interlace, st.doublescan, st.dotclock_min,
				st.refresh_tolerance, st.super_width, st.monitor_aspect,
				st.h_size, st.h_shift, st.v_shift, st.pixel_precision);

			// switchres does not treat an unknown preset name as an error: it
			// logs and silently falls back to generic_15, so without this the
			// user gets a working-but-wrong picture and no obvious cause.
			if (!preset.empty() && preset != resolved_monitor)
			{
				groovy_log.error("switchres did not accept monitor preset '%s' — it fell back to '%s'. "
					"Check the spelling against the preset list; the CRT limits in force are now the fallback's.",
					preset, resolved_monitor);
			}
		}

		m_sr_inited = true;

		// Install the trace sink BEFORE gmw_init so every CmdInit step (socket
		// create -> RIO register -> CMD_INIT -> ACK) and any failure reason is
		// captured into the GROOVY log channel. Verbosity is user-configurable:
		// 0 = setup/errors/reconnects only (default, quiet); 1 =
		// + per-frame Frame-Sleep timing; 2 = full ACK/echo/JOY/KBD telemetry.
		gmw_set_log_callback(&groovy_lib_log_sink,
			static_cast<int>(g_cfg.groovy_mister.lib_log_verbose.get()));

		groovy_log.notice("Connecting: host=%s mtu=%u lz4=%u", m_host, m_mtu, static_cast<u32>(m_lz4));

		// Bounded retry (~5 s). A freshly-started MiSTer core often is not
		// ready to ACK the first CMD_INIT within CmdInit's single 60 ms
		// window; one shot then permanently disabling MiSTer for the session
		// (the old behaviour) is exactly what made "fresh core + fresh RPCS3"
		// fail until a restart. Each attempt is fully clean (try_connect()
		// resets the gmw object first) so a transient first failure cannot
		// poison the retry.
		constexpr int k_attempts = 5;
		for (int a = 0; a < k_attempts; ++a)
		{
			if (try_connect())
			{
				finalize_connect_bringup();
				return true;
			}
			if (a + 1 < k_attempts)
			{
				groovy_log.warning("MiSTer connect attempt %d/%d failed; retrying...", a + 1, k_attempts);
				std::this_thread::sleep_for(std::chrono::seconds(1));
			}
		}

		// Still unreachable. Do NOT sr_deinit() / permanently disable: keep
		// retrying on subsequent flips (covers "RPCS3 started before the
		// Groovy core" and core reboots, with no RPCS3 restart needed).
		m_connect_pending = true;
		m_next_connect_try = std::chrono::steady_clock::now() + std::chrono::seconds(2);
		groovy_log.warning("MiSTer not reachable at %s after %d attempts. "
			"Keeping MiSTer output armed — will keep retrying in the background.", m_host, k_attempts);
		return false;
	}

	void output_base::poll_connect_on_flip()
	{
		// Cheap fast-out on the hot present path: only do anything if a
		// prior init armed the pending-retry state and we are not already
		// streaming.
		if (m_active || !m_connect_pending.load(std::memory_order_relaxed))
			return;

		const auto now = std::chrono::steady_clock::now();
		if (now < m_next_connect_try)
			return;
		// try_connect() does a blocking handshake (up to ~120 ms of getACK
		// while the core is down); throttle so it only periodically hitches
		// the present thread until the MiSTer appears.
		m_next_connect_try = now + std::chrono::seconds(2);

		if (try_connect())
		{
			m_connect_pending = false;
			finalize_connect_bringup();
		}
	}

	bool output_base::ensure_mode()
	{
		// PS3 source video mode (set by cellVideoOutConfigure; defaults to
		// 720p before the game configures). switchres maps (logical PS3 res,
		// refresh) -> a monitor-correct modeline.
		const auto& av = g_fxo->get<rsx::avconf>();

		// Optional user override: force a specific output resolution
		// regardless of the PS3 avconf. Useful for titles that render
		// shrunken inside their advertised mode (BlazBlue at 480p) — run
		// RPCS3 at 720p so the framebuffer fills, then pick 480p 4:3 here
		// to downres into a CRT-filling output. Refresh (NTSC/PAL) is
		// still derived from avconf so a game switching modes still flips
		// the refresh correctly.
		const auto override_res = g_cfg.groovy_mister.output_resolution.get();
		u32 src_w, src_h;
		if (override_res == groovy_mister_output_res::_auto)
		{
			src_w = av.resolution_x ? av.resolution_x : 1280;
			src_h = av.resolution_y ? av.resolution_y : 720;
		}
		else
		{
			switch (override_res)
			{
			case groovy_mister_output_res::_240p_4_3:  src_w =  320; src_h = 240; break;
			case groovy_mister_output_res::_480p_4_3:  src_w =  640; src_h = 480; break;
			case groovy_mister_output_res::_480p_ntsc: src_w =  720; src_h = 480; break;
			case groovy_mister_output_res::_576p_4_3:  src_w =  768; src_h = 576; break;
			case groovy_mister_output_res::_576p_pal:  src_w =  720; src_h = 576; break;
			case groovy_mister_output_res::_480p_16_9: src_w =  853; src_h = 480; break;
			case groovy_mister_output_res::_720p_16_9: src_w = 1280; src_h = 720; break;
			default:                                   src_w = 1280; src_h = 720; break;
			}
		}
		// PS3: 576 line modes are PAL/50 Hz, everything else NTSC/HD 59.94 Hz.
		const double src_hz = (av.resolution_id == CELL_VIDEO_OUT_RESOLUTION_576) ? 50.0 : 59.94;

		if (m_resources_ready && src_w == m_src_w && src_h == m_src_h && src_hz == m_src_hz)
			return true; // unchanged — fast path

		if (override_res != groovy_mister_output_res::_auto)
		{
			groovy_log.notice("Output resolution override active: %ux%u (PS3 avconf=%ux%u)",
				src_w, src_h, av.resolution_x, av.resolution_y);
		}

		groovy_log.notice("switchres: PS3 source %ux%u @ %.2fHz -> calculating modeline...", src_w, src_h, src_hz);

		sr_mode srm{};
		const int ok = sr_add_mode(static_cast<int>(src_w), static_cast<int>(src_h), src_hz, 0, &srm);
		if (!ok || srm.width <= 0 || srm.height <= 0)
		{
			groovy_log.error("sr_add_mode(%ux%u@%.2f) failed (rtn=%d). MiSTer output inactive until the mode changes.",
				src_w, src_h, src_hz, ok);
			m_resources_ready = false;
			return false;
		}

		// CRT-safety backstop: refuse anything past the cap regardless of
		// what switchres returned (the monitor preset should already bound it).
		groovy_mister::modeline ml{};
		ml.pclock    = static_cast<double>(srm.pclock) / 1000000.0;
		ml.h_active  = static_cast<u16>(srm.width);
		ml.h_begin   = static_cast<u16>(srm.hbegin);
		ml.h_end     = static_cast<u16>(srm.hend);
		ml.h_total   = static_cast<u16>(srm.htotal);
		ml.v_active  = static_cast<u16>(srm.height);
		ml.v_begin   = static_cast<u16>(srm.vbegin);
		ml.v_end     = static_cast<u16>(srm.vend);
		ml.v_total   = static_cast<u16>(srm.vtotal);
		ml.interlace = static_cast<u8>(srm.interlace);

		// Three separate checks, deliberately not one predicate — they fail for
		// different reasons and only one of them is the user's to override.
		if (!groovy_mister::is_well_formed(ml))
		{
			groovy_log.error("switchres returned a malformed modeline (%ux%u, hTotal=%u vTotal=%u). Refusing.",
				ml.h_active, ml.v_active, ml.h_total, ml.v_total);
			m_resources_ready = false;
			return false;
		}

		// Hard: one blit must fit the client's buffer. Not overridable — we
		// memcpy into it.
		if (!groovy_mister::fits_blit_buffer(ml, m_bpp))
		{
			groovy_log.error("Modeline %ux%u at %u bytes/px needs %llu bytes per blit, over the protocol limit of %u. "
				"Refusing. Pick a smaller output resolution%s.",
				ml.h_active, ml.v_active, m_bpp,
				static_cast<u64>(ml.h_active) * ml.v_active * m_bpp, groovy_mister::max_blit_bytes,
				m_bpp > 2 ? " or switch the pixel format to RGB565" : "");
			m_resources_ready = false;
			return false;
		}

		// Soft: the CRT-shape envelope. The user can downgrade this to a warning.
		if (!groovy_mister::is_crt_shape_safe(ml))
		{
			if (g_cfg.groovy_mister.crt_safety_limits.get())
			{
				groovy_log.error("Modeline %ux%u is outside the CRT safety limits (max %ux%u). Refusing. "
					"Uncheck 'CRT safety limits' in the MiSTer settings if your display really can take it.",
					ml.h_active, ml.v_active, groovy_mister::max_safe_h_active, groovy_mister::max_safe_v_active);
				m_resources_ready = false;
				return false;
			}

			groovy_log.warning("Modeline %ux%u is outside the CRT safety limits (max %ux%u) but they are disabled — sending it anyway. "
				"If the display shows nothing or loses sync, this is the first thing to re-enable.",
				ml.h_active, ml.v_active, groovy_mister::max_safe_h_active, groovy_mister::max_safe_v_active);
		}

		// interlace param to gmw_switchres, identical mapping to RetroArch
		// gfx_mister.c: progressive -> 0; interlaced -> (interlaced_fb ? 1 : 2).
		// Default interlaced_fb=false -> 2 ("progressive framebuffer", the
		// proven-working value from the reference logs); the client still
		// sends full progressive frames (field 0), which we already do.
		const bool il_fb = g_cfg.groovy_mister.interlaced_fb.get();
		const u8 il_param = srm.interlace ? (il_fb ? 1u : 2u) : 0u;

		// (Re)allocate the backend's capture resources to the new modeline
		// active area.
		m_dst_w = ml.h_active;
		m_dst_h = ml.v_active;

		groovy_log.notice("switchres -> %dx%d pclock=%.4fMHz h(%u %u %u) v(%u %u %u) il=%d->param%u vfreq=%.3f hfreq=%.1f",
			srm.width, srm.height, ml.pclock, ml.h_begin, ml.h_end, ml.h_total,
			ml.v_begin, ml.v_end, ml.v_total, srm.interlace, il_param, srm.vfreq, srm.hfreq);

		on_mode_resources(m_dst_w, m_dst_h);

		// Hand the modeline to the sender thread; it issues gmw_switchres
		// before its next blit so the Groovy UDP socket is only driven from
		// the sender thread (no RSX/sender socket race).
		{
			std::lock_guard lock(m_sr_mutex);
			m_pending_modeline = pending_modeline{
				ml.pclock,
				ml.h_active, ml.h_begin, ml.h_end, ml.h_total,
				ml.v_active, ml.v_begin, ml.v_end, ml.v_total,
				il_param };
			m_switchres_pending = true;
			// Retain a copy for the reconnect re-arm (see m_last_modeline).
			m_last_modeline = m_pending_modeline;
			m_have_modeline = true;
		}

		m_src_w = src_w;
		m_src_h = src_h;
		m_src_hz = src_hz;
		m_resources_ready = true;
		return true;
	}

	void output_base::consume_dump_hotkey()
	{
		// Hotkey-driven raw-frame dump: forward the GUI's edge-triggered
		// request to the sender thread, which owns the gmw object and will
		// build the dump dir + call gmw_set_frame_dump() on its next
		// iteration. exchange() makes this a one-shot per key press; multiple
		// presses just reset the counter (next iteration starts a fresh 120).
		if (g_user_asked_for_mister_frame_dump.exchange(false))
			m_dump_request.store(true, std::memory_order_release);
	}

	output_base::capture_rects output_base::compute_capture_rects(u16 src_w, u16 src_h) const
	{
		capture_rects rc{};

		const auto ps3_aspect_cfg = g_cfg.video.aspect_ratio.get();
		rc.content_an = (ps3_aspect_cfg == video_aspect::_16_9) ? 16 : 4;
		rc.content_ad = (ps3_aspect_cfg == video_aspect::_16_9) ?  9 : 3;

		// src content rect: crop bars out of the framebuffer.
		rc.src_content = areai(0, 0, src_w, src_h);
		const u64 fb_wH = static_cast<u64>(src_w) * rc.content_ad;
		const u64 fb_hW = static_cast<u64>(src_h) * rc.content_an;
		if (fb_wH > fb_hW)
		{
			// Framebuffer wider than content → pillarbox: crop left/right.
			const u32 cw = static_cast<u32>(static_cast<u64>(src_h) * rc.content_an / rc.content_ad);
			const u32 cx = (src_w - cw) / 2;
			rc.src_content = areai(cx, 0, cx + cw, src_h);
		}
		else if (fb_wH < fb_hW)
		{
			// Framebuffer taller than content → letterbox: crop top/bottom.
			const u32 ch = static_cast<u32>(static_cast<u64>(src_w) * rc.content_ad / rc.content_an);
			const u32 cy = (src_h - ch) / 2;
			rc.src_content = areai(0, cy, src_w, cy + ch);
		}

		// dst active rect: fit content into dst preserving content aspect.
		rc.dst_active = areai(0, 0, m_dst_w, m_dst_h);
		const u64 dst_wH = static_cast<u64>(m_dst_w) * rc.content_ad;
		const u64 dst_hW = static_cast<u64>(m_dst_h) * rc.content_an;
		if (dst_wH > dst_hW)
		{
			// dst wider than content → pillarbox in dst.
			const u32 aw = static_cast<u32>(static_cast<u64>(m_dst_h) * rc.content_an / rc.content_ad);
			const u32 ax = (m_dst_w - aw) / 2;
			rc.dst_active = areai(ax, 0, ax + aw, m_dst_h);
			rc.needs_dst_clear = (aw < m_dst_w);
		}
		else if (dst_wH < dst_hW)
		{
			// dst taller than content → letterbox in dst.
			const u32 ah = static_cast<u32>(static_cast<u64>(m_dst_w) * rc.content_ad / rc.content_an);
			const u32 ay = (m_dst_h - ah) / 2;
			rc.dst_active = areai(0, ay, m_dst_w, ay + ah);
			rc.needs_dst_clear = (ah < m_dst_h);
		}

		// Fast path = direct copy of src (no scratch, no scale). Only valid
		// when src exactly matches dst AND no aspect cropping is needed on
		// either end.
		const bool aspect_native = (fb_wH == fb_hW);
		rc.fast_path = (src_w == m_dst_w && src_h == m_dst_h && aspect_native);

		return rc;
	}

	void output_base::pack_bgra_to_wire(u8* out, const u8* in, size_t pixels, u32 bpp)
	{
		// Capture readback is B8G8R8A8: in[0]=B in[1]=G in[2]=R.
		if (bpp == 2)
		{
			// RGB565, little-endian (standard libretro RGB565 — what
			// RetroArch's SCALER_FMT_RGB565 feeds the core). If R/B look
			// swapped on the CRT the core wants B5G6R5: swap b<->r below.
			for (size_t i = 0; i < pixels; ++i)
			{
				const u32 b = in[0], g = in[1], r = in[2];
				const u16 px = static_cast<u16>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
				out[0] = static_cast<u8>(px & 0xFF);
				out[1] = static_cast<u8>(px >> 8);
				in += 4;
				out += 2;
			}
		}
		else
		{
			// BGRA -> BGR strip-alpha. Tight loop, ~1 MB at 480p.
			for (size_t i = 0; i < pixels; ++i)
			{
				out[0] = in[0];
				out[1] = in[1];
				out[2] = in[2];
				in += 4;
				out += 3;
			}
		}
	}

	void output_base::do_groovy_close()
	{
		// Run exactly once, regardless of which thread calls (sender thread
		// normally; shutdown() as a fallback).
		if (m_closed.exchange(true))
			return;

		if (!m_active)
			return; // never connected (e.g. gmw_init failed) — nothing to release

		// Stop the cell_audio thread feeding the tap before teardown; the
		// ring is cleared so a later Stop->Start session starts clean.
		groovy_mister_audio_tap::set_active(false);

		// Tell the MiSTer to drop the stream so it returns to its
		// connection-search screen instead of freezing on the last frame.
		// CMD_CLOSE is a single UDP datagram; send it a few times (spread out)
		// as cheap loss insurance, then do the full teardown. All from the
		// Groovy-socket-owning thread (the sender thread), which is why the
		// previous RSX-thread gmw_close() silently lost the datagram on RIO.
		for (int i = 0; i < 3; ++i)
		{
			gmw_send_close();
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
		gmw_close();
		groovy_log.success("Sent CMD_CLOSE; MiSTer stream released (returns to connection-search).");
	}

	void output_base::shutdown()
	{
		// Stop the background flip-retry first so no new connect can race
		// the teardown below.
		m_connect_pending = false;

		if (m_sender_thread.joinable())
		{
			m_quit = true;
			m_queue_cv.notify_all();
			m_sender_thread.join();
		}

		// The sender thread already issued CMD_CLOSE + gmw_close() on its way
		// out (it owns the Groovy socket). This is a fallback for the case the
		// sender thread never ran (init failed / connect still pending); the
		// once-flag makes it a no-op otherwise. gmw_close() here also frees a
		// gmw object left over from a failed/pending connect so it can't
		// leak across to the next game launch.
		do_groovy_close();
		if (!m_active && m_sr_inited)
			gmw_close(); // free a gmw left by a failed/pending connect
			             // (do_groovy_close() early-outs when never active;
			             // gated on m_sr_inited so a MiSTer-disabled session
			             // never touches gmw)

		if (m_sr_inited)
		{
			sr_deinit();
			m_sr_inited = false;
		}

		m_active = false;
		m_resources_ready = false;
		m_switchres_pending = false;
		m_src_w = m_src_h = 0;
		m_src_hz = 0.0;

		// Backend capture resources (incl. pending captures) released last so
		// any in-flight GPU ref is gone.
		release_capture_resources();

		{
			std::lock_guard lock(m_queue_mutex);
			m_queue.clear();
		}
	}

	void output_base::push_to_sender(queued_frame&& f)
	{
		{
			std::lock_guard lock(m_queue_mutex);
			if (m_queue.size() >= MAX_RING)
			{
				// Sender is behind (bandwidth-bound); drop oldest. Counter is
				// a member so the heartbeat can report it (telemetry).
				if ((m_dropped_total++ % 60) == 0)
				{
					groovy_log.warning("Sender backlog; dropping frame %u (total drops=%llu).",
						f.frame_idx, m_dropped_total);
				}
				m_queue.erase(m_queue.begin());
			}
			m_queue.push_back(std::move(f));
		}
		m_queue_cv.notify_one();
	}

	void output_base::send_keepalive_if_idle()
	{
		if (!m_keepalive || !gmw_is_connected())
			return;

		const u64 now = monotonic_ms();
		const u64 idle_ms = now - m_last_wire_ms;
		if (idle_ms < KEEPALIVE_IDLE_MS)
			return;

		// 1-byte CMD_GET_STATUS. Any datagram resets the core's activity timer;
		// this is simply the one with no side effects.
		gmw_send_keepalive();
		m_last_wire_ms = now;

		// Drain the send completion queue. On Windows the keepalive rides the
		// same RIO send path as a blit, so it posts a completion — and it fires
		// precisely when no blit is running to drain one. The client's ring is
		// 846 slices, so a session left paused would silently exhaust it in
		// ~28 minutes and every later send would fail. gmw_waitSync() ends in
		// drainSendCompletions() and also refreshes the cached FPGA status.
		// Cheap here: with no blit in flight its pacing loop has nothing to wait
		// for, and the client clamps any implausible raster correction.
		gmw_waitSync();

		groovy_log.trace("MiSTer keepalive sent (idle %llu ms).", idle_ms);
	}

	void output_base::sender_loop()
	{
		thread_ctrl::set_native_priority(1);

		u64 sent = 0;

		// Display-freeze watchdog state: a hung FPGA
		// display keeps ACKing blits — frameEcho advances, so the vendored
		// no-ACK-advance reconnect never fires — while the display counter
		// (st.frame) freezes. Tracked per-blit below.
		u32 disp_last_frame = 0;
		u32 disp_stuck_blits = 0;

		// The vendored client auto-replays its stashed CmdSwitchres on every
		// successful in-CmdBlit reconnect, but that replay can itself fail its
		// ACK, and the C wrapper discards CmdSwitchres's return so neither we nor
		// it can tell. So the epoch is load-bearing, not just telemetry: on every
		// change we realign the blit counter and re-arm the modeline ourselves.
		u32 last_reconnect_epoch = gmw_reconnect_epoch();

		while (!m_quit)
		{
			queued_frame f;
			{
				std::unique_lock lock(m_queue_mutex);

				// Timed wait, not an unbounded one: when the RSX thread stops
				// flipping (paused, loading, in a menu) this loop is the only
				// thing left that can put a datagram on the wire, and the core
				// drops a silent session after its idle timeout. Waking on a
				// short period lets us hold the session open — and because the
				// send is gated on time-since-last-datagram rather than on this
				// period, waking often costs nothing during normal play.
				const bool have_frame = m_queue_cv.wait_for(lock,
					std::chrono::milliseconds(KEEPALIVE_POLL_MS),
					[this] { return m_quit || !m_queue.empty(); });

				if (m_quit) break;

				if (!have_frame)
				{
					// Idle tick. Nothing to blit; hold the session open if we
					// have gone quiet for long enough.
					lock.unlock();
					send_keepalive_if_idle();
					continue;
				}
				// Newest-wins: always send the most recent capture and discard
				// anything staler (counted as drops, same telemetry as the
				// push-side ring overflow). A FIFO here builds a standing
				// queue (observed qdepth=2 ≈ +2 frames of latency) whenever
				// the RSX flip rate transiently outpaces the paced sender —
				// the FPGA frameskips surplus frames anyway, so sending stale
				// ones only adds latency, never smoothness.
				if (m_queue.size() > 1)
				{
					m_dropped_total += m_queue.size() - 1;
					m_queue.erase(m_queue.begin(), m_queue.end() - 1);
				}
				f = std::move(m_queue.front());
				m_queue.erase(m_queue.begin());
			}

			// Auto-reconnect handling. The client reconnects internally from
			// inside CmdBlit; when it does, the core has started a COMPLETELY
			// FRESH session, so per-session state on our side must be realigned:
			//
			//  1. the blit counter — the core restarted its own at zero, and a
			//     host that keeps counting from the dead session sits thousands
			//     of frames ahead. The client clamps that spread away rather
			//     than hanging, but the raster servo then contributes nothing
			//     and pacing quietly degrades to coarse frame-time;
			//  2. the modeline — CmdInit zeroes the core's notion of it, and a
			//     core with no modeline silently DISCARDS ALL VIDEO for the rest
			//     of the session. The client replays its own stash, but that
			//     replay can fail its ACK and the C wrapper drops the return, so
			//     we re-arm unconditionally: a redundant CMD_SWITCHRES is
			//     harmless, a missing one is unrecoverable;
			//  3. the display-freeze watchdog — its counters describe a core
			//     that no longer exists.
			{
				const u32 cur_epoch = gmw_reconnect_epoch();
				if (cur_epoch != last_reconnect_epoch)
				{
					last_reconnect_epoch = cur_epoch;

					m_blit_frame = 0;
					disp_last_frame = 0;
					disp_stuck_blits = 0;

					bool rearmed = false;
					{
						std::lock_guard lock(m_sr_mutex);
						if (m_have_modeline)
						{
							m_pending_modeline = m_last_modeline;
							m_switchres_pending = true;
							rearmed = true;
						}
					}

					groovy_log.warning("MiSTer auto-reconnect observed (epoch %u); blit counter realigned%s.",
						cur_epoch, rearmed ? ", modeline re-armed" : " (no modeline yet)");
				}
			}

			// Raw-frame dump start request (Ctrl+Shift+D hotkey or the settings
			// "Dump Raw Frames Now" button). The flag was set on the RSX thread
			// (record_capture) from the GUI's global; we pick it up here so the
			// gmw_set_frame_dump() write (and the per-blit DumpFrame reads from
			// CmdBlit below) all happen on the sender thread. The dump itself is
			// the vendored client's job from this point on — it auto-stops after
			// the configured frame count.
			if (m_dump_request.exchange(false, std::memory_order_acquire))
			{
				// Directory + count are user-configurable (Settings → GroovyMiSTer →
				// Frame Dump); empty dir falls back to <config>/mister_frames/.
				std::string dir = g_cfg.groovy_mister.frame_dump_dir.to_string();
				if (dir.empty())
					dir = fs::get_config_dir() + "mister_frames/";
				if (dir.back() != '/' && dir.back() != '\\')
					dir += '/';

				const u32 count = static_cast<u32>(g_cfg.groovy_mister.frame_dump_frames.get());

				if (!fs::create_path(dir))
				{
					groovy_log.error("MiSTer frame dump: could not create %s — dump request ignored.", dir);
				}
				else
				{
					gmw_set_frame_dump(dir.c_str(), count);
					groovy_log.success("MiSTer frame dump armed: next %u raw frames -> %s",
						count, dir);
				}
			}

			// Apply a pending switchres before any blit, so the FPGA is in
			// the right mode for the frames that follow. Only the sender
			// thread drives the Groovy UDP socket.
			{
				pending_modeline pm{};
				bool have = false;
				{
					std::lock_guard lock(m_sr_mutex);
					if (m_switchres_pending)
					{
						pm = m_pending_modeline;
						m_switchres_pending = false;
						have = true;
					}
				}
				if (have)
				{
					// CmdSwitchres ACKs and retries 3x internally; a non-zero
					// return means the core never confirmed it. That is not a
					// cosmetic failure — CmdInit zeroes the core's modeline and
					// only a processed CMD_SWITCHRES restores it, so an
					// unconfirmed one leaves the core discarding every
					// subsequent video packet for the rest of the session.
					// Re-arm rather than blit into a core with no modeline; the
					// next sender iteration retries.
					const int rc = gmw_switchres(pm.pclock_mhz,
						pm.ha, pm.hb, pm.he, pm.ht,
						pm.va, pm.vb, pm.ve, pm.vt,
						pm.interlace);
					m_last_wire_ms = monotonic_ms();

					if (rc != 0)
					{
						groovy_log.error("gmw_switchres was not acknowledged by the core (%ux%u). "
							"Video would be discarded until a modeline lands — retrying on the next frame.",
							pm.ha, pm.va);

						std::lock_guard lock(m_sr_mutex);
						m_pending_modeline = pm;
						m_switchres_pending = true;
						continue;
					}

					groovy_log.success("gmw_switchres sent: %ux%u pclock=%.4fMHz h(%u %u %u) v(%u %u %u) interlace=%u",
						pm.ha, pm.va, pm.pclock_mhz, pm.hb, pm.he, pm.ht, pm.vb, pm.ve, pm.vt, pm.interlace);
					m_blit_frame = 0; // restart frame ordering after a mode set
				}
			}

			char* dst = gmw_get_pBufferBlit(0);
			if (!dst)
			{
				groovy_log.error("gmw_get_pBufferBlit returned null; aborting sender.");
				break;
			}

			// FPGA status from the last ACK (cheap struct copy; refreshed by
			// the previous iteration's gmw_waitSync -> getACK).
			gmw_fpgaStatus st{};
			gmw_getStatus(&st);

			// Mirror PS3 audio FIRST, every iteration. Only when the FPGA
			// reports audio; the sender thread is the sole owner of the
			// Groovy RIO socket. gmw_audio's soundSize is u16 and SendStream
			// splits by MTU, so cap well under 64 KB / BUFFER_SIZE (per-frame
			// audio is ~3 KB).
			if (st.audio)
			{
				if (char* abuf = gmw_get_pBufferAudio())
				{
					const u32 n = groovy_mister_audio_tap::read(abuf, 60000);
					if (n)
					{
						gmw_audio(static_cast<u16>(n));
						m_last_wire_ms = monotonic_ms();
					}
				}
			}

			// FPGA pacing was shelved: RGB565 already keeps the
			// link well inside budget (sender ring empty, zero UDP loss),
			// while gating on FPGA echo only discarded ~9% of displayable
			// frames for no benefit (frameskip=1 is the NORMAL steady state
			// for the interlace(2) modeline, not an over-send signal). Every
			// captured frame is blitted; backpressure is the drop-oldest in
			// push_to_sender (telemetry: heartbeat qdepth/drops).
			std::memcpy(dst, f.bgr.data(), f.bgr.size());

			// Frame-number resync. The protocol wants a monotonically increasing
			// number, and the client's raster servo derives its sub-frame
			// correction from how far ours sits from the FPGA's free-running
			// counter — so if the FPGA has run ahead we must catch up or the
			// correction is computed against a stale position. Mirror RetroArch
			// gfx_mister.c: bump, and jump past the FPGA if it leads.
			++m_blit_frame;
			if (st.frame > m_blit_frame)
			{
				// A genuine "the core moved past us" is a one- or two-frame
				// condition. A lead of thousands is not a resync — it is a
				// session change we failed to notice (the epoch handler above
				// should have caught it), and feeding it back is what desyncs
				// the raster servo. Adopt it either way so we never blit behind
				// the core, but make the anomaly loud rather than silent.
				const u32 lead = st.frame - m_blit_frame;
				if (lead > RASTER_RESYNC_WARN_FRAMES)
				{
					groovy_log.warning("MiSTer blit counter jumped %u frames (ours %u -> core %u) — stale session state? Raster sync will be coarse until it settles.",
						lead, m_blit_frame, st.frame + 1);
				}
				m_blit_frame = st.frame + 1;
			}

			// Frame delay. Default vCountSync=1 (sync at line 1) is RetroArch's
			// default and today's proven behaviour; 0 selects the library's
			// "auto frame delay", which predicts from its own measured emulation
			// time and assumes a synchronous render->blit->waitSync cadence —
			// not our deferred sender thread. margin only applies in auto mode
			// and the API takes nanoseconds.
			//
			// Read live rather than cached: these are meant to be A/B'd against a
			// CRT mid-session, and both are plain atomic loads.
			const u16 vcount_sync = static_cast<u16>(g_cfg.groovy_mister.frame_delay_line.get());
			const u32 fd_margin_ns = static_cast<u32>(g_cfg.groovy_mister.frame_delay_margin_us.get()) * 1000u;

			gmw_blit(m_blit_frame, /*field=*/0, vcount_sync, fd_margin_ns, /*matchDeltaBytes=*/0);
			gmw_waitSync();
			m_last_wire_ms = monotonic_ms();

			++sent;

			// Display-freeze watchdog: we are actively blitting (this code runs
			// once per sent frame), so if the FPGA's free-running display counter
			// stops advancing for ~3s of blits, the core's display side is hung
			// (observed on a MiSTer USB hotplug: HPS keeps ACKing, CRT frozen).
			// An emulator stall cannot false-positive here — it pauses this loop
			// at the queue wait instead. Warn-only: no auto-reconnect (a CmdInit
			// storm could fight a transient); repeat the warning every ~10s.
			if (st.frame != disp_last_frame)
			{
				disp_last_frame = st.frame;
				disp_stuck_blits = 0;
			}
			else if (++disp_stuck_blits == 180 || (disp_stuck_blits > 180 && (disp_stuck_blits % 600) == 0))
			{
				groovy_log.error("FPGA display counter frozen at %u for %u blits while ACKs still flow — core-side display hang (e.g. USB hotplug on the MiSTer). Reload/power-cycle the Groovy core to recover.",
					st.frame, disp_stuck_blits);
			}

			// First frame: confirm the pipeline produced and shipped a frame end to end.
			if (sent == 1)
			{
				groovy_log.success("First frame blitted to MiSTer (%ux%u, %zu bytes, blit_frame=%u).",
					m_dst_w, m_dst_h, f.bgr.size(), m_blit_frame);
			}

			// Heartbeat every ~2s (assuming 60Hz). Reads back the FPGA's last ACK so the
			// RPCS3 log alone tells you whether the device is receiving and displaying.
			// vramSynced == 1 means the FPGA has a real (non-red) picture.
			if ((sent % 120) == 0)
			{
				gmw_fpgaStatus st_hb{};
				gmw_getStatus(&st_hb);

				// Bandwidth telemetry: raw bytes/frame (== lib m_RGBSize) and
				// approx raw rate at ~60Hz, sender queue depth, and total +
				// delta dropped frames (drops => link/FPGA can't keep up).
				// FPGA-side pressure: vgaFrameskip / vramQueue.
				const u64 raw_bytes = static_cast<u64>(m_dst_w) * m_dst_h * m_bpp;
				size_t qdepth = 0;
				u64 drops_total = 0;
				{
					std::lock_guard lock(m_queue_mutex);
					qdepth = m_queue.size();
					drops_total = m_dropped_total;
				}
				const u64 drops_delta = drops_total - m_dropped_at_last_hb;
				m_dropped_at_last_hb = drops_total;

				// Telemetry: count of vendored auto-reconnects so
				// far this session. Stays 0 unless the FPGA actually
				// vanished and was rediscovered without an RPCS3 restart.
				const u32 reconnects_total = gmw_reconnect_epoch();

				groovy_log.notice(
					"heartbeat: sent=%llu blit_frame=%u | fpga frame=%u echo=%u vramSynced=%u vramReady=%u vblank=%u audio=%u frameskip=%u vramQueue=%u | %s %ux%u raw=%.0fKB/f ~%.1fMB/s | qdepth=%zu drops=%llu(+%llu) reconnects=%u",
					sent, m_blit_frame, st_hb.frame, st_hb.frameEcho, st_hb.vramSynced, st_hb.vramReady, st_hb.vgaVblank, st_hb.audio,
					st_hb.vgaFrameskip, st_hb.vramQueue,
					(m_bpp == 2 ? "RGB565" : "RGB888"), m_dst_w, m_dst_h,
					raw_bytes / 1024.0, raw_bytes * 60.0 / 1.0e6,
					qdepth, drops_total, drops_delta, reconnects_total);
			}
		}

		// Sender thread owns the Groovy socket: release the stream here so the
		// CMD_CLOSE goes out on the same thread that drove all the blits
		// (eliminates the cross-thread Windows-RIO send loss). shutdown() only
		// calls this as a fallback.
		do_groovy_close();
	}
}
