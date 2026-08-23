#pragma once

#include "system_config_types.h"
#include "Utilities/Config.h"

enum CellNetCtlState : s32;
enum CellSysutilLicenseArea : s32;
enum CellSysutilLang : s32;
enum CellKbMappingType : s32;

struct cfg_root : cfg::node
{
	struct node_core : cfg::node
	{
		node_core(cfg::node* _this) : cfg::node(_this, "Core") {}

		cfg::_enum<ppu_decoder_type> ppu_decoder{ this, "PPU Decoder", ppu_decoder_type::llvm };
		cfg::_int<1, 8> ppu_threads{ this, "PPU Threads", 2 }; // Amount of PPU threads running simultaneously (must be 2)
		cfg::_bool ppu_debug{ this, "PPU Debug" };
		cfg::_bool ppu_call_history{ this, "PPU Calling History" }; // Enable PPU calling history recording
		cfg::_bool llvm_logs{ this, "Save LLVM logs" };
		cfg::string llvm_cpu{ this, "Use LLVM CPU" };
		cfg::_int<0, 1024> llvm_threads{ this, "Max LLVM Compile Threads", 0 };
		cfg::_bool ppu_llvm_greedy_mode{ this, "PPU LLVM Greedy Mode", false, false };
		cfg::_bool llvm_precompilation{ this, "LLVM Precompilation", true };
		cfg::_enum<thread_scheduler_mode> thread_scheduler{this, "Thread Scheduler Mode", thread_scheduler_mode::os};
		cfg::_bool set_daz_and_ftz{ this, "Set DAZ and FTZ", false };
		cfg::_enum<spu_decoder_type> spu_decoder{ this, "SPU Decoder", spu_decoder_type::llvm };
		cfg::uint<0, 100> spu_reservation_busy_waiting_percentage{ this, "SPU Reservation Busy Waiting Percentage 1", 100, true };
		cfg::_bool spu_reservation_busy_waiting_enabled{ this, "SPU Reservation Busy Waiting Enabled", false, true };
		cfg::uint<0, 100> spu_getllar_busy_waiting_percentage{ this, "SPU GETLLAR Busy Waiting Percentage", 100, true };
		cfg::_bool spu_getllar_spin_optimization_disabled{ this, "Disable SPU GETLLAR Spin Optimization", false, true };
		cfg::_bool spu_debug{ this, "SPU Debug" };
		cfg::_bool mfc_debug{ this, "MFC Debug" };
		cfg::_int<0, 6> preferred_spu_threads{ this, "Preferred SPU Threads", 0, true }; // Number of hardware threads dedicated to heavy simultaneous spu tasks
		cfg::_int<0, 16> spu_delay_penalty{ this, "SPU delay penalty", 3 }; // Number of milliseconds to block a thread if a virtual 'core' isn't free
		cfg::_bool spu_loop_detection{ this, "SPU loop detection", false }; // Try to detect wait loops and trigger thread yield
		cfg::_int<1, 6> max_spurs_threads{ this, "Max SPURS Threads", 6, true }; // HACK. If less then 6, max number of running SPURS threads in each thread group.
		cfg::_enum<spu_block_size_type> spu_block_size{ this, "SPU Block Size", spu_block_size_type::safe };
		cfg::_bool spu_accurate_dma{ this, "Accurate SPU DMA", false };
		cfg::_bool spu_accurate_reservations{ this, "Accurate SPU Reservations", true };
		cfg::_bool accurate_cache_line_stores{ this, "Accurate Cache Line Stores", false };
		cfg::_bool rsx_accurate_res_access{this, "Accurate RSX reservation access", false, true};

		struct fifo_setting : public cfg::_enum<rsx_fifo_mode>
		{
			using _enum = cfg::_enum<rsx_fifo_mode>;
			using _enum::_enum;

			explicit operator bool() const
			{
				return get() != rsx_fifo_mode::fast;
			}
		};

		fifo_setting rsx_fifo_accuracy{this, "RSX FIFO Fetch Accuracy", rsx_fifo_mode::atomic };
		cfg::_bool spu_verification{ this, "SPU Verification", true }; // Should be enabled
		cfg::_bool spu_cache{ this, "SPU Cache", true };
		cfg::_bool spu_prof{ this, "SPU Profiler", false };
		cfg::_bool ppu_prof{ this, "PPU Profiler", false };
		cfg::uint<0, 16> mfc_transfers_shuffling{ this, "MFC Commands Shuffling Limit", 0 };
		cfg::uint<0, 10000> mfc_transfers_timeout{ this, "MFC Commands Timeout", 0, true };
		cfg::_bool mfc_shuffling_in_steps{ this, "MFC Commands Shuffling In Steps", false, true };
		cfg::_enum<xfloat_accuracy> spu_xfloat_accuracy{ this, "SPU XFloat Accuracy", xfloat_accuracy::approximate, false };
		cfg::_int<-1, 14> ppu_128_reservations_loop_max_length{ this, "Accurate PPU 128-byte Reservation Op Max Length", 0, true }; // -1: Always accurate, 0: Never accurate, 1-14: max accurate loop length
		cfg::_int<-64, 64> stub_ppu_traps{ this, "Stub PPU Traps", 0, true }; // Hack, skip PPU traps for rare cases where the trap is continueable (specify relative instructions to skip)
		cfg::_bool precise_spu_verification{ this, "Precise SPU Verification", false }; // Disables use of xorsum based spu verification if enabled.
		cfg::_bool ppu_llvm_nj_fixup{ this, "PPU LLVM Java Mode Handling", true }; // Partially respect current Java Mode for alti-vec ops by PPU LLVM
		cfg::_bool ppu_fix_vnan{ this, "PPU Vector NaN Handling", true }; // Accuracy. Partial.
		cfg::_bool use_accurate_dfma{ this, "Use Accurate DFMA", true }; // Enable accurate double-precision FMA for CPUs which do not support it natively
		cfg::_bool ppu_set_sat_bit{ this, "PPU Set Saturation Bit", false }; // Accuracy. If unset, completely disable saturation flag handling.
		cfg::_bool ppu_use_nj_bit{ this, "PPU Accurate Non-Java Mode", false }; // Accuracy. If set, accurately emulate NJ flag. Implies NJ fixup.
		cfg::_bool ppu_set_vnan{ this, "PPU Accurate Vector NaN Values", false }; // Accuracy. Implies ppu_fix_vnan.
		cfg::_bool ppu_set_fpcc{ this, "PPU Set FPCC Bits", false }; // Accuracy.

		cfg::_bool debug_console_mode{ this, "Debug Console Mode", false }; // Debug console emulation, not recommended
		cfg::_bool hook_functions{ this, "Hook static functions" };
		cfg::set_entry libraries_control{ this, "Libraries Control" }; // Override HLE/LLE behaviour of selected libs
		cfg::_bool hle_lwmutex{ this, "HLE lwmutex" }; // Force alternative lwmutex/lwcond implementation
		cfg::uint64 spu_llvm_lower_bound{ this, "SPU LLVM Lower Bound" };
		cfg::uint64 spu_llvm_upper_bound{ this, "SPU LLVM Upper Bound", 0xffffffffffffffff };

		cfg::_int<10, 3000> clocks_scale{ this, "Clocks scale", 100 }; // Changing this from 100 (percentage) may affect game speed in unexpected ways
		cfg::uint<0, 3000> spu_wakeup_delay{ this, "SPU Wake-Up Delay", 0, true };
		cfg::uint<0, (1 << 6) - 1> spu_wakeup_delay_mask{ this, "SPU Wake-Up Delay Thread Mask", (1 << 6) - 1, true };
		cfg::uint<0, 400> max_cpu_preempt_count_per_frame{ this, "Max CPU Preempt Count", 0, true };
		cfg::_bool allow_rsx_cpu_preempt{ this, "Allow RSX CPU Preemptions", true, true };
#if defined (__linux__) || defined (__APPLE__)
		cfg::_enum<sleep_timers_accuracy_level> sleep_timers_accuracy{ this, "Sleep Timers Accuracy", sleep_timers_accuracy_level::_as_host, true };
#else
		cfg::_enum<sleep_timers_accuracy_level> sleep_timers_accuracy{ this, "Sleep Timers Accuracy", sleep_timers_accuracy_level::_usleep, true };
#endif
		cfg::_int<-1000, 1500> usleep_addend{ this, "Usleep Time Addend", 0, true };

		cfg::uint64 perf_report_threshold{this, "Performance Report Threshold", 500, true}; // In µs, 0.5ms = default, 0 = everything
		cfg::_bool perf_report{this, "Enable Performance Report", false, true}; // Show certain perf-related logs
		cfg::_bool external_debugger{this, "Assume External Debugger"};
	} core{ this };

	struct node_vfs : cfg::node
	{
		node_vfs(cfg::node* _this) : cfg::node(_this, "VFS") {}

		cfg::_bool host_root{ this, "Enable /host_root/" };
		cfg::_bool init_dirs{ this, "Initialize Directories", true };

		cfg::_bool limit_cache_size{ this, "Limit disk cache size", false };
		cfg::_int<0, 10240> cache_max_size{ this, "Disk cache maximum size (MB)", 5120 };
		cfg::_bool empty_hdd0_tmp{ this, "Empty /dev_hdd0/tmp/", true };

	} vfs{ this };

	struct node_video : cfg::node
	{
		node_video(cfg::node* _this) : cfg::node(_this, "Video") {}

#if defined(HAVE_VULKAN)
		cfg::_enum<video_renderer> renderer{ this, "Renderer", video_renderer::vulkan };
#else
		cfg::_enum<video_renderer> renderer{ this, "Renderer", video_renderer::opengl };
#endif

		cfg::_enum<video_resolution> resolution{ this, "Resolution", video_resolution::_720p };
		cfg::_enum<video_aspect> aspect_ratio{ this, "Aspect ratio", video_aspect::_16_9 };
		cfg::_enum<frame_limit_type> frame_limit{ this, "Frame limit", frame_limit_type::_auto, true };
		cfg::_float<0, 1000> second_frame_limit{ this, "Second Frame Limit", 0, true }; // 0 disables its effect
		cfg::_enum<msaa_level> antialiasing_level{ this, "MSAA", msaa_level::_auto };
		cfg::_enum<shader_mode> shadermode{ this, "Shader Mode", shader_mode::async_recompiler };
		cfg::_enum<gpu_preset_level> shader_precision{ this, "Shader Precision", gpu_preset_level::high };
		cfg::_enum<vsync_mode> vsync{ this, "VSync Mode", vsync_mode::off, true };

		cfg::_bool write_color_buffers{ this, "Write Color Buffers" };
		cfg::_bool write_depth_buffer{ this, "Write Depth Buffer" };
		cfg::_bool read_color_buffers{ this, "Read Color Buffers" };
		cfg::_bool read_depth_buffer{ this, "Read Depth Buffer" };
		cfg::_bool handle_tiled_memory{ this, "Handle RSX Memory Tiling", false, true };
		cfg::_bool log_programs{ this, "Log shader programs" };
		cfg::_bool debug_output{ this, "Debug output" };
		cfg::_bool debug_overlay{ this, "Debug overlay", false, true };
		cfg::_bool renderdoc_compatiblity{ this, "Renderdoc Compatibility Mode" };
		cfg::_bool use_gpu_texture_scaling{ this, "Use GPU texture scaling", false };
		cfg::_bool stretch_to_display_area{ this, "Stretch To Display Area", false, true };
		cfg::_bool force_high_precision_z_buffer{ this, "Force High Precision Z buffer" };
		cfg::_bool strict_rendering_mode{ this, "Strict Rendering Mode" };
		cfg::_enum<framebuffer_aliasing_bias> fb_aliasing_bias{ this, "Framebuffer Aliasing Heuristic Bias", framebuffer_aliasing_bias::_auto, true };
		cfg::_bool disable_zcull_queries{ this, "Disable ZCull Occlusion Queries", false, true };
		cfg::_bool disable_video_output{ this, "Disable Video Output", false, true };
		cfg::_bool disable_vertex_cache{ this, "Disable Vertex Cache", false };
		cfg::_bool disable_FIFO_reordering{ this, "Disable FIFO Reordering", false };
		cfg::_bool emulate_depth_compare{ this, "Emulate Special Depth Comparison", false };
		cfg::_bool frame_skip_enabled{ this, "Enable Frame Skip", false, true };
		cfg::_bool force_cpu_blit_processing{ this, "Force CPU Blit", false, true }; // Debugging option
		cfg::_bool disable_on_disk_shader_cache{ this, "Disable On-Disk Shader Cache", false };
		cfg::_bool disable_vulkan_mem_allocator{ this, "Disable Vulkan Memory Allocator", false };
		cfg::_bool full_rgb_range_output{ this, "Use full RGB output range", true, true }; // Video out dynamic range
		cfg::_bool strict_texture_flushing{ this, "Strict Texture Flushing", false };
		cfg::_bool multithreaded_rsx{ this, "Multithreaded RSX", false };
		cfg::_bool relaxed_zcull_sync{ this, "Relaxed ZCULL Sync", false };
		cfg::_bool force_hw_MSAA_resolve{ this, "Force Hardware MSAA Resolve", false, true };
		cfg::_bool stereo_enabled{ this, "3D Display Enabled", false };
		cfg::_enum<stereo_render_mode_options> stereo_render_mode{ this, "3D Display Mode", stereo_render_mode_options::disabled, true };
		cfg::_int<10, 99> screen_size{ this, "Screen size in inches", 24, false };
		cfg::_bool debug_program_analyser{ this, "Debug Program Analyser", false };
		cfg::_bool precise_zpass_count{ this, "Accurate ZCULL stats", true };
		cfg::_int<1, 8> consecutive_frames_to_draw{ this, "Consecutive Frames To Draw", 1, true};
		cfg::_int<1, 8> consecutive_frames_to_skip{ this, "Consecutive Frames To Skip", 1, true};
		cfg::uint<25, 800> resolution_scale_percent{ this, "Resolution Scale", 100, true };
		cfg::uint<0, 16> anisotropic_level_override{ this, "Anisotropic Filter Override", 0, true };
		cfg::_float<-32, 32> texture_lod_bias{ this, "Texture LOD Bias Addend", 0, true };
		cfg::uint<1, 1024> min_scalable_dimension{ this, "Minimum Scalable Dimension", 16, true };
		cfg::_int<0, 16> shader_compiler_threads_count{ this, "Shader Compiler Threads", 0 };
		cfg::_int<0, 30000000> driver_recovery_timeout{ this, "Driver Recovery Timeout", 1000000, true };
		cfg::uint<0, 16667> driver_wakeup_delay{ this, "Driver Wake-Up Delay", 0, true };
		cfg::_int<1, 6000> vblank_rate{ this, "Vblank Rate", 60, true }; // Changing this from 60 may affect game speed in unexpected ways
		cfg::_bool vblank_ntsc{ this, "Vblank NTSC Fixup", false, true };
		cfg::_bool decr_memory_layout{ this, "DECR memory layout", false}; // Force enable increased allowed main memory range as DECR console
		cfg::_bool host_label_synchronization{ this, "Allow Host GPU Labels", false };
		cfg::_bool disable_msl_fast_math{ this, "Disable MSL Fast Math", false };
		cfg::_bool disable_async_host_memory_manager{ this, "Disable Asynchronous Memory Manager", false, true };
		cfg::_enum<output_scaling_mode> output_scaling{ this, "Output Scaling Mode", output_scaling_mode::bilinear, true };
		cfg::_bool record_with_overlays{ this, "Record With Overlays", true, true };
		cfg::_bool disable_hardware_texel_remapping{ this, "Disable Hardware ColorSpace Remapping", false, true };
		cfg::uint<0, 100> rcas_sharpening_intensity{ this, "FidelityFX CAS Sharpening Intensity", 50, true };

		struct node_vk : cfg::node
		{
			node_vk(cfg::node* _this) : cfg::node(_this, "Vulkan") {}

			cfg::string adapter{ this, "Adapter" };
			cfg::_bool force_primitive_restart{ this, "Force primitive restart flag" };
			cfg::_enum<vk_exclusive_fs_mode> exclusive_fullscreen_mode{ this, "Exclusive Fullscreen Mode", vk_exclusive_fs_mode::unspecified};
			cfg::_bool asynchronous_texture_streaming{ this, "Asynchronous Texture Streaming", false };
			cfg::_enum<vk_gpu_scheduler_mode> asynchronous_scheduler{ this, "Asynchronous Queue Scheduler", vk_gpu_scheduler_mode::safe };
			cfg::uint<256, 65536> vram_allocation_limit{ this, "VRAM allocation limit (MB)", 65536, false };
			cfg::_bool use_rebar_upload_heap{ this, "Use Re-BAR for GPU uploads", true, false };

		} vk{ this };

		struct node_perf_overlay : cfg::node
		{
			node_perf_overlay(cfg::node* _this) : cfg::node(_this, "Performance Overlay") {}

			cfg::_bool enabled{ this, "Enabled", false, true };
			cfg::_bool framerate_graph_enabled{ this, "Enable Framerate Graph", false, true };
			cfg::_bool frametime_graph_enabled{ this, "Enable Frametime Graph", false, true };
			cfg::uint<2, 6000> framerate_datapoint_count{ this, "Framerate datapoints", 50, true };
			cfg::uint<2, 6000> frametime_datapoint_count{ this, "Frametime datapoints", 170, true };
			cfg::_enum<detail_level> level{ this, "Detail level", detail_level::medium, true };
			cfg::_enum<perf_graph_detail_level> framerate_graph_detail_level{ this, "Framerate graph detail level", perf_graph_detail_level::show_all, true };
			cfg::_enum<perf_graph_detail_level> frametime_graph_detail_level{ this, "Frametime graph detail level", perf_graph_detail_level::show_all, true };
			cfg::uint<1, 1000> update_interval{ this, "Metrics update interval (ms)", 350, true };
			cfg::uint<4, 36> font_size{ this, "Font size (px)", 10, true };
			cfg::_enum<screen_quadrant> position{ this, "Position", screen_quadrant::top_left, true };
			cfg::string font{ this, "Font", "n023055ms.ttf", true };
			cfg::_float<0, 100> margin_x{ this, "Horizontal Margin (%)", 4, true }; // horizontal distance to the window border relative to the screen_quadrant in percent of the window width
			cfg::_float<0, 100> margin_y{ this, "Vertical Margin (%)", 7, true }; // vertical distance to the window border relative to the screen_quadrant in percent of the window height
			cfg::_bool center_x{ this, "Center Horizontally", false, true };
			cfg::_bool center_y{ this, "Center Vertically", false, true };
			cfg::uint<0, 100> opacity{ this, "Opacity (%)", 70, true };
			cfg::string color_body{ this, "Body Color (hex)", "#FFE138FF", true };
			cfg::string background_body{ this, "Body Background (hex)", "#002339FF", true };
			cfg::string color_title{ this, "Title Color (hex)", "#F26C24FF", true };
			cfg::string background_title{ this, "Title Background (hex)", "#00000000", true };
			cfg::_bool use_window_space{this, "Use Window Space", false, true};

		} perf_overlay{ this };

		struct node_shader_preloading_dialog : cfg::node
		{
			node_shader_preloading_dialog(cfg::node* _this) : cfg::node(_this, "Shader Loading Dialog") {}

			cfg::_bool use_custom_background{ this, "Allow custom background", true, true };
			cfg::uint<0, 100> darkening_strength{ this, "Darkening effect strength", 30, true };
			cfg::uint<0, 100> blur_strength{ this, "Blur effect strength", 0, true };

		} shader_preloading_dialog{ this };

		struct anaglyph_matrices : cfg::node
		{
			anaglyph_matrices(cfg::node* _this) : cfg::node(_this, "Custom Anaglyph Matrices") {}

			cfg::node_map_entry left{ this, "Matrix Left" };
			cfg::node_map_entry right{ this, "Matrix Right" };

		} custom_anaglyph_matrices{ this };

	} video{ this };

	struct node_groovy_mister : cfg::node
	{
		node_groovy_mister(cfg::node* _this) : cfg::node(_this, "GroovyMister") {}

		cfg::_bool enabled{ this, "Enabled", false };
		cfg::string host{ this, "MiSTer Host", "127.0.0.1" };
		// switchres monitor preset. Empty = switchres default ("generic_15"),
		// which is exactly what the reference RetroArch setup uses
		// (crt_switch_resolution=4 -> no sr_set_monitor -> generic_15).
		// Other values: arcade_15 / arcade_31 / pc_31_120 / generic_15 / ntsc /
		// pal / vga ... (see switchres monitor presets).
		cfg::string monitor_preset{ this, "Monitor Preset", "" };
		// Optional path to a switchres.ini (sr_load_ini) for power users.
		cfg::string switchres_ini{ this, "Switchres Ini", "" };
		// = RetroArch's "mister_interlaced_fb". When switchres yields an
		// interlaced modeline: false -> send interlace=2 (progressive
		// framebuffer, the proven-working default); true -> interlace=1.
		// Consumed inside ensure_mode(), which re-runs whenever the source mode
		// changes — so a change here lands on the next mode switch.
		cfg::_bool interlaced_fb{ this, "Interlaced Framebuffer", false, true };
		// Override the source resolution fed to switchres + downscale
		// target. Default Auto = use the PS3 avconf (current behaviour).
		// Any other value forces the MiSTer pipeline to downres an
		// RPCS3-rendered framebuffer to the chosen output — useful for
		// titles that render shrunken inside their advertised mode (e.g.
		// BlazBlue Calamity Trigger at 480p): run RPCS3 at 720p so the
		// game fills its framebuffer, then pick 480p 4:3 here. Refresh
		// (50/59.94 Hz) is still inferred from the PS3 video mode.
		// Re-read on every flip via ensure_mode(), so this one really is live.
		cfg::_enum<groovy_mister_output_res> output_resolution{ this, "Output Resolution Override", groovy_mister_output_res::_auto, true };
		// Read per flip in the presenter.
		cfg::_enum<groovy_mister_host_display> host_display{ this, "Host Display While Active", groovy_mister_host_display::parallel, true };
		cfg::_bool tap_audio{ this, "Mirror Audio To MiSTer", true };
		// Wire pixel format (CMD_INIT byte[4]), fixed for the session. RGB565
		// costs a third fewer raw bytes than RGB888 (and gives LZ4/NLC less to
		// chew) at the cost of banding — worth it when the link or the FPGA is
		// bandwidth-bound (choppy / dropped frames). Replaces the old
		// "Force RGB565" bool; an existing config simply falls back to RGB888.
		cfg::_enum<groovy_mister_rgb_mode> rgb_mode{ this, "RGB Mode", groovy_mister_rgb_mode::rgb888 };
		// Only 1500 and 3800 mean anything to the core, and 3800 additionally
		// requires OSD Server -> Jumbo frames = On. Stored as a plain number, so
		// a config carrying the old free-form integer still reads back if it was
		// one of these two.
		cfg::_enum<groovy_mister_mtu> mtu{ this, "MTU", groovy_mister_mtu::_1500 };
		// Wire-side codec. NLC is the default — the deterministic near-lossless
		// codec (wire format v2) decoded by the autonomous FPGA display engine.
		// Off = raw, LZ4 / LZ4 HC are the general-purpose fallbacks.
		cfg::_enum<groovy_mister_lz4> lz4{ this, "LZ4 Compression", groovy_mister_lz4::nlc_tiled };
		// NLC tuning — only effective when the codec above is NLC. Both ride the
		// CMD_INIT byte[1] packing, so they are fixed for the session.
		//
		// Pack = the entropy front-end (byte[1] bit 7). Rice is the default: it is
		// what brings heavy 3D content under the core's ~38 MB/s ingest ceiling.
		// Rice REQUIRES a core with the Rice decoder (the rbf_rice_r3 kit) — on an
		// older core the bit is ignored and Rice bytes are misparsed as Tiled
		// (garbage picture). Tiled works on any NLC core and is better on flat/2D
		// content (Rice has a 1-bit/sample floor).
		//
		// NEAR = quantization (bits [3:2]). 0 = lossless. Rice + NEAR 1 is the
		// HW-validated default: a demanding 3D title runs at a LOCKED
		// 60 fps (330 KB avg / 472 KB peak = 28 MB/s, 34% headroom under the
		// MiSTer's measured ~38 MB/s ingest ceiling), and NEAR 0/1/2 were confirmed
		// VISUALLY IDENTICAL on the CRT.
		//   tiled near0: 625/636 KB avg/worst — 38.1 MB/s, AT the ceiling (the lag)
		//   rice  near0: 573/583 KB — 35.0 MB/s, no headroom (see caveat below)
		//   rice  near1: 413/423 KB — 25.4 MB/s, 34% headroom   <- DEFAULT
		//   rice  near2: 350/359 KB — 21.5 MB/s, reserve knob
		// NEAR 0 caveat: on very heavy scenes its peaks (~650 KB = a full frame period
		// of ingest) can saturate the receiver and produce a few seconds of horizontal
		// noise banding until the scene lightens (bounded and self-recovering).
		// Colour is fixed at YCoCg-R (byte[1] bit 4); the RGB-direct path was retired.
		cfg::_enum<groovy_mister_nlc_pack> nlc_pack{ this, "NLC Pack", groovy_mister_nlc_pack::rice };
		cfg::_enum<groovy_mister_near_level> nlc_near_level{ this, "NLC NEAR Level", groovy_mister_near_level::near_1 };
		// Vendored client ([gmw]) log verbosity (gmw_set_log_callback), cumulative:
		// 0 = setup/handshake + errors + reconnect warnings + rare one-offs like an
		//     NLC->raw encode fallback (always emitted; quiet in steady state);
		// 1 = + one per-frame frame-pacing ("Frame Sleep") line;
		// 2 = + the full per-frame firehose (ACK/echo/VRAM-status bits, JOY/KBD
		//     state, and the per-frame NLC encode line) — a line EVERY frame, adds
		//     sender-thread latency; short, targeted debugging only.
		// Controls only the vendored client's lines; RPCS3's own GROOVY heartbeat /
		// capture traces are gated independently and unaffected.
		cfg::_enum<groovy_mister_lib_log> lib_log_verbose{ this, "MiSTer Library Log Verbosity", groovy_mister_lib_log::errors };
		// Raw pre-encode frame dump (GM_FRAME_DUMP). Count = how many blits to
		// capture before auto-stop. Directory empty = <config dir>/mister_frames/.
		// Consumed by both the Ctrl+Shift+D hotkey and the settings-tab button.
		// Read at arm time on the sender thread, so both are live.
		cfg::_int<1, 100000> frame_dump_frames{ this, "Frame Dump Frame Count", 120, true };
		cfg::string          frame_dump_dir{ this, "Frame Dump Directory", "", true };

		// Hold an idle session open. The core drops a session that sends nothing
		// on the video socket for its idle timeout (OSD Server -> Idle timeout,
		// default 5 s) and frees the CRT — so without this, pausing or opening a
		// menu for a few seconds ends the session. Leave on; the option exists to
		// restore pre-idle-timeout behaviour against a very old core.
		cfg::_bool keepalive{ this, "Keep Session Alive While Idle", true };

		// Vendored client's ACK watchdog: after ~10 blits with no frameEcho
		// advance it tears down the video side and reconnects, re-sending the
		// input subscribe and replaying the modeline. RPCS3 additionally realigns
		// its blit counter and re-arms the modeline on every reconnect.
		cfg::_bool auto_reconnect{ this, "Auto Reconnect", true };

		// Frame delay (advanced). vCountSync passed to CmdBlit: the raster line
		// the next frame's start syncs to. Later line = lower latency, less
		// margin before a late frame tears or frameskips. 1 is RetroArch's
		// default and our proven value. 0 selects the client's "auto frame
		// delay", which predicts from its own measured emulation time and
		// assumes a synchronous render->blit->waitSync loop — our sender thread
		// is deferred, so auto is EXPERIMENTAL here and wants on-cabinet A/B.
		// Dynamic on purpose: this exists to be A/B'd against a CRT, and having
		// to reboot the game between each trial would make that useless. The
		// sender thread re-reads both per blit (an atomic load each).
		cfg::_int<0, 1023> frame_delay_line{ this, "Frame Delay Vsync Line", 1, true };
		// Safety headroom for auto mode only, in microseconds (GroovyMAME's
		// mister_fd_margin is 1.5/2.0/3.0 ms). Ignored unless the line is 0.
		cfg::_int<0, 10000> frame_delay_margin_us{ this, "Frame Delay Margin", 0, true };

		// CRT-shape guidance, ON by default. When on, a modeline taller than 576
		// lines or wider than 1024 px is refused — the shape a 15/31 kHz CRT fed
		// by this core is expected to produce. Turning it OFF downgrades that to
		// a warning and lets the mode through; it does NOT lift the protocol's
		// hard per-blit byte limit (720x576x3 = 1,245,312 bytes, the client's
		// actual blit buffer), which is a memory bound and is always enforced.
		// Checked inside ensure_mode(), so a change applies at the next mode switch.
		cfg::_bool crt_safety_limits{ this, "CRT Safety Limits", true, true };

		// ---------------------------------------------------------------------
		// Advanced switchres overrides.
		//
		// Each maps 1:1 onto a switchres option of the same name (the SR_OPT_*
		// constants in 3rdparty/switchres/switchres_defines.h) and is passed
		// straight to sr_set_option(). EMPTY MEANS "DON'T CALL IT" — switchres's
		// own default stands, so a config with all of these blank behaves exactly
		// like a build without them. Value syntax and semantics are switchres's;
		// upstream's switchres.ini is the reference.
		//
		// Only options that actually affect modeline calculation are here. The
		// host-display ones (display, api, keep_changes, lock_*_modes and the
		// custom-video-backend group) are inert in this build: RPCS3 compiles
		// switchres with SR_CALC_ONLY and drives the dummy backend, because we
		// never switch a host display — we hand the numbers to the MiSTer. They
		// remain reachable via the switchres INI if ever needed.
		//
		// modeline_generation is deliberately absent: with the dummy backend it
		// is the only thing that creates a candidate mode, so setting it to 0
		// leaves switchres with nothing to choose from and output goes dark.
		// ---------------------------------------------------------------------

		// Custom monitor range, used when the preset is "custom". 14 comma-
		// separated fields; see switchres.ini. Multi-range monitors need
		// crt_range1-9 too — use the INI for those.
		cfg::string sr_crt_range0{ this, "Switchres crt_range0", "" };
		// Refresh range for the "lcd" preset, e.g. "50-61".
		cfg::string sr_lcd_range{ this, "Switchres lcd_range", "" };
		// Force a literal modeline in XFree86 format. DANGEROUS: switchres
		// derives the monitor range FROM this string, bypassing the selected
		// preset and every CRT limit it carried.
		cfg::string sr_modeline{ this, "Switchres modeline", "" };
		// Constrain the OUTPUT mode as <w>x<h>@<r>, 0 = wildcard. Distinct from
		// "Output Resolution Override", which changes what we ASK switchres for:
		// this pins what it may return. e.g. "0x240" = any width, force 240
		// lines — the only way to force a scanline count.
		cfg::string sr_user_mode{ this, "Switchres user_mode", "" };

		cfg::string sr_interlace{ this, "Switchres interlace", "" };
		cfg::string sr_doublescan{ this, "Switchres doublescan", "" };
		cfg::string sr_interlace_force_even{ this, "Switchres interlace_force_even", "" };
		cfg::string sr_dotclock_min{ this, "Switchres dotclock_min", "" };
		cfg::string sr_sync_refresh_tolerance{ this, "Switchres sync_refresh_tolerance", "" };
		cfg::string sr_super_width{ this, "Switchres super_width", "" };
		cfg::string sr_aspect{ this, "Switchres aspect", "" };
		cfg::string sr_h_size{ this, "Switchres h_size", "" };
		cfg::string sr_h_shift{ this, "Switchres h_shift", "" };
		cfg::string sr_v_shift{ this, "Switchres v_shift", "" };
		cfg::string sr_v_shift_correct{ this, "Switchres v_shift_correct", "" };
		cfg::string sr_pixel_precision{ this, "Switchres pixel_precision", "" };
		cfg::string sr_scale_proportional{ this, "Switchres scale_proportional", "" };

	} groovy_mister{ this };

	struct node_audio : cfg::node
	{
		node_audio(cfg::node* _this) : cfg::node(_this, "Audio") {}

		cfg::_enum<audio_renderer> renderer{ this, "Renderer", audio_renderer::cubeb, true };
		cfg::_enum<audio_provider> provider{ this, "Audio Provider", audio_provider::cell_audio, false };
		cfg::_enum<audio_avport> rsxaudio_port{ this, "RSXAudio Avport", audio_avport::hdmi_0, true };
		cfg::_bool dump_to_file{ this, "Dump to file", false, true };
		cfg::_bool convert_to_s16{ this, "Convert to 16 bit", false, true };
		cfg::_enum<audio_format> format{ this, "Audio Format", audio_format::stereo, false };
		cfg::uint<0, 0xFF> formats{ this, "Audio Formats", static_cast<u32>(audio_format_flag::lpcm_2_48khz), false };
		cfg::_enum<audio_channel_layout> channel_layout{ this, "Audio Channel Layout", audio_channel_layout::automatic, false };
		cfg::string audio_device{ this, "Audio Device", "@@@default@@@", true };
		cfg::_int<0, 200> volume{ this, "Master Volume", 100, true };
		cfg::_bool enable_buffering{ this, "Enable Buffering", true, true };
		cfg::_int <4, 250> desired_buffer_duration{ this, "Desired Audio Buffer Duration", 34, true };
		cfg::_bool enable_time_stretching{ this, "Enable Time Stretching", false, true };
		cfg::_bool disable_sampling_skip{ this, "Disable Sampling Skip", false, true };
		cfg::_int<0, 100> time_stretching_threshold{ this, "Time Stretching Threshold", 75, true };
		cfg::_enum<microphone_handler> microphone_type{ this, "Microphone Type", microphone_handler::null };
		cfg::string microphone_devices{ this, "Microphone Devices", "@@@@@@@@@@@@" };
		cfg::_enum<music_handler> music{ this, "Music Handler", music_handler::qt };
	} audio{ this };

	struct node_io : cfg::node
	{
		node_io(cfg::node* _this) : cfg::node(_this, "Input/Output") {}

		cfg::_enum<keyboard_handler> keyboard{ this, "Keyboard", keyboard_handler::null };
		cfg::_enum<mouse_handler> mouse{ this, "Mouse", mouse_handler::basic };
		cfg::_enum<camera_handler> camera{ this, "Camera", camera_handler::null };
		cfg::_enum<fake_camera_type> camera_type{ this, "Camera type", fake_camera_type::unknown };
		cfg::_enum<camera_flip> camera_flip_option{ this, "Camera flip", camera_flip::none, true };
		cfg::string camera_id{ this, "Camera ID", "Default", true };
		cfg::string sdl_camera_id{ this, "SDL Camera ID", "Default", true };
		cfg::_enum<move_handler> move{ this, "Move", move_handler::null, true };
		cfg::_enum<buzz_handler> buzz{ this, "Buzz emulated controller", buzz_handler::null };
		cfg::_enum<turntable_handler> turntable{this, "Turntable emulated controller", turntable_handler::null};
		cfg::_enum<ghltar_handler> ghltar{this, "GHLtar emulated controller", ghltar_handler::null};
		cfg::_enum<pad_handler_mode> pad_mode{this, "Pad handler mode", pad_handler_mode::single_threaded, true};
		cfg::_bool keep_pads_connected{this, "Keep pads connected", false, true};
		cfg::uint<0, 100'000> pad_sleep{this, "Pad handler sleep (microseconds)", 1'000, true};
		cfg::_bool background_input_enabled{this, "Background input enabled", true, true};
		cfg::_bool show_move_cursor{this, "Show move cursor", false, true};
		cfg::_bool paint_move_spheres{this, "Paint move spheres", false, true};
		cfg::_bool allow_move_hue_set_by_game{this, "Allow move hue set by game", false, true};
		cfg::_bool lock_overlay_input_to_player_one{this, "Lock overlay input to player one", false, true};
		cfg::string midi_devices{this, "Emulated Midi devices", "Keyboardßßß@@@Keyboardßßß@@@Keyboardßßß@@@"};
		cfg::_bool load_sdl_mappings{ this, "Load SDL GameController Mappings", true };
		cfg::_bool pad_debug_overlay{ this, "IO Debug overlay", false, true };
		cfg::_bool mouse_debug_overlay{ this, "Mouse Debug overlay", false, true };
		cfg::uint<1, 180> fake_move_rotation_cone_h{ this, "Fake Move Rotation Cone", 10, true };
		cfg::uint<1, 180> fake_move_rotation_cone_v{ this, "Fake Move Rotation Cone (Vertical)", 10, true };

	} io{ this };

	struct node_sys : cfg::node
	{
		static std::string get_random_system_name();
		static u128 get_random_psid();

		node_sys(cfg::node* _this) : cfg::node(_this, "System") {}

		cfg::_enum<CellSysutilLicenseArea> license_area{ this, "License Area", CellSysutilLicenseArea{1} }; // CELL_SYSUTIL_LICENSE_AREA_A
		cfg::_enum<CellSysutilLang> language{ this, "Language", CellSysutilLang{1} }; // CELL_SYSUTIL_LANG_ENGLISH_US
		cfg::_enum<CellKbMappingType> keyboard_type{ this, "Keyboard Type", CellKbMappingType{0} }; // CELL_KB_MAPPING_101 = US
		cfg::_enum<enter_button_assign> enter_button_assignment{ this, "Enter button assignment", enter_button_assign::cross };
		cfg::_enum<date_format> date_fmt{ this, "Date Format", date_format::ddmmyyyy };
		cfg::_enum<time_format> time_fmt{ this, "Time Format", time_format::clock24 };
		cfg::_int<-60*60*24*365*100LL, 60*60*24*365*100LL> console_time_offset{ this, "Console time offset (s)", 0 }; // console time offset, limited to +/-100years
		cfg::string system_name{this, "System Name", get_random_system_name()};
		cfg::uint128 console_psid{this, "Console PSID", get_random_psid()};
		cfg::string hdd_model{this, "HDD Model Name", ""};
		cfg::string hdd_serial{this, "HDD Serial Number", ""};
		cfg::node_map_entry sup_argv{ this, "Process ARGV" };
	} sys{ this };

	struct node_net : cfg::node
	{
		node_net(cfg::node* _this) : cfg::node(_this, "Net") {}

		cfg::_enum<np_internet_status> net_active{this, "Internet enabled", np_internet_status::disabled};
		cfg::string ip_address{this, "IP address", "0.0.0.0"};
		cfg::string bind_address{this, "Bind address", "0.0.0.0"};
		cfg::string dns{this, "DNS address", "8.8.8.8"};
		cfg::string swap_list{this, "IP swap list", ""};
		cfg::_bool upnp_enabled{this, "UPNP Enabled", false};
		cfg::_bool derive_mac_from_psid{this, "Derive MAC from PSID", false};

		cfg::_enum<np_psn_status> psn_status{this, "PSN status", np_psn_status::disabled};
		cfg::string country{this, "PSN Country", "us"};
		cfg::_bool clans_enabled{this, "Clans Enabled", false};
	} net{this};

	struct node_savestate : cfg::node
	{
		node_savestate(cfg::node* _this) : cfg::node(_this, "Savestate") {}

		cfg::_bool start_paused{ this, "Start Paused", false }; // Pause on first frame
		cfg::_bool suspend_emu{ this, "Suspend Emulation Savestate Mode", false }; // Close emulation when saving, delete save after loading
		cfg::_bool compatible_mode{ this, "Compatible Savestate Mode", false }; // SPU emulation optimized for savestate compatibility (off by default for performance reasons)
		cfg::_bool state_inspection_mode{ this, "Inspection Mode Savestates" }; // Save memory stored in executable files, thus allowing to view state without any files (for debugging)
		cfg::_bool save_disc_game_data{ this, "Save Disc Game Data", false };
		cfg::uint<0, 64> max_files{ this, "Maximum SaveState Files", 4 };
		cfg::uint<0, 1024 * 512> max_files_size{ this, "Maximum SaveState Files Space (MiB)", 4096 };
	} savestate{this};

	struct node_misc : cfg::node
	{
		node_misc(cfg::node* _this) : cfg::node(_this, "Miscellaneous") {}

		cfg::_bool autostart{ this, "Automatically start games after boot", true, true };
		cfg::_bool autoexit{ this, "Exit RPCS3 when process finishes", false, true };
		cfg::_bool autopause{ this, "Pause emulation on RPCS3 focus loss", false, true };
		cfg::_bool start_fullscreen{ this, "Start games in fullscreen mode", true, true };
		cfg::_bool prevent_display_sleep{ this, "Prevent display sleep while running games", true, true };
		cfg::_bool show_trophy_popups{ this, "Show trophy popups", true, true };
		cfg::_bool show_rpcn_popups{ this, "Show RPCN popups", true, true };
		cfg::_bool show_shader_compilation_hint{ this, "Show shader compilation hint", true, true };
		cfg::_bool show_ppu_compilation_hint{ this, "Show PPU compilation hint", true, true };
		cfg::_bool show_autosave_autoload_hint{ this, "Show autosave/autoload hint", false, true };
		cfg::_bool show_pressure_intensity_toggle_hint{ this, "Show pressure intensity toggle hint", true, true };
		cfg::_bool show_analog_limiter_toggle_hint{ this, "Show analog limiter toggle hint", true, true };
		cfg::_bool show_mouse_and_keyboard_toggle_hint{ this, "Show mouse and keyboard toggle hint", true, true };
		cfg::_bool show_fatal_error_hints{ this, "Show fatal error hints", false, true };
		cfg::_bool show_capture_hints{ this, "Show capture hints", true, true };
		cfg::_bool use_native_interface{ this, "Use native user interface", true };
		cfg::_bool use_recursive_scan{this, "Use recursive scan", false};
		cfg::string gdb_server{ this, "GDB Server", "127.0.0.1:2345" };
		cfg::_bool silence_all_logs{ this, "Silence All Logs", false, true };
		cfg::string title_format{ this, "Window Title Format", "FPS: %F | %R | %V | %T [%t]", true };
		cfg::_bool pause_during_home_menu{this, "Pause Emulation During Home Menu", false, false };
		cfg::_bool play_music_during_boot{this, "Play music during boot sequence", true, true };
		cfg::_bool enable_gamemode{ this, "Enable GameMode", false, false };

	} misc{ this };

	cfg::log_entry log{ this, "Log" };

	std::string name{};
};

extern cfg_root g_cfg;
extern cfg_root g_backup_cfg;
