// AVX2 kernels. Compiled as a separate TU with -mavx2 (see CMakeLists.txt) so
// the rest of bink2_core stays SSE2-baseline — older CPUs without AVX2 must
// still load this library and call only SSE2 paths. Every kernel here assumes
// the caller already CPUID-gated via Bink2SimdRuntimeConfig::avx2_active.

#include "bink2_simd_avx2.h"

#if defined(__AVX2__)
#include <immintrin.h>

// Byte-identical to the SSE2 Bk2LumaRounded16 path (two 8-wide halves) but
// with 16 int16 lanes in flight per row. The arithmetic is:
//   ab = p0 + p1;   cd = m1 + p2;   ef = m2 + p3;
//   sum = ((ab*19) >> 1) - (cd << 1) + (ef >> 1) + 8;
//   dst[i] = clamp(sum >> 4, 0, 255);
// All intermediates fit int16 for 8-bit inputs (max |sum| ~ 4845).
void Bk2LumaMc16Mode1Avx2(uint8_t* dst, int stride,
                          const uint8_t* msrc, int sstride)
{
    const __m256i c19 = _mm256_set1_epi16(19);
    const __m256i c8  = _mm256_set1_epi16(8);
    for (int j = 0; j < 16; ++j) {
        const __m256i m2 = _mm256_cvtepu8_epi16(
            _mm_loadu_si128((const __m128i*)(msrc - 2)));
        const __m256i m1 = _mm256_cvtepu8_epi16(
            _mm_loadu_si128((const __m128i*)(msrc - 1)));
        const __m256i p0 = _mm256_cvtepu8_epi16(
            _mm_loadu_si128((const __m128i*)(msrc + 0)));
        const __m256i p1 = _mm256_cvtepu8_epi16(
            _mm_loadu_si128((const __m128i*)(msrc + 1)));
        const __m256i p2 = _mm256_cvtepu8_epi16(
            _mm_loadu_si128((const __m128i*)(msrc + 2)));
        const __m256i p3 = _mm256_cvtepu8_epi16(
            _mm_loadu_si128((const __m128i*)(msrc + 3)));

        const __m256i ab = _mm256_add_epi16(p0, p1);
        const __m256i cd = _mm256_add_epi16(m1, p2);
        const __m256i ef = _mm256_add_epi16(m2, p3);

        const __m256i term1 = _mm256_srli_epi16(
            _mm256_mullo_epi16(ab, c19), 1);
        const __m256i term2 = _mm256_slli_epi16(cd, 1);
        const __m256i term3 = _mm256_srli_epi16(ef, 1);
        const __m256i sum = _mm256_add_epi16(
            _mm256_sub_epi16(term1, term2),
            _mm256_add_epi16(term3, c8));
        const __m256i shifted = _mm256_srai_epi16(sum, 4);

        // _mm256_packus_epi16 packs per-128-bit-lane (interleaved); since we
        // only have one 16-lane vector, extract halves and use SSE packus.
        const __m128i lo = _mm256_castsi256_si128(shifted);
        const __m128i hi = _mm256_extracti128_si256(shifted, 1);
        _mm_storeu_si128((__m128i*)dst, _mm_packus_epi16(lo, hi));

        dst += stride;
        msrc += sstride;
    }
}

#else  // !__AVX2__

// Stub — only reachable if CMake mis-compiles this TU without -mavx2. Caller
// should have CPUID-gated, so reaching this means a build-system bug; fail
// loudly rather than silently diverging.
#include <cstdio>
#include <cstdlib>
void Bk2LumaMc16Mode1Avx2(uint8_t*, int, const uint8_t*, int)
{
    std::fprintf(stderr, "bink2: AVX2 kernel called in a TU compiled without "
                         "-mavx2 — CMake misconfigured\n");
    std::abort();
}

#endif  // __AVX2__
