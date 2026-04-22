// AVX2 kernels for bink2. Implementations live in bink2_simd_avx2.cpp which
// is compiled with -mavx2 in isolation; the declarations here are safe to
// include from a plain SSE2 translation unit. Callers must CPUID-gate at the
// call site (via Bink2SimdRuntimeConfig::avx2_active) — the kernels themselves
// assume AVX2 is available and will crash with SIGILL on older CPUs otherwise.

#pragma once

#include <cstdint>

// Horizontal 6-tap luma MC (LumaMc16 mode 1) — byte-identical to the SSE2
// Bk2LumaRounded16-based path. Filters 16 pixels per row, 16 rows.
// msrc must point at (src + mv_x + mv_y*sstride) i.e. the origin of the
// 16x16 output in the reference; the kernel itself reads [-2, +3] horizontal
// neighbours.
void Bk2LumaMc16Mode1Avx2(uint8_t* dst, int stride,
                          const uint8_t* msrc, int sstride);
