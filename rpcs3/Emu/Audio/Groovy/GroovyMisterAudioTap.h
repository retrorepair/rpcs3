#pragma once

#include "util/types.hpp"

// Process-global bridge that mirrors the PS3's already-mixed audio to a
// MiSTer (Groovy_MiSTer), in parallel with RPCS3's normal host audio.
//
// Why a bridge: the Groovy UDP socket is RIO and must only ever be driven
// by the MiSTer video sender thread (the same single-owner-socket rule that
// applies to blit / CMD_CLOSE / input). RPCS3 mixes audio on the
// cell_audio thread, so it cannot call gmw_audio() directly. Instead the
// cell_audio thread taps the post-mix PCM here (producer); the MiSTer
// sender thread drains it and issues gmw_audio() (consumer).
//
// Audio-side only: no VK / groovymister includes, so cellAudio gains no
// extra dependency and there is no cyclic include.
namespace groovy_mister_audio_tap
{
	// Enabled by the MiSTer video path while its gmw connection is up
	// (and g_cfg.groovy_mister.tap_audio). Clears any queued PCM on a
	// state transition so a new session starts clean.
	void set_active(bool on);
	bool is_active();

	// Producer — cell_audio thread. `src` is post-mix interleaved float
	// PCM, `frames` frames of `src_ch` channels (the PS3 mix, before the
	// host-side downmix). Converted to s16 LE stereo interleaved and
	// queued. No-op when inactive; never blocks; drops oldest on overflow.
	void write(const f32* src, u32 frames, u32 src_ch);

	// Consumer — MiSTer sender thread. Pops up to `max_bytes` of queued
	// s16 stereo PCM into `dst`. Returns the number of bytes written.
	u32 read(void* dst, u32 max_bytes);
}
