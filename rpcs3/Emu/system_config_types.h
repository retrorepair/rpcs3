#pragma once

enum class ppu_decoder_type : unsigned
{
	_static,
	llvm,
};

enum class spu_decoder_type : unsigned
{
	_static,
	dynamic,
	asmjit,
	llvm,
};

enum class spu_block_size_type
{
	safe,
	mega,
	giga,
};

enum class sleep_timers_accuracy_level
{
	_as_host,
	_usleep,
	_all_timers,
};

enum class keyboard_handler
{
	null,
	basic,
};

enum class mouse_handler
{
	null,
	basic,
	raw
};

enum class video_renderer
{
	null,
	opengl,
	vulkan,
};

enum class audio_renderer
{
	null,
#ifdef _WIN32
	xaudio,
#endif
	cubeb,
#ifdef HAVE_FAUDIO
	faudio,
#endif
};

enum class audio_provider
{
	none,
	cell_audio,
	rsxaudio
};

enum class audio_avport
{
	hdmi_0,
	hdmi_1,
	avmulti,
	spdif_0,
	spdif_1
};

enum class audio_format
{
	stereo,
	surround_5_1,
	surround_7_1,
	automatic,
	manual,
};

enum class audio_format_flag : unsigned
{
	lpcm_2_48khz   = 0x00000000, // Linear PCM 2 Ch. 48 kHz (always available)
	lpcm_5_1_48khz = 0x00000001, // Linear PCM 5.1 Ch. 48 kHz
	lpcm_7_1_48khz = 0x00000002, // Linear PCM 7.1 Ch. 48 kHz
	ac3            = 0x00000004, // Dolby Digital 5.1 Ch.
	dts            = 0x00000008, // DTS 5.1 Ch.
};

enum class audio_channel_layout
{
	automatic,
	mono,
	stereo,
	stereo_lfe,
	quadraphonic,
	quadraphonic_lfe,
	surround_5_1,
	surround_7_1,
};

enum class music_handler
{
	null,
	qt
};

enum class camera_handler
{
	null,
	fake,
	qt,
#ifdef HAVE_SDL3
	sdl,
#endif
};

enum class camera_flip
{
	none,
	horizontal,
	vertical,
	both
};

enum class fake_camera_type
{
	unknown,
	eyetoy,
	eyetoy2,
	uvc1_1,
};

enum class move_handler
{
	null,
	real,
	fake,
	mouse,
	raw_mouse,
#ifdef HAVE_LIBEVDEV
	gun
#endif
};

enum class buzz_handler
{
	null,
	one_controller,
	two_controllers,
};

enum class turntable_handler
{
	null,
	one_controller,
	two_controllers,
};

enum class ghltar_handler
{
	null,
	one_controller,
	two_controllers,
};

enum class microphone_handler
{
	null,
	standard,
	singstar,
	real_singstar,
	rocksmith,
};

enum class pad_handler_mode
{
	single_threaded, // All pad handlers run on the same thread sequentially.
	multi_threaded   // Each pad handler has its own thread.
};

enum class video_resolution
{
	_1080p,
	_1080i,
	_720p,
	_480p,
	_480i,
	_576p,
	_576i,
	_1600x1080p,
	_1440x1080p,
	_1280x1080p,
	_960x1080p,
};

enum class video_aspect
{
	_4_3,
	_16_9,
};

enum class groovy_mister_host_display
{
	parallel, // host window continues presenting alongside MiSTer output
	headless, // host window present is skipped, MiSTer is the only output
};

enum class groovy_mister_lz4
{
	off,
	lz4,
	lz4_hc,
	// NLC near-lossless codec (wire format v2) — the default. The entropy
	// front-end (TILED / RICE) and quantization are separate knobs:
	// groovy_mister.nlc_pack / nlc_near_level. Selecting NLC changes CMD_INIT
	// byte[1] semantics — requires a core with NLC v2 decode support; older
	// cores treat any codec value >1 as raw (safe fallback).
	nlc_tiled,
};

// NLC entropy front-end (CMD_INIT byte[1] bit 7). Rides the same CMD_INIT as the
// codec, so it is fixed for the session.
enum class groovy_mister_nlc_pack
{
	// Block-adaptive bit widths. Better on flat / 2D content (Rice has a
	// 1-bit-per-sample floor). Works on any NLC-capable core.
	tiled,
	// Golomb-Rice entropy coding. Better on photographic / 3D content — this is
	// what brings heavy 3D under the core's ~38 MB/s ingest ceiling.
	// REQUIRES a core with the Rice decoder (the rbf_rice_r3 kit): an older core
	// ignores bit 7 and would misparse Rice bytes as TILED (garbage picture).
	rice,
};

// MiSTer output-resolution override. "Auto" feeds the PS3 avconf
// (resolution_x/y) to switchres as today. Any other value forces the
// MiSTer modeline + downscale to that res — useful for titles that
// render shrunken inside their advertised mode (e.g. BlazBlue Calamity
// Trigger at 480p): run RPCS3 at 720p so the framebuffer fills, then
// the MiSTer pipeline downresses to the chosen output. Refresh
// (NTSC/PAL) is still inferred from avconf.resolution_id.
enum class groovy_mister_output_res
{
	_auto,         // use PS3 avconf as today
	_240p_4_3,     // 320x240, 4:3 (15kHz arcade)
	_480p_4_3,     // 640x480, 4:3 (15kHz arcade or 31kHz VGA)
	_480p_ntsc,    // 720x480, NTSC (anamorphic 3:2 pixel grid)
	_576p_4_3,     // 768x576, 4:3 (PAL square pixel)
	_576p_pal,     // 720x576, PAL (anamorphic)
	_480p_16_9,    // 853x480, 16:9 square pixel
	_720p_16_9,    // 1280x720, 16:9
};

// Wire pixel format (CMD_INIT byte[4]). Fixed for the session.
enum class groovy_mister_rgb_mode
{
	rgb888,   // 3 B/px — the reference format
	rgba888,  // 4 B/px — alpha is carried but never displayed; here for completeness
	rgb565,   // 2 B/px — a third fewer raw bytes at the cost of banding
};

// MTU. Only two values are meaningful to the core; 3800 additionally requires
// OSD Server -> Jumbo frames = On, and the format strings are the numbers so the
// stored value stays a plain integer.
enum class groovy_mister_mtu
{
	_1500,
	_3800,
};

// NLC quantization (CMD_INIT byte[1] bits [3:2]). The format strings are "0".."3"
// so this stays wire- and config-compatible with the plain integer it replaces —
// only the UI gains labels.
enum class groovy_mister_near_level
{
	near_0,   // lossless
	near_1,   // recommended default
	near_2,
	near_3,
};

// Vendored client log verbosity. Format strings are "0".."2" for the same
// config-compatibility reason as above.
enum class groovy_mister_lib_log
{
	errors,   // 0: setup/handshake/errors/reconnects — quiet in steady state
	pacing,   // 1: + per-frame frame-pacing line and the RIO telemetry summary
	trace,    // 2: + the full per-frame ACK/VRAM/input firehose
};

enum class frame_limit_type
{
	none,
	_30,
	_50,
	_60,
	_120,
	display_rate,
	_auto,
	_ps3,
	infinite,
};

enum class msaa_level
{
	none,
	_auto
};

enum class framebuffer_aliasing_bias
{
	_auto,
	prefer_color,
	prefer_depth,
};

enum class detail_level
{
	none,
	minimal,
	low,
	medium,
	high,
};

enum class screen_quadrant
{
	top_left,
	top_right,
	bottom_left,
	bottom_right
};

enum class rsx_fifo_mode : unsigned
{
	fast,
	atomic,
	atomic_ordered,
	as_ps3,
};

enum class enter_button_assign
{
	circle, // CELL_SYSUTIL_ENTER_BUTTON_ASSIGN_CIRCLE
	cross   // CELL_SYSUTIL_ENTER_BUTTON_ASSIGN_CROSS
};

enum class date_format
{
	yyyymmdd, // CELL_SYSUTIL_DATE_FMT_YYYYMMDD
	ddmmyyyy, // CELL_SYSUTIL_DATE_FMT_DDMMYYYY
	mmddyyyy  // CELL_SYSUTIL_DATE_FMT_MMDDYYYY
};

enum class time_format
{
	clock12, // CELL_SYSUTIL_TIME_FMT_CLOCK12
	clock24  // CELL_SYSUTIL_TIME_FMT_CLOCK24
};

enum class np_internet_status
{
	disabled,
	enabled,
};

enum class np_psn_status
{
	disabled,
	psn_fake,
	psn_rpcn,
};

enum class shader_mode
{
	recompiler,
	async_recompiler,
	async_with_interpreter,
	interpreter_only
};

enum class vk_exclusive_fs_mode
{
	unspecified,
	disable,
	enable
};

enum class vk_gpu_scheduler_mode
{
	safe,
	fast
};

enum class thread_scheduler_mode
{
	os,
	old,
	alt
};

enum class perf_graph_detail_level
{
	minimal,
	show_min_max,
	show_one_percent_avg,
	show_all
};

enum class zcull_precision_level
{
	precise,
	approximate,
	relaxed,
	undefined
};

enum class gpu_preset_level
{
	ultra,
	high,
	low,
	_auto
};

enum class output_scaling_mode
{
	nearest,
	bilinear,
	fsr
};

enum class stereo_render_mode_options
{
	disabled,
	side_by_side,
	over_under,
	interlaced,
	anaglyph_red_green,
	anaglyph_red_blue,
	anaglyph_red_cyan,
	anaglyph_magenta_cyan,
	anaglyph_trioscopic,
	anaglyph_amber_blue,
	anaglyph_custom,
};

enum class xfloat_accuracy
{
	accurate,
	approximate,
	relaxed, // Approximate accuracy for only the "FCGT", "FNMS", "FREST" AND "FRSQEST" instructions
	inaccurate
};

enum class vsync_mode
{
	off,
	adaptive,
	full,
};
