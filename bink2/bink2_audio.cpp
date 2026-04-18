/*
 * bink2_audio.cpp - Bink audio decode (DCT / RDFT) porting
 * FFmpeg's libavcodec/binkaudio.c.
 */

#include "bink2_audio.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

// WMA critical band frequencies (Hz). Bink audio uses the same boundaries
// for its band-limited quantisation. Source: FFmpeg `ff_wma_critical_freqs`.
constexpr uint16_t kWmaCriticalFreqs[25] = {
     100,   200,  300,  400,  510,  630,   770,   920,
    1080,  1270, 1480, 1720, 2000, 2320,  2700,  3150,
    3700,  4400, 5300, 6400, 7700, 9500, 12000, 15500,
   24500,
};

// Bink audio RLE length table (per-width run length in samples after the
// fixed 2-sample DC header, in chunks of 8).
constexpr uint8_t kRleLengthTab[16] = {
    2, 3, 4, 5, 6, 8, 9, 10, 11, 12, 13, 14, 15, 16, 32, 64,
};

constexpr float kPi = 3.14159265358979323846f;

// Reads FFmpeg's `get_float`: 5-bit exponent + 23-bit mantissa + sign.
bool ReadFloat(Bink2BitReader& bits, float& out)
{
    uint32_t power = 0;
    uint32_t mantissa = 0;
    uint32_t sign = 0;
    if (!bits.Read_Bits(5u, power)) return false;
    if (!bits.Read_Bits(23u, mantissa)) return false;
    if (!bits.Read_Bit(sign)) return false;
    float f = std::ldexp((float)mantissa, (int)power - 23);
    out = sign ? -f : f;
    return true;
}

// Reads a 32-bit little-endian float (version-b path).
bool ReadFloat32(Bink2BitReader& bits, float& out)
{
    uint32_t raw = 0;
    if (!bits.Read_Bits(32u, raw)) return false;
    float f;
    std::memcpy(&f, &raw, sizeof(f));
    out = f;
    return true;
}

// Naive O(N²) inverse DCT-II (aka DCT-III). Matches FFmpeg's
// `ff_dct_calc(DCT_III)`, which applies a 1/N normalisation internally
// on top of the textbook inverse DCT-II formula. Caller is expected to
// scale `coeffs[0] *= 2` beforehand (FFmpeg does `coeffs[0] /= 0.5`).
//   x[n] = (2/N) * (0.5 * X[0] + sum_{k=1}^{N-1} X[k] * cos(pi*(2n+1)*k/(2N)))
//
// Inner loop avoids std::cos() per iteration via a per-N cosine LUT
// of size 4N (values cos(pi*i / (2N)) for i=0..4N-1). The argument
// `(2n+1)*k` is reduced mod 4N to index this table — the cos function
// has period 2*pi = 4N units in our scale.
//
// This is still O(N²) compute; what changes is that each inner step is
// a load + FMA, not a transcendental. For frame_len=2048 the LUT is
// 32 KB — well within L1.
void DctIii(std::vector<float>& coeffs)
{
    const size_t N = coeffs.size();
    if (N == 0) return;

    static thread_local std::vector<float> cos_lut;
    static thread_local size_t cos_lut_n = 0;
    if (cos_lut_n != N) {
        const size_t M = 4u * N;
        cos_lut.assign(M, 0.f);
        const double step = (double)kPi / (2.0 * (double)N);
        for (size_t i = 0; i < M; ++i) {
            cos_lut[i] = (float)std::cos(step * (double)i);
        }
        cos_lut_n = N;
    }
    const size_t mask = 4u * N - 1u;   // 4N is a power of two for our N.

    std::vector<float> out(N, 0.f);
    const float base = 0.5f * coeffs[0];
    const float inv_n = 2.0f / (float)N;
    const float* lut = cos_lut.data();
    for (size_t n = 0; n < N; ++n) {
        const size_t a = 2u * n + 1u;     // (2n+1)
        size_t idx = a;                    // start at k=1: a*1
        float acc = base;
        for (size_t k = 1; k < N; ++k) {
            acc += coeffs[k] * lut[idx & mask];
            idx += a;
        }
        out[n] = acc * inv_n;
    }
    coeffs = std::move(out);
}

// Inverse real DFT (hermitian → real). Input layout matches FFmpeg's
// `RDFTContext(DFT_C2R)`: coeffs[0] = real(DC), coeffs[1] = real(nyquist),
// then interleaved real/imag pairs for bins 1..N/2-1.
void RdftC2R(std::vector<float>& coeffs)
{
    const size_t N = coeffs.size();
    if (N < 2) return;

    std::vector<float> out(N, 0.f);
    // Reassemble a full-length complex spectrum: X[0] = coeffs[0] + 0i,
    // X[N/2] = coeffs[1] + 0i, X[k] = coeffs[2k] + i*coeffs[2k+1] for
    // k=1..N/2-1, with hermitian conjugates on the other side.
    const size_t half = N / 2;
    std::vector<float> re(N, 0.f), im(N, 0.f);
    re[0] = coeffs[0];
    re[half] = coeffs[1];
    for (size_t k = 1; k < half; ++k) {
        re[k] = coeffs[2 * k];
        im[k] = coeffs[2 * k + 1];
        re[N - k] = re[k];
        im[N - k] = -im[k];
    }

    for (size_t n = 0; n < N; ++n) {
        float acc = 0.f;
        const float scale = 2.0f * kPi * (float)n / (float)N;
        for (size_t k = 0; k < N; ++k) {
            acc += re[k] * std::cos(scale * (float)k)
                 - im[k] * std::sin(scale * (float)k);
        }
        out[n] = acc;
    }
    coeffs = std::move(out);
}

} // namespace

bool Bink2AudioInit(Bink2AudioDecoder& d,
                    uint32_t sample_rate,
                    bool use_dct,
                    bool stereo,
                    bool version_b)
{
    d = {};
    if (sample_rate == 0) return false;
    d.kind         = use_dct ? Bink2AudioDecoder::Kind::DCT
                             : Bink2AudioDecoder::Kind::RDFT;
    d.sample_rate  = sample_rate;
    d.channels     = stereo ? 2u : 1u;
    d.version_b    = version_b;
    d.first_block  = true;

    uint32_t frame_len_bits = 0;
    if (sample_rate < 22050) frame_len_bits = 9;
    else if (sample_rate < 44100) frame_len_bits = 10;
    else frame_len_bits = 11;

    // For RDFT the audio is pre-interleaved on the bitstream side; the
    // transform runs on the interleaved 1-channel signal, which implies a
    // sample_rate * channels bitstream rate. This mirrors FFmpeg's path.
    uint32_t effective_rate = sample_rate;
    if (!use_dct) {
        if (stereo && !version_b) frame_len_bits += 1; // av_log2(2) = 1
        effective_rate = sample_rate * (stereo ? 2u : 1u);
        d.channels = 1u; // RDFT path processes interleaved as mono internally.
    }

    d.frame_len   = 1u << frame_len_bits;
    d.overlap_len = d.frame_len / 16u;
    d.block_size  = (d.frame_len - d.overlap_len) * d.channels;
    const uint32_t sample_rate_half = (effective_rate + 1u) / 2u;

    if (use_dct) {
        d.root = (float)d.frame_len /
                 (std::sqrt((float)d.frame_len) * 32768.0f);
    } else {
        d.root = 2.0f / (std::sqrt((float)d.frame_len) * 32768.0f);
    }
    for (int i = 0; i < 96; ++i) {
        d.quant_table[i] = std::exp((float)i * 0.15289164787221953823f) * d.root;
    }

    // Number of bands (WMA-style critical band mapping). Matches FFmpeg
    // exactly: loop 1..24 and break if `sample_rate_half <= freq[n-1]`;
    // otherwise num_bands ends at 25.
    d.num_bands = 1u;
    while (d.num_bands < 25u) {
        if (sample_rate_half <= (uint32_t)kWmaCriticalFreqs[d.num_bands - 1u]) break;
        ++d.num_bands;
    }

    d.bands.assign(d.num_bands + 1u, 0u);
    d.bands[0] = 2u;
    for (uint32_t i = 1u; i < d.num_bands; ++i) {
        d.bands[i] = ((uint32_t)kWmaCriticalFreqs[i - 1u] * d.frame_len /
                      sample_rate_half) & ~1u;
    }
    d.bands[d.num_bands] = d.frame_len;

    d.previous.assign(d.channels, std::vector<float>(d.overlap_len, 0.f));
    d.scratch_coeffs.assign(d.channels, std::vector<float>(d.frame_len, 0.f));
    d.ready = true;
    return true;
}

bool Bink2AudioDecodeBlock(Bink2AudioDecoder& d,
                           Bink2BitReader& bits,
                           std::vector<std::vector<float>>& out)
{
    if (!d.ready) return false;
    if (out.size() != d.channels) return false;
    for (auto& ch : out) {
        if (ch.size() != d.frame_len) return false;
    }

    const bool use_dct = (d.kind == Bink2AudioDecoder::Kind::DCT);
    if (use_dct) {
        uint32_t tag = 0;
        if (!bits.Read_Bits(2u, tag)) return false;
    }

    float quant[25] = {};

    for (uint32_t ch = 0; ch < d.channels; ++ch) {
        auto& coeffs = out[ch];

        // DC coefficients (2 samples).
        if (d.version_b) {
            if (!ReadFloat32(bits, coeffs[0])) return false;
            if (!ReadFloat32(bits, coeffs[1])) return false;
            coeffs[0] *= d.root;
            coeffs[1] *= d.root;
        } else {
            float f0, f1;
            if (!ReadFloat(bits, f0)) return false;
            if (!ReadFloat(bits, f1)) return false;
            coeffs[0] = f0 * d.root;
            coeffs[1] = f1 * d.root;
        }

        // Per-band quantiser indices (8 bits each).
        for (uint32_t i = 0; i < d.num_bands; ++i) {
            uint32_t v = 0;
            if (!bits.Read_Bits(8u, v)) return false;
            if (v > 95u) v = 95u;
            quant[i] = d.quant_table[v];
        }

        uint32_t k = 0;
        float q = quant[0];

        // Coefficients starting at index 2.
        uint32_t i = 2u;
        while (i < d.frame_len) {
            uint32_t j;
            if (d.version_b) {
                j = i + 16u;
            } else {
                uint32_t v = 0;
                if (!bits.Read_Bit(v)) return false;
                if (v) {
                    uint32_t w = 0;
                    if (!bits.Read_Bits(4u, w)) return false;
                    j = i + (uint32_t)kRleLengthTab[w] * 8u;
                } else {
                    j = i + 8u;
                }
            }
            j = std::min(j, d.frame_len);

            uint32_t width = 0;
            if (!bits.Read_Bits(4u, width)) return false;

            if (width == 0u) {
                for (uint32_t p = i; p < j; ++p) coeffs[p] = 0.f;
                i = j;
                while (k < d.num_bands && d.bands[k] < i) q = quant[k++];
            } else {
                while (i < j) {
                    if (k < d.num_bands && d.bands[k] == i) q = quant[k++];
                    uint32_t coeff = 0;
                    if (!bits.Read_Bits(width, coeff)) return false;
                    if (coeff) {
                        uint32_t sign = 0;
                        if (!bits.Read_Bit(sign)) return false;
                        coeffs[i] = sign ? (-q * (float)coeff)
                                         : ( q * (float)coeff);
                    } else {
                        coeffs[i] = 0.f;
                    }
                    ++i;
                }
            }
        }

        if (use_dct) {
            coeffs[0] *= 2.0f;   // FFmpeg: `coeffs[0] /= 0.5`.
            DctIii(coeffs);
        } else {
            RdftC2R(coeffs);
        }
    }

    // Overlap-add 1/16 of the previous block.
    const uint32_t count = d.overlap_len * d.channels;
    for (uint32_t ch = 0; ch < d.channels; ++ch) {
        if (!d.first_block) {
            uint32_t j = ch;
            for (uint32_t i = 0; i < d.overlap_len; ++i, j += d.channels) {
                out[ch][i] = (d.previous[ch][i] * (count - j) +
                              out[ch][i] * j) / (float)count;
            }
        }
        // Cache the overlap tail (last overlap_len samples) for the next
        // block. FFmpeg reads from `frame_len - overlap_len`.
        std::memcpy(d.previous[ch].data(),
                    out[ch].data() + (d.frame_len - d.overlap_len),
                    d.overlap_len * sizeof(float));
    }

    d.first_block = false;
    return true;
}

bool Bink2AudioDecodePacket(Bink2AudioDecoder& d,
                            const std::vector<uint8_t>& packet,
                            std::vector<std::vector<float>>& out)
{
    if (!d.ready) return false;
    if (packet.size() < 4) return false;

    Bink2BitReader bits;
    if (!bits.Reset(packet.data(), packet.size())) return false;

    // Skip the leading reported-size u32 that FFmpeg discards.
    if (!bits.Skip_Bits(32u)) return false;

    if (out.size() != d.channels) out.assign(d.channels, {});
    std::vector<std::vector<float>> block(d.channels,
                                          std::vector<float>(d.frame_len, 0.f));

    // A rough lower bound on bits needed for one block. Anything below
    // this is trailing padding: FFmpeg's receive_frame would refuse to
    // even call decode_block on it.
    const size_t min_bits = 2u + 58u + 8u * d.num_bands + 32u;
    size_t blocks_emitted = 0;
    while (bits.Bits_Left() >= min_bits) {
        if (!Bink2AudioDecodeBlock(d, bits, block)) {
            // Tail-truncation: a block that started inside the packet but
            // ran out of bits is treated as end-of-stream (FFmpeg would
            // normally re-enter with the next packet; we have none here).
            if (blocks_emitted > 0) return true;
            return false;
        }
        // Emit the first (frame_len - overlap_len) samples per channel.
        const uint32_t emit = d.frame_len - d.overlap_len;
        for (uint32_t ch = 0; ch < d.channels; ++ch) {
            out[ch].insert(out[ch].end(),
                           block[ch].begin(),
                           block[ch].begin() + emit);
        }
        ++blocks_emitted;
        // FFmpeg aligns each block to a 32-bit boundary.
        const size_t rem = bits.Bits_Read() % 32u;
        if (rem != 0u) {
            if (!bits.Skip_Bits(32u - rem)) break;
        }
    }

    return true;
}
