#pragma once

#include "util/types.hpp"

namespace groovy_mister
{
	// Largest blit the protocol can carry, in bytes. This is not a preference:
	// it is the size the vendored client allocates for its blit buffers
	// (BUFFER_SIZE in 3rdparty/groovy_mister/groovymister.h, 720x576x3), and we
	// memcpy into that buffer. Exceeding it is a heap overrun, so it is enforced
	// unconditionally and is NOT covered by the CRT-safety option.
	constexpr u32 max_blit_bytes = 1245312;

	// CRT-shape guidance: the envelope a 15/31 kHz CRT driven by this core is
	// expected to stay inside. Unlike the byte cap above, this is advice — the
	// user can turn it off (g_cfg.groovy_mister.crt_safety_limits) to allow
	// unusual modes through with a warning.
	constexpr u16 max_safe_v_active = 576;
	constexpr u16 max_safe_h_active = 1024;

	struct modeline
	{
		double pclock;   // MHz
		u16 h_active, h_begin, h_end, h_total;
		u16 v_active, v_begin, v_end, v_total;
		u8 interlace;    // 0 progressive, 1 interlaced field, 2 progressive framebuffer over interlaced signal
	};

	// Structural sanity. A modeline failing this is malformed, not merely
	// unusual, so it is refused whatever the CRT-safety setting says.
	constexpr bool is_well_formed(const modeline& m)
	{
		return m.v_active > 0
			&& m.h_active > 0
			&& m.h_total > m.h_active
			&& m.v_total > m.v_active;
	}

	// Does one blit of this mode fit the client's buffer at `bpp` bytes/pixel?
	// Hard limit — see max_blit_bytes.
	constexpr bool fits_blit_buffer(const modeline& m, u32 bpp)
	{
		return static_cast<u64>(m.h_active) * m.v_active * bpp <= max_blit_bytes;
	}

	// The soft CRT-shape envelope. Only consulted when the user leaves CRT
	// safety limits enabled.
	constexpr bool is_crt_shape_safe(const modeline& m)
	{
		return m.v_active <= max_safe_v_active
			&& m.h_active <= max_safe_h_active;
	}
}
