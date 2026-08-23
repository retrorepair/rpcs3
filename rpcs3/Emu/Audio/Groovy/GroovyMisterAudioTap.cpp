#include "stdafx.h"
#include "GroovyMisterAudioTap.h"

#include "Emu/Audio/AudioBackend.h"
#include "util/logs.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <vector>

LOG_CHANNEL(gm_audio_log, "GMAudio");

namespace groovy_mister_audio_tap
{
	// s16 stereo @48kHz = 192000 B/s. This is only a safety cap: the sender
	// thread drains every blit (~3 KB at 60Hz), so the ring is normally
	// near-empty. On a sender stall we drop oldest so latency self-corrects.
	static constexpr u32 RING_CAP = 256 * 1024;

	static std::atomic<bool> g_active{false};

	static std::mutex g_mtx;
	static std::vector<u8> g_ring;   // sized to RING_CAP on first activate
	static u32 g_head = 0;           // read pos
	static u32 g_count = 0;          // bytes queued
	static u64 g_drops = 0;          // overflow accounting (rate-limited log)

	void set_active(bool on)
	{
		std::lock_guard lock(g_mtx);
		if (g_active.load(std::memory_order_relaxed) == on)
			return;

		if (on && g_ring.size() != RING_CAP)
			g_ring.resize(RING_CAP);

		// Drop any stale PCM so a new session starts clean.
		g_head = 0;
		g_count = 0;
		g_active.store(on, std::memory_order_release);
		gm_audio_log.notice("MiSTer audio tap %s", on ? "active" : "inactive");
	}

	bool is_active()
	{
		return g_active.load(std::memory_order_acquire);
	}

	static void ring_push(const u8* src, u32 bytes)
	{
		// Caller holds g_mtx. Drop-oldest on overflow (advance read pos).
		if (bytes >= RING_CAP)
		{
			// Pathologically large chunk: keep only the newest RING_CAP.
			src += bytes - RING_CAP;
			bytes = RING_CAP;
		}

		if (g_count + bytes > RING_CAP)
		{
			const u32 overflow = g_count + bytes - RING_CAP;
			g_head = (g_head + overflow) % RING_CAP;
			g_count -= overflow;
			g_drops += overflow;
			if ((g_drops & 0x3FFFF) < overflow) // ~ every 256 KB dropped
				gm_audio_log.warning("MiSTer audio backlog; dropped ~%llu bytes total (sender behind).", g_drops);
		}

		u32 wr = (g_head + g_count) % RING_CAP;
		const u32 first = std::min<u32>(bytes, RING_CAP - wr);
		std::memcpy(g_ring.data() + wr, src, first);
		if (bytes > first)
			std::memcpy(g_ring.data(), src + first, bytes - first);
		g_count += bytes;
	}

	void write(const f32* src, u32 frames, u32 src_ch)
	{
		if (!g_active.load(std::memory_order_acquire) || !src || frames == 0 || src_ch == 0)
			return;

		// Produce s16 LE stereo interleaved. cell_audio thread is the sole
		// producer, so thread_local scratch is safe and avoids per-call allocs.
		thread_local std::vector<f32> tmp_f;
		thread_local std::vector<s16> tmp_s16;

		const u32 out_samples = frames * 2; // stereo
		tmp_s16.resize(out_samples);

		if (src_ch == 2)
		{
			AudioBackend::convert_to_s16(out_samples, src, tmp_s16.data());
		}
		else if (src_ch == 1)
		{
			tmp_f.resize(out_samples);
			for (u32 i = 0; i < frames; i++)
				tmp_f[i * 2 + 0] = tmp_f[i * 2 + 1] = src[i];
			AudioBackend::convert_to_s16(out_samples, tmp_f.data(), tmp_s16.data());
		}
		else // src_ch > 2: downmix to stereo first
		{
			tmp_f.resize(out_samples);
			AudioBackend::downmix(frames * src_ch, src_ch, audio_channel_layout::stereo, src, tmp_f.data());
			AudioBackend::convert_to_s16(out_samples, tmp_f.data(), tmp_s16.data());
		}

		std::lock_guard lock(g_mtx);
		if (!g_active.load(std::memory_order_relaxed) || g_ring.size() != RING_CAP)
			return; // deactivated between the check and the lock
		ring_push(reinterpret_cast<const u8*>(tmp_s16.data()), out_samples * sizeof(s16));
	}

	u32 read(void* dst, u32 max_bytes)
	{
		std::lock_guard lock(g_mtx);
		if (g_count == 0 || max_bytes == 0 || g_ring.size() != RING_CAP)
			return 0;

		const u32 n = std::min<u32>(max_bytes, g_count);
		const u32 first = std::min<u32>(n, RING_CAP - g_head);
		std::memcpy(dst, g_ring.data() + g_head, first);
		if (n > first)
			std::memcpy(static_cast<u8*>(dst) + first, g_ring.data(), n - first);

		g_head = (g_head + n) % RING_CAP;
		g_count -= n;
		return n;
	}
}
