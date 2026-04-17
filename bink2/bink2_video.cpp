/*
 * bink2_video.cpp - Early packet-to-frame planning helpers for BK2 video decode.
 */

#include "bink2_video.h"

#include "bink2_bitstream.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace {

static const uint8_t kBink2gChromaCbpPat[16] = {
    0x00, 0x00, 0x00, 0x0F,
    0x00, 0x0F, 0x0F, 0x0F,
    0x00, 0x0F, 0x0F, 0x0F,
    0x0F, 0x0F, 0x0F, 0x0F,
};

static const int32_t kBink2gDcPat[] = {
    1024, 1218, 1448, 1722, 2048,
    2435, 2896, 3444, 4096, 4871,
    5793, 6889, 8192, 9742, 11585, 13777, 16384,
    19484, 23170, 27555, 32768, 38968, 46341,
    55109, 65536, 77936, 92682, 110218, 131072,
    155872, 185364, 220436, 262144, 311744,
    370728, 440872, 524288,
};

static const uint8_t kLumaRepos[16] = {
    0, 1, 4, 5, 2, 3, 6, 7,
    8, 9, 12, 13, 10, 11, 14, 15,
};

static const uint8_t kBink2gScan[64] = {
     0,  8,  1,  2,  9, 16, 24, 17,
    10,  3,  4, 11, 18, 25, 32, 40,
    33, 26, 19, 12,  5,  6, 13, 20,
    27, 34, 41, 48, 56, 49, 42, 35,
    28, 21, 14,  7, 15, 22, 29, 36,
    43, 50, 57, 58, 51, 44, 37, 30,
    23, 31, 38, 45, 52, 59, 60, 53,
    46, 39, 47, 54, 61, 62, 55, 63,
};

static const uint16_t kBink2gAcSkipCodes[2][14] = {
    { 0x0001, 0x0000, 0x0004, 0x002c, 0x006c, 0x000c, 0x004c,
      0x00ac, 0x00ec, 0x012c, 0x016c, 0x01ac, 0x0002, 0x001c },
    { 0x0001, 0x0004, 0x0000, 0x0008, 0x0002, 0x0032, 0x000a,
      0x0012, 0x003a, 0x007a, 0x00fa, 0x0072, 0x0006, 0x001a },
};

static const uint8_t kBink2gAcSkipBits[2][14] = {
    { 1, 3, 4, 9, 9, 7, 7, 9, 8, 9, 9, 9, 2, 5 },
    { 1, 3, 4, 4, 5, 7, 5, 6, 7, 8, 8, 7, 3, 6 },
};

static const uint8_t kBink2NextSkips[16] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7, 0, 0,
};

static const uint8_t kBink2gSkips[16] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 64, 0, 0, 0,
};

static const uint16_t kBink2gLumaIntraQmat[4][64] = {
    {
         1024,  1432,  1506,  1181,  1843,  2025,  5271,  8592,
         1313,  1669,  1630,  1672,  2625,  3442,  8023, 12794,
         1076,  1755,  1808,  1950,  3980,  4875,  8813, 11909,
         1350,  1868,  2127,  2016,  4725,  4450,  7712,  9637,
         2458,  3103,  4303,  4303,  6963,  6835, 11079, 13365,
         3375,  5704,  5052,  6049,  9198,  7232, 10725,  9834,
         5486,  7521,  7797,  7091, 11079, 10016, 13559, 12912,
         7279,  7649,  7020,  6097,  9189,  9047, 12661, 13768
    }, {
         1218,  1703,  1791,  1405,  2192,  2408,  6268, 10218,
         1561,  1985,  1938,  1988,  3122,  4093,  9541, 15215,
         1279,  2087,  2150,  2319,  4733,  5798, 10481, 14162,
         1606,  2222,  2530,  2398,  5619,  5292,  9171, 11460,
         2923,  3690,  5117,  5118,  8281,  8128, 13176, 15894,
         4014,  6783,  6008,  7194, 10938,  8600, 12755, 11694,
         6524,  8944,  9272,  8433, 13176, 11911, 16125, 15354,
         8657,  9096,  8348,  7250, 10927, 10759, 15056, 16373
    }, {
         1448,  2025,  2130,  1671,  2607,  2864,  7454, 12151,
         1856,  2360,  2305,  2364,  3713,  4867, 11346, 18094,
         1521,  2482,  2557,  2758,  5628,  6894, 12464, 16841,
         1909,  2642,  3008,  2852,  6683,  6293, 10906, 13629,
         3476,  4388,  6085,  6086,  9847,  9666, 15668, 18901,
         4773,  8066,  7145,  8555, 13007, 10227, 15168, 13907,
         7758, 10637, 11026, 10028, 15668, 14165, 19175, 18259,
        10294, 10817,  9927,  8622, 12995, 12794, 17905, 19470
    }, {
         1722,  2408,  2533,  1987,  3100,  3406,  8864, 14450,
         2208,  2807,  2741,  2811,  4415,  5788, 13493, 21517,
         1809,  2951,  3041,  3280,  6693,  8199, 14822, 20028,
         2271,  3142,  3578,  3391,  7947,  7484, 12969, 16207,
         4133,  5218,  7236,  7238, 11711, 11495, 18633, 22478,
         5677,  9592,  8497, 10174, 15469, 12162, 18038, 16538,
         9226, 12649, 13112, 11926, 18633, 16845, 22804, 21715,
        12242, 12864, 11806, 10254, 15454, 15215, 21293, 23155
    }
};

static const uint16_t kBink2gChromaIntraQmat[4][64] = {
    {
         1024,  1193,  1434,  2203,  5632,  4641,  5916,  6563,
         1193,  1622,  1811,  3606,  6563,  5408,  6894,  7649,
         1434,  1811,  3515,  4875,  5916,  4875,  6215,  6894,
         2203,  3606,  4875,  3824,  4641,  3824,  4875,  5408,
         5632,  6563,  5916,  4641,  5632,  4641,  5916,  6563,
         4641,  5408,  4875,  3824,  4641,  3824,  4875,  5408,
         5916,  6894,  6215,  4875,  5916,  4875,  6215,  6894,
         6563,  7649,  6894,  5408,  6563,  5408,  6894,  7649
    }, {
         1218,  1419,  1706,  2620,  6698,  5519,  7035,  7805,
         1419,  1929,  2153,  4288,  7805,  6432,  8199,  9096,
         1706,  2153,  4180,  5798,  7035,  5798,  7390,  8199,
         2620,  4288,  5798,  4548,  5519,  4548,  5798,  6432,
         6698,  7805,  7035,  5519,  6698,  5519,  7035,  7805,
         5519,  6432,  5798,  4548,  5519,  4548,  5798,  6432,
         7035,  8199,  7390,  5798,  7035,  5798,  7390,  8199,
         7805,  9096,  8199,  6432,  7805,  6432,  8199,  9096
    }, {
         1448,  1688,  2028,  3116,  7965,  6563,  8367,  9282,
         1688,  2294,  2561,  5099,  9282,  7649,  9750, 10817,
         2028,  2561,  4971,  6894,  8367,  6894,  8789,  9750,
         3116,  5099,  6894,  5408,  6563,  5408,  6894,  7649,
         7965,  9282,  8367,  6563,  7965,  6563,  8367,  9282,
         6563,  7649,  6894,  5408,  6563,  5408,  6894,  7649,
         8367,  9750,  8789,  6894,  8367,  6894,  8789,  9750,
         9282, 10817,  9750,  7649,  9282,  7649,  9750, 10817
    }, {
         1722,  2007,  2412,  3706,  9472,  7805,  9950, 11038,
         2007,  2729,  3045,  6064, 11038,  9096, 11595, 12864,
         2412,  3045,  5912,  8199,  9950,  8199, 10452, 11595,
         3706,  6064,  8199,  6432,  7805,  6432,  8199,  9096,
         9472, 11038,  9950,  7805,  9472,  7805,  9950, 11038,
         7805,  9096,  8199,  6432,  7805,  6432,  8199,  9096,
         9950, 11595, 10452,  8199,  9950,  8199, 10452, 11595,
        11038, 12864, 11595,  9096, 11038,  9096, 11595, 12864
    }
};

static const int32_t kBink2MaxQuant = 37;
static const uint32_t kBink2MacroblockPixels = 32u;

uint32_t Align16(uint32_t value)
{
    return (value + 15u) & ~15u;
}

uint32_t Align32(uint32_t value)
{
    return (value + 31u) & ~31u;
}

int32_t ClipInt(int32_t value, int32_t min_value, int32_t max_value)
{
    return std::max(min_value, std::min(max_value, value));
}

int32_t MidPred(int32_t a, int32_t b, int32_t c)
{
    return a + b + c - std::max(a, std::max(b, c)) - std::min(a, std::min(b, c));
}

int32_t DCMpred2(int32_t a, int32_t b)
{
    return (a + b + 1) >> 1;
}

void Bink2gIdct1d(int16_t *blk, int step, int shift)
{
#define idct_mul_a(val) ((val) + ((val) >> 2))
#define idct_mul_b(val) ((val) >> 1)
#define idct_mul_c(val) ((val) - ((val) >> 2) - ((val) >> 4))
#define idct_mul_d(val) ((val) + ((val) >> 2) - ((val) >> 4))
#define idct_mul_e(val) ((val) >> 2)
    int tmp00 =  blk[3*step] + blk[5*step];
    int tmp01 =  blk[3*step] - blk[5*step];
    int tmp02 =  idct_mul_a(blk[2*step]) + idct_mul_b(blk[6*step]);
    int tmp03 =  idct_mul_b(blk[2*step]) - idct_mul_a(blk[6*step]);
    int tmp0  = (blk[0*step] + blk[4*step]) + tmp02;
    int tmp1  = (blk[0*step] + blk[4*step]) - tmp02;
    int tmp2  =  blk[0*step] - blk[4*step];
    int tmp3  =  blk[1*step] + tmp00;
    int tmp4  =  blk[1*step] - tmp00;
    int tmp5  =  tmp01 + blk[7*step];
    int tmp6  =  tmp01 - blk[7*step];
    int tmp7  =  tmp4 + idct_mul_c(tmp6);
    int tmp8  =  idct_mul_c(tmp4) - tmp6;
    int tmp9  =  idct_mul_d(tmp3) + idct_mul_e(tmp5);
    int tmp10 =  idct_mul_e(tmp3) - idct_mul_d(tmp5);
    int tmp11 =  tmp2 + tmp03;
    int tmp12 =  tmp2 - tmp03;

    blk[0*step] = (int16_t)((tmp0  + tmp9)  >> shift);
    blk[1*step] = (int16_t)((tmp11 + tmp7)  >> shift);
    blk[2*step] = (int16_t)((tmp12 + tmp8)  >> shift);
    blk[3*step] = (int16_t)((tmp1  + tmp10) >> shift);
    blk[4*step] = (int16_t)((tmp1  - tmp10) >> shift);
    blk[5*step] = (int16_t)((tmp12 - tmp8)  >> shift);
    blk[6*step] = (int16_t)((tmp11 - tmp7)  >> shift);
    blk[7*step] = (int16_t)((tmp0  - tmp9)  >> shift);
#undef idct_mul_a
#undef idct_mul_b
#undef idct_mul_c
#undef idct_mul_d
#undef idct_mul_e
}

void Bink2gIdctPut(uint8_t *dst, int stride, int16_t *block)
{
    for (int i = 0; i < 8; ++i)
        Bink2gIdct1d(block + i, 8, 0);
    for (int i = 0; i < 8; ++i)
        Bink2gIdct1d(block + i * 8, 1, 6);
    for (int i = 0; i < 8; ++i) {
        for (int j = 0; j < 8; ++j)
            dst[j] = (uint8_t)ClipInt(block[j * 8 + i], 0, 255);
        dst += stride;
    }
}

void PutDcOnlyBlock(uint8_t *dst, int stride, int32_t dc)
{
    int16_t block[64] = {};
    block[0] = (int16_t)(dc * 8 + 32);
    Bink2gIdctPut(dst, stride, block);
}

template <size_t N>
void FillPlaneWithBlocks(std::array<uint8_t, N>& plane,
                         int stride,
                         const int32_t* dc,
                         size_t dc_count,
                         const uint8_t* repos,
                         bool use_repos)
{
    for (size_t i = 0; i < dc_count; ++i) {
        uint8_t index = use_repos ? repos[i] : (uint8_t)i;
        int block_x = (index & (use_repos ? 3 : 1)) * 8;
        int block_y = (index >> (use_repos ? 2 : 1)) * 8;
        PutDcOnlyBlock(plane.data() + block_y * stride + block_x, stride, dc[i]);
    }
}

bool DecodeVlcLittleEndian(Bink2BitReader& bits,
                           const uint16_t* codes,
                           const uint8_t* lengths,
                           size_t code_count,
                           uint32_t& symbol)
{
    uint32_t value = 0;
    for (uint32_t bit_count = 1; bit_count <= 16u; ++bit_count) {
        uint32_t bit = 0;
        if (!bits.Read_Bit(bit)) return false;
        value |= bit << (bit_count - 1u);

        for (size_t i = 0; i < code_count; ++i) {
            if (lengths[i] == bit_count && codes[i] == value) {
                symbol = (uint32_t)i;
                return true;
            }
        }
    }

    return false;
}

bool DecodeAcBlocks(Bink2BitReader& bits,
                    uint32_t cbp,
                    int32_t q,
                    const uint16_t qmat[4][64],
                    std::array<std::array<int16_t, 64>, 4>& blocks)
{
    for (size_t i = 0; i < blocks.size(); ++i) {
        blocks[i].fill(0);
    }

    if (q < 0 || q >= kBink2MaxQuant) return false;

    const uint16_t* skip_codes = (cbp & 0xffff0000u) ? kBink2gAcSkipCodes[1]
                                                     : kBink2gAcSkipCodes[0];
    const uint8_t* skip_bits = (cbp & 0xffff0000u) ? kBink2gAcSkipBits[1]
                                                   : kBink2gAcSkipBits[0];
    const int32_t q_scale = 1 << (q >> 2);

    for (size_t block_index = 0; block_index < blocks.size(); ++block_index, cbp >>= 1u) {
        if ((cbp & 1u) == 0) continue;

        int32_t next = 0;
        int32_t idx = 1;

        while (idx < 64) {
            --next;
            if (next < 1) {
                uint32_t symbol = 0;
                if (!DecodeVlcLittleEndian(bits, skip_codes, skip_bits, 14u, symbol)) {
                    return false;
                }

                next = (int32_t)kBink2NextSkips[symbol];
                int32_t skip = (int32_t)kBink2gSkips[symbol];
                if (skip == 11) {
                    uint32_t extra = 0;
                    if (!bits.Read_Bits(6u, extra)) return false;
                    skip = (int32_t)extra;
                }

                idx += skip;
                if (idx >= 64) break;
            }

            uint32_t unary = 0;
            if (!bits.Read_Unary(0u, 12u, unary)) return false;

            int32_t coeff = (int32_t)unary + 1;
            if (coeff > 3) {
                uint32_t extra = 0;
                if (!bits.Read_Bits((uint32_t)coeff - 3u, extra)) return false;
                coeff = (1 << (coeff - 3)) + (int32_t)extra + 2;
            }

            uint32_t sign = 0;
            if (!bits.Read_Bit(sign)) return false;
            if (sign != 0) coeff = -coeff;

            const uint8_t scan_idx = kBink2gScan[idx];
            const int64_t dequant =
                ((int64_t)coeff * (int64_t)qmat[q & 3][scan_idx] * (int64_t)q_scale + 64) >> 7;
            blocks[block_index][scan_idx] =
                (int16_t)ClipInt((int32_t)dequant, -32768, 32767);
            ++idx;
        }
    }

    return true;
}

template <size_t PlaneSize, size_t BlockCount, size_t DcCount>
void ReconstructPlaneBlocks(std::array<uint8_t, PlaneSize>& plane,
                            int stride,
                            std::array<std::array<int16_t, 64>, BlockCount>& blocks,
                            const std::array<int32_t, DcCount>& dc,
                            const uint8_t* repos,
                            bool use_repos)
{
    for (size_t i = 0; i < BlockCount; ++i) {
        blocks[i][0] = (int16_t)ClipInt(dc[i] * 8 + 32, -32768, 32767);

        uint8_t index = use_repos ? repos[i] : (uint8_t)i;
        int block_x = (index & (use_repos ? 3 : 1)) * 8;
        int block_y = (index >> (use_repos ? 2 : 1)) * 8;

        std::array<int16_t, 64> block = blocks[i];
        Bink2gIdctPut(plane.data() + block_y * stride + block_x, stride, block.data());
    }
}

bool DecodeAndReconstructMacroblock(Bink2BitReader& bits,
                                    const Bink2IntraMacroblockProbe& probe,
                                    Bink2DecodedMacroblock& macroblock)
{
    macroblock = {};
    macroblock.probe = probe;
    macroblock.pixels.has_alpha = probe.has_alpha;

    if (probe.intra_q < 0 || probe.intra_q >= kBink2MaxQuant) return false;

    std::array<std::array<int16_t, 64>, 4> blocks = {};

    for (size_t group = 0; group < 4; ++group) {
        if (!DecodeAcBlocks(bits, probe.y_cbp >> (group * 4u), probe.intra_q,
                            kBink2gLumaIntraQmat, blocks)) {
            return false;
        }
        for (size_t i = 0; i < blocks.size(); ++i) {
            macroblock.y_blocks[group * 4u + i] = blocks[i];
        }
    }

    if (!DecodeAcBlocks(bits, probe.u_cbp, probe.intra_q, kBink2gChromaIntraQmat, blocks)) {
        return false;
    }
    for (size_t i = 0; i < blocks.size(); ++i) {
        macroblock.u_blocks[i] = blocks[i];
    }

    if (!DecodeAcBlocks(bits, probe.v_cbp, probe.intra_q, kBink2gChromaIntraQmat, blocks)) {
        return false;
    }
    for (size_t i = 0; i < blocks.size(); ++i) {
        macroblock.v_blocks[i] = blocks[i];
    }

    if (probe.has_alpha) {
        for (size_t group = 0; group < 4; ++group) {
            if (!DecodeAcBlocks(bits, probe.a_cbp >> (group * 4u), probe.intra_q,
                                kBink2gLumaIntraQmat, blocks)) {
                return false;
            }
            for (size_t i = 0; i < blocks.size(); ++i) {
                macroblock.a_blocks[group * 4u + i] = blocks[i];
            }
        }
    }

    ReconstructPlaneBlocks(macroblock.pixels.y, 32, macroblock.y_blocks, probe.y_dc,
                           kLumaRepos, true);
    ReconstructPlaneBlocks(macroblock.pixels.u, 16, macroblock.u_blocks, probe.u_dc,
                           nullptr, false);
    ReconstructPlaneBlocks(macroblock.pixels.v, 16, macroblock.v_blocks, probe.v_dc,
                           nullptr, false);

    if (probe.has_alpha) {
        ReconstructPlaneBlocks(macroblock.pixels.a, 32, macroblock.a_blocks, probe.a_dc,
                               kLumaRepos, true);
    }

    macroblock.probe.bit_offset_after_coeffs = bits.Bits_Read();
    return true;
}

void BlitPlaneBlock(uint8_t* dst,
                    uint32_t dst_stride,
                    const uint8_t* src,
                    uint32_t src_stride,
                    uint32_t width,
                    uint32_t height)
{
    for (uint32_t row = 0; row < height; ++row) {
        std::memcpy(dst + row * dst_stride, src + row * src_stride, width);
    }
}

bool BlitPlaneRegion(uint8_t* dst,
                     uint32_t dst_stride,
                     uint32_t dst_width,
                     uint32_t dst_height,
                     uint32_t dst_x,
                     uint32_t dst_y,
                     const uint8_t* src,
                     uint32_t src_stride,
                     uint32_t src_width,
                     uint32_t src_height)
{
    if (!dst || !src) return false;
    if (dst_x > dst_width || dst_y > dst_height) return false;
    if (src_width == 0 || src_height == 0) return true;
    if (dst_x + src_width > dst_width || dst_y + src_height > dst_height) return false;

    for (uint32_t row = 0; row < src_height; ++row) {
        std::memcpy(dst + (dst_y + row) * dst_stride + dst_x,
                    src + row * src_stride,
                    src_width);
    }

    return true;
}

bool BlitMacroblockIntoSlice(const Bink2DecodedMacroblock& macroblock,
                             uint32_t mb_col,
                             uint32_t mb_row,
                             Bink2DecodedKeyframeSlice& slice)
{
    const uint32_t y_x = mb_col * 32u;
    const uint32_t y_y = mb_row * 32u;
    const uint32_t uv_x = mb_col * 16u;
    const uint32_t uv_y = mb_row * 16u;

    if (slice.y.empty() || slice.u.empty() || slice.v.empty()) return false;
    if (y_x + 32u > slice.luma_width || y_y + 32u > slice.luma_height) return false;
    if (uv_x + 16u > slice.chroma_width || uv_y + 16u > slice.chroma_height) return false;
    if (macroblock.pixels.has_alpha && slice.a.empty()) return false;

    BlitPlaneBlock(slice.y.data() + y_y * slice.luma_stride + y_x,
                   slice.luma_stride,
                   macroblock.pixels.y.data(),
                   32u,
                   32u,
                   32u);
    BlitPlaneBlock(slice.u.data() + uv_y * slice.chroma_stride + uv_x,
                   slice.chroma_stride,
                   macroblock.pixels.u.data(),
                   16u,
                   16u,
                   16u);
    BlitPlaneBlock(slice.v.data() + uv_y * slice.chroma_stride + uv_x,
                   slice.chroma_stride,
                   macroblock.pixels.v.data(),
                   16u,
                   16u,
                   16u);

    if (macroblock.pixels.has_alpha) {
        BlitPlaneBlock(slice.a.data() + y_y * slice.luma_stride + y_x,
                       slice.luma_stride,
                       macroblock.pixels.a.data(),
                       32u,
                       32u,
                       32u);
    }

    return true;
}

bool FlushPackedByte(uint32_t& accumulator,
                     uint32_t& bit_offset,
                     std::vector<uint8_t>& dst,
                     size_t& out_index)
{
    if (bit_offset < 8u) return true;
    if (out_index >= dst.size()) return false;
    dst[out_index++] = (uint8_t)(accumulator & 0xffu);
    accumulator >>= 8;
    bit_offset -= 8u;
    return true;
}

bool DecodeFlagStream(Bink2BitReader& bits,
                      uint32_t initial_bit_offset,
                      uint32_t flag_count,
                      std::vector<uint8_t>& dst)
{
    dst.assign((flag_count + 7u) >> 3, 0);
    if (flag_count == 0) return true;

    uint32_t raw_mode = 0;
    if (!bits.Read_Bit(raw_mode)) return false;
    if (raw_mode == 0) {
        uint32_t value = 0;
        size_t byte_index = 0;

        for (uint32_t i = 0; i < flag_count / 8u; ++i) {
            if (!bits.Read_Bits(8u, value)) return false;
            dst[byte_index++] = (uint8_t)value;
        }

        uint32_t leftover = flag_count & 7u;
        if (leftover != 0) {
            if (!bits.Read_Bits(leftover, value)) return false;
            dst[byte_index] = (uint8_t)value;
        }
        return true;
    }

    uint32_t accumulator = 0;
    uint32_t bit_offset = initial_bit_offset;
    uint32_t flags_left = flag_count;
    uint32_t mode = 0;
    uint32_t flag = 0;
    size_t out_index = 0;

    while (flags_left > 0) {
        uint32_t cache = bit_offset;
        uint32_t branch = 0;
        if (!bits.Read_Bit(branch)) return false;

        if (branch == 0) {
            if (mode == 3u) {
                flag ^= 1u;
            } else {
                if (!bits.Read_Bit(flag)) return false;
            }
            mode = 2u;

            uint32_t tail_bits = 0;
            if (flags_left < 5u) {
                uint32_t literal_bits = flags_left > 0 ? (flags_left - 1u) : 0u;
                if (literal_bits != 0 && !bits.Read_Bits(literal_bits, tail_bits)) return false;
                tail_bits <<= ((bit_offset + 1u) & 31u);
                bit_offset += flags_left;
                flags_left = 0;
            } else {
                if (!bits.Read_Bits(4u, tail_bits)) return false;
                tail_bits <<= ((bit_offset + 1u) & 31u);
                bit_offset += 5u;
                flags_left -= 5u;
            }

            accumulator |= (flag << (cache & 31u)) | tail_bits;
            if (!FlushPackedByte(accumulator, bit_offset, dst, out_index)) return false;
        } else {
            uint32_t bits_for_run = (flags_left < 4u) ? 2u : (flags_left < 16u ? 4u : 5u);
            uint32_t coded = bits_for_run + 1u;

            if (mode == 3u) {
                flag ^= 1u;
            } else {
                ++coded;
                if (!bits.Read_Bit(flag)) return false;
            }

            coded = std::min(coded, flags_left);
            flags_left -= coded;

            if (flags_left > 0) {
                uint32_t extra = 0;
                if (!bits.Read_Bits(bits_for_run, extra)) return false;
                if (extra > flags_left) return false;
                flags_left -= extra;
                coded += extra;
                mode = (extra == ((1u << bits_for_run) - 1u)) ? 1u : 3u;
            }

            uint32_t fill = flag ? 0xffu : 0u;
            while (coded > 8u) {
                accumulator |= fill << (cache & 31u);
                if (out_index >= dst.size()) return false;
                dst[out_index++] = (uint8_t)(accumulator & 0xffu);
                accumulator >>= 8;
                coded -= 8u;
            }

            if (coded > 0u) {
                bit_offset += coded;
                uint32_t mask = (coded == 32u) ? 0xffffffffu : ((1u << coded) - 1u);
                accumulator |= (mask & fill) << (cache & 31u);
                if (!FlushPackedByte(accumulator, bit_offset, dst, out_index)) return false;
            }
        }
    }

    if (bit_offset != 0u) {
        if (out_index >= dst.size()) return false;
        dst[out_index] = (uint8_t)(accumulator & 0xffu);
    }

    return true;
}

bool DecodeDQ(Bink2BitReader& bits, int32_t& dq)
{
    uint32_t value = 0;
    if (!bits.Read_Unary(1u, 4u, value)) return false;

    int32_t signed_value = (int32_t)value;
    if (signed_value == 3) {
        uint32_t extra = 0;
        if (!bits.Read_Bit(extra)) return false;
        signed_value += (int32_t)extra;
    } else if (signed_value == 4) {
        uint32_t extra = 0;
        if (!bits.Read_Bits(5u, extra)) return false;
        signed_value += (int32_t)extra + 1;
    }

    if (signed_value != 0) {
        uint32_t sign = 0;
        if (!bits.Read_Bit(sign)) return false;
        if (sign != 0) signed_value = -signed_value;
    }

    dq = signed_value;
    return true;
}

bool DecodeCbpLuma(Bink2BitReader& bits,
                   uint32_t frame_flags,
                   uint32_t prev_cbp,
                   uint32_t& cbp)
{
    uint32_t ones = 0;
    for (uint32_t i = 0; i < 16u; ++i) {
        if ((prev_cbp & (1u << i)) != 0) ++ones;
    }

    uint32_t mask = 0;
    cbp = 0;
    if (ones > 7u) {
        ones = 16u - ones;
        mask = 0xffffu;
    }

    uint32_t bit = 0;
    if (!bits.Read_Bit(bit)) return false;
    if (bit == 0) {
        if (ones < 4u) {
            for (uint32_t j = 0; j < 16u; j += 4u) {
                if (!bits.Read_Bit(bit)) return false;
                if (bit == 0) {
                    uint32_t nibble = 0;
                    if (!bits.Read_Bits(4u, nibble)) return false;
                    cbp |= nibble << j;
                }
            }
        } else {
            if (!bits.Read_Bits(16u, cbp)) return false;
        }
    }

    cbp ^= mask;
    if ((frame_flags & 0x00040000u) == 0 || cbp != 0) {
        if (!bits.Read_Bit(bit)) return false;
        if (bit != 0) cbp |= cbp << 16;
    }

    return true;
}

bool DecodeCbpChroma(Bink2BitReader& bits,
                     uint32_t prev_cbp,
                     uint32_t& cbp)
{
    cbp = (prev_cbp & 0xf0000u) | kBink2gChromaCbpPat[prev_cbp & 0xfu];

    uint32_t bit = 0;
    if (!bits.Read_Bit(bit)) return false;
    if (bit == 0) {
        if (!bits.Read_Bits(4u, cbp)) return false;
        if (!bits.Read_Bit(bit)) return false;
        if (bit != 0) cbp |= cbp << 16;
    }

    return true;
}

int32_t PredictIntraQ(int8_t left_q, int8_t top_q, int8_t top_left_q, int32_t dq, uint32_t flags)
{
    if ((flags & 0x20u) && (flags & 0x80u))
        return 16 + dq;
    if (flags & 0x80u)
        return left_q + dq;
    if (flags & 0x20u)
        return top_q + dq;
    return MidPred(top_q, left_q, top_left_q) + dq;
}

template <size_t N>
bool DecodeDcValues(Bink2BitReader& bits,
                    int32_t q,
                    bool is_luma,
                    int32_t min_dc,
                    int32_t max_dc,
                    uint32_t flags,
                    const std::array<int32_t, N>& left_dc,
                    const std::array<int32_t, N>& top_dc,
                    const std::array<int32_t, N>& top_left_dc,
                    std::array<int32_t, N>& dst)
{
    dst.fill(0);

    q = std::max(q, 8);
    if ((size_t)q >= (sizeof(kBink2gDcPat) / sizeof(kBink2gDcPat[0]))) return false;

    const int32_t pat = kBink2gDcPat[q];
    const size_t count = is_luma ? 16u : 4u;
    if ((is_luma && N != 16u) || (!is_luma && N != 4u)) return false;
    std::array<int32_t, 16> tdc = {};

    uint32_t has_delta = 0;
    if (!bits.Read_Bit(has_delta)) return false;
    if (has_delta != 0) {
        for (size_t i = 0; i < count; ++i) {
            uint32_t unary = 0;
            if (!bits.Read_Unary(0u, 12u, unary)) return false;

            int32_t delta = (int32_t)unary;
            if (delta > 3) {
                uint32_t extra = 0;
                if (!bits.Read_Bits((uint32_t)delta - 3u, extra)) return false;
                delta = (1 << (delta - 3)) + (int32_t)extra + 2;
            }

            if (delta != 0) {
                uint32_t sign = 0;
                if (!bits.Read_Bit(sign)) return false;
                if (sign != 0) delta = -delta;
            }

            tdc[i] = (delta * pat + 0x200) >> 10;
        }
    }

    if (is_luma && (flags & 0x20u) && (flags & 0x80u)) {
        dst[0]  = ClipInt((min_dc < 0 ? 0 : 1024) + tdc[0], min_dc, max_dc);
        dst[1]  = ClipInt(dst[0] + tdc[1], min_dc, max_dc);
        dst[2]  = ClipInt(DCMpred2(dst[0], dst[1]) + tdc[2], min_dc, max_dc);
        dst[3]  = ClipInt(MidPred(dst[0], dst[2], dst[1]) + tdc[3], min_dc, max_dc);
        dst[4]  = ClipInt(DCMpred2(dst[1], dst[3]) + tdc[4], min_dc, max_dc);
        dst[5]  = ClipInt(dst[4] + tdc[5], min_dc, max_dc);
        dst[6]  = ClipInt(MidPred(dst[1], dst[3], dst[4]) + tdc[6], min_dc, max_dc);
        dst[7]  = ClipInt(MidPred(dst[4], dst[6], dst[5]) + tdc[7], min_dc, max_dc);
        dst[8]  = ClipInt(DCMpred2(dst[2], dst[3]) + tdc[8], min_dc, max_dc);
        dst[9]  = ClipInt(MidPred(dst[2], dst[8], dst[3]) + tdc[9], min_dc, max_dc);
        dst[10] = ClipInt(DCMpred2(dst[8], dst[9]) + tdc[10], min_dc, max_dc);
        dst[11] = ClipInt(MidPred(dst[8], dst[10], dst[9]) + tdc[11], min_dc, max_dc);
        dst[12] = ClipInt(MidPred(dst[3], dst[9], dst[6]) + tdc[12], min_dc, max_dc);
        dst[13] = ClipInt(MidPred(dst[6], dst[12], dst[7]) + tdc[13], min_dc, max_dc);
        dst[14] = ClipInt(MidPred(dst[9], dst[11], dst[12]) + tdc[14], min_dc, max_dc);
        dst[15] = ClipInt(MidPred(dst[12], dst[14], dst[13]) + tdc[15], min_dc, max_dc);
    } else if (is_luma && (flags & 0x80u)) {
        dst[0]  = ClipInt(DCMpred2(left_dc[5], left_dc[7]) + tdc[0], min_dc, max_dc);
        dst[1]  = ClipInt(dst[0] + tdc[1], min_dc, max_dc);
        dst[2]  = ClipInt(MidPred(left_dc[5], left_dc[7], dst[0]) + tdc[2], min_dc, max_dc);
        dst[3]  = ClipInt(MidPred(dst[0], dst[2], dst[1]) + tdc[3], min_dc, max_dc);
        dst[4]  = ClipInt(DCMpred2(dst[1], dst[3]) + tdc[4], min_dc, max_dc);
        dst[5]  = ClipInt(dst[4] + tdc[5], min_dc, max_dc);
        dst[6]  = ClipInt(MidPred(dst[1], dst[3], dst[4]) + tdc[6], min_dc, max_dc);
        dst[7]  = ClipInt(MidPred(dst[4], dst[6], dst[5]) + tdc[7], min_dc, max_dc);
        dst[8]  = ClipInt(MidPred(left_dc[7], left_dc[13], dst[2]) + tdc[8], min_dc, max_dc);
        dst[9]  = ClipInt(MidPred(dst[2], dst[8], dst[3]) + tdc[9], min_dc, max_dc);
        dst[10] = ClipInt(MidPred(left_dc[13], left_dc[15], dst[8]) + tdc[10], min_dc, max_dc);
        dst[11] = ClipInt(MidPred(dst[8], dst[10], dst[9]) + tdc[11], min_dc, max_dc);
        dst[12] = ClipInt(MidPred(dst[3], dst[9], dst[6]) + tdc[12], min_dc, max_dc);
        dst[13] = ClipInt(MidPred(dst[6], dst[12], dst[7]) + tdc[13], min_dc, max_dc);
        dst[14] = ClipInt(MidPred(dst[9], dst[11], dst[12]) + tdc[14], min_dc, max_dc);
        dst[15] = ClipInt(MidPred(dst[12], dst[14], dst[13]) + tdc[15], min_dc, max_dc);
    } else if (is_luma && (flags & 0x20u)) {
        dst[0]  = ClipInt(DCMpred2(top_dc[10], top_dc[11]) + tdc[0], min_dc, max_dc);
        dst[1]  = ClipInt(MidPred(top_dc[10], dst[0], top_dc[11]) + tdc[1], min_dc, max_dc);
        dst[2]  = ClipInt(DCMpred2(dst[0], dst[1]) + tdc[2], min_dc, max_dc);
        dst[3]  = ClipInt(MidPred(dst[0], dst[2], dst[1]) + tdc[3], min_dc, max_dc);
        dst[4]  = ClipInt(MidPred(top_dc[11], dst[1], top_dc[14]) + tdc[4], min_dc, max_dc);
        dst[5]  = ClipInt(MidPred(top_dc[14], dst[4], top_dc[15]) + tdc[5], min_dc, max_dc);
        dst[6]  = ClipInt(MidPred(dst[1], dst[3], dst[4]) + tdc[6], min_dc, max_dc);
        dst[7]  = ClipInt(MidPred(dst[4], dst[6], dst[5]) + tdc[7], min_dc, max_dc);
        dst[8]  = ClipInt(DCMpred2(dst[2], dst[3]) + tdc[8], min_dc, max_dc);
        dst[9]  = ClipInt(MidPred(dst[2], dst[8], dst[3]) + tdc[9], min_dc, max_dc);
        dst[10] = ClipInt(DCMpred2(dst[8], dst[9]) + tdc[10], min_dc, max_dc);
        dst[11] = ClipInt(MidPred(dst[8], dst[10], dst[9]) + tdc[11], min_dc, max_dc);
        dst[12] = ClipInt(MidPred(dst[3], dst[9], dst[6]) + tdc[12], min_dc, max_dc);
        dst[13] = ClipInt(MidPred(dst[6], dst[12], dst[7]) + tdc[13], min_dc, max_dc);
        dst[14] = ClipInt(MidPred(dst[9], dst[11], dst[12]) + tdc[14], min_dc, max_dc);
        dst[15] = ClipInt(MidPred(dst[12], dst[14], dst[13]) + tdc[15], min_dc, max_dc);
    } else if (is_luma) {
        dst[0]  = ClipInt(MidPred(top_left_dc[15], left_dc[5], top_dc[10]) + tdc[0], min_dc, max_dc);
        dst[1]  = ClipInt(MidPred(top_dc[10], dst[0], top_dc[11]) + tdc[1], min_dc, max_dc);
        dst[2]  = ClipInt(MidPred(left_dc[5], left_dc[7], dst[0]) + tdc[2], min_dc, max_dc);
        dst[3]  = ClipInt(MidPred(dst[0], dst[2], dst[1]) + tdc[3], min_dc, max_dc);
        dst[4]  = ClipInt(MidPred(top_dc[11], dst[1], top_dc[14]) + tdc[4], min_dc, max_dc);
        dst[5]  = ClipInt(MidPred(top_dc[14], dst[4], top_dc[15]) + tdc[5], min_dc, max_dc);
        dst[6]  = ClipInt(MidPred(dst[1], dst[3], dst[4]) + tdc[6], min_dc, max_dc);
        dst[7]  = ClipInt(MidPred(dst[4], dst[6], dst[5]) + tdc[7], min_dc, max_dc);
        dst[8]  = ClipInt(MidPred(left_dc[7], left_dc[13], dst[2]) + tdc[8], min_dc, max_dc);
        dst[9]  = ClipInt(MidPred(dst[2], dst[8], dst[3]) + tdc[9], min_dc, max_dc);
        dst[10] = ClipInt(MidPred(left_dc[13], left_dc[15], dst[8]) + tdc[10], min_dc, max_dc);
        dst[11] = ClipInt(MidPred(dst[8], dst[10], dst[9]) + tdc[11], min_dc, max_dc);
        dst[12] = ClipInt(MidPred(dst[3], dst[9], dst[6]) + tdc[12], min_dc, max_dc);
        dst[13] = ClipInt(MidPred(dst[6], dst[12], dst[7]) + tdc[13], min_dc, max_dc);
        dst[14] = ClipInt(MidPred(dst[9], dst[11], dst[12]) + tdc[14], min_dc, max_dc);
        dst[15] = ClipInt(MidPred(dst[12], dst[14], dst[13]) + tdc[15], min_dc, max_dc);
    } else if ((flags & 0x20u) && (flags & 0x80u)) {
        dst[0] = ClipInt((min_dc < 0 ? 0 : 1024) + tdc[0], min_dc, max_dc);
        dst[1] = ClipInt(dst[0] + tdc[1], min_dc, max_dc);
        dst[2] = ClipInt(DCMpred2(dst[0], dst[1]) + tdc[2], min_dc, max_dc);
        dst[3] = ClipInt(MidPred(dst[0], dst[2], dst[1]) + tdc[3], min_dc, max_dc);
    } else if (flags & 0x80u) {
        dst[0] = ClipInt(DCMpred2(left_dc[1], left_dc[3]) + tdc[0], min_dc, max_dc);
        dst[1] = ClipInt(dst[0] + tdc[1], min_dc, max_dc);
        dst[2] = ClipInt(MidPred(left_dc[1], left_dc[3], dst[0]) + tdc[2], min_dc, max_dc);
        dst[3] = ClipInt(MidPred(dst[0], dst[2], dst[1]) + tdc[3], min_dc, max_dc);
    } else if (flags & 0x20u) {
        dst[0] = ClipInt(DCMpred2(top_dc[2], top_dc[3]) + tdc[0], min_dc, max_dc);
        dst[1] = ClipInt(MidPred(top_dc[2], dst[0], top_dc[3]) + tdc[1], min_dc, max_dc);
        dst[2] = ClipInt(DCMpred2(dst[0], dst[1]) + tdc[2], min_dc, max_dc);
        dst[3] = ClipInt(MidPred(dst[0], dst[2], dst[1]) + tdc[3], min_dc, max_dc);
    } else {
        dst[0] = ClipInt(MidPred(top_left_dc[3], left_dc[1], top_dc[2]) + tdc[0], min_dc, max_dc);
        dst[1] = ClipInt(MidPred(top_dc[2], dst[0], top_dc[3]) + tdc[1], min_dc, max_dc);
        dst[2] = ClipInt(MidPred(left_dc[1], left_dc[3], dst[0]) + tdc[2], min_dc, max_dc);
        dst[3] = ClipInt(MidPred(dst[0], dst[2], dst[1]) + tdc[3], min_dc, max_dc);
    }

    return true;
}

bool DecodeKeyframeMacroblock(Bink2BitReader& bits,
                              const Bink2Header& header,
                              uint32_t frame_flags,
                              uint32_t mb_x,
                              uint32_t mb_y,
                              uint32_t flags,
                              int8_t left_q,
                              int8_t top_q,
                              int8_t top_left_q,
                              uint32_t& y_prev_cbp,
                              uint32_t& u_prev_cbp,
                              uint32_t& v_prev_cbp,
                              uint32_t& a_prev_cbp,
                              const std::array<int32_t, 16>& left_y_dc,
                              const std::array<int32_t, 16>& top_y_dc,
                              const std::array<int32_t, 16>& top_left_y_dc,
                              const std::array<int32_t, 4>& left_u_dc,
                              const std::array<int32_t, 4>& top_u_dc,
                              const std::array<int32_t, 4>& top_left_u_dc,
                              const std::array<int32_t, 4>& left_v_dc,
                              const std::array<int32_t, 4>& top_v_dc,
                              const std::array<int32_t, 4>& top_left_v_dc,
                              const std::array<int32_t, 16>& left_a_dc,
                              const std::array<int32_t, 16>& top_a_dc,
                              const std::array<int32_t, 16>& top_left_a_dc,
                              Bink2IntraMacroblockProbe& probe,
                              uint32_t* failure_reason)
{
    if (failure_reason) *failure_reason = 0;
    probe = {};
    probe.mb_x = mb_x;
    probe.mb_y = mb_y;
    probe.flags = flags;
    probe.has_alpha = header.Has_Alpha();
    probe.bit_offset_before = bits.Bits_Read();

    int32_t dq = 0;
    if (!DecodeDQ(bits, dq)) {
        if (failure_reason) *failure_reason = 1;
        return false;
    }
    probe.intra_q = ClipInt(PredictIntraQ(left_q, top_q, top_left_q, dq, flags),
                            0, kBink2MaxQuant - 1);
    probe.bit_offset_after_q = bits.Bits_Read();

    if (!DecodeCbpLuma(bits, frame_flags, y_prev_cbp, probe.y_cbp)) {
        if (failure_reason) *failure_reason = 2;
        return false;
    }
    y_prev_cbp = probe.y_cbp;
    if (!DecodeDcValues(bits, probe.intra_q, true, 0, 2047, flags,
                        left_y_dc, top_y_dc, top_left_y_dc, probe.y_dc)) {
        if (failure_reason) *failure_reason = 3;
        return false;
    }
    probe.bit_offset_after_y = bits.Bits_Read();

    if (!DecodeCbpChroma(bits, u_prev_cbp, probe.u_cbp)) {
        if (failure_reason) *failure_reason = 4;
        return false;
    }
    u_prev_cbp = probe.u_cbp;
    if (!DecodeDcValues(bits, probe.intra_q, false, 0, 2047, flags,
                        left_u_dc, top_u_dc, top_left_u_dc, probe.u_dc)) {
        if (failure_reason) *failure_reason = 5;
        return false;
    }
    probe.bit_offset_after_u = bits.Bits_Read();

    if (!DecodeCbpChroma(bits, v_prev_cbp, probe.v_cbp)) {
        if (failure_reason) *failure_reason = 6;
        return false;
    }
    v_prev_cbp = probe.v_cbp;
    if (!DecodeDcValues(bits, probe.intra_q, false, 0, 2047, flags,
                        left_v_dc, top_v_dc, top_left_v_dc, probe.v_dc)) {
        if (failure_reason) *failure_reason = 7;
        return false;
    }
    probe.bit_offset_after_v = bits.Bits_Read();

    if (probe.has_alpha) {
        if (!DecodeCbpLuma(bits, frame_flags, a_prev_cbp, probe.a_cbp)) {
            if (failure_reason) *failure_reason = 8;
            return false;
        }
        a_prev_cbp = probe.a_cbp;
        if (!DecodeDcValues(bits, probe.intra_q, true, 0, 2047, flags,
                            left_a_dc, top_a_dc, top_left_a_dc, probe.a_dc)) {
            if (failure_reason) *failure_reason = 9;
            return false;
        }
    }
    probe.bit_offset_after_a = bits.Bits_Read();
    probe.bit_offset_after_coeffs = probe.bit_offset_after_a;

    return true;
}

bool FindNonZeroCbpKeyframeMacroblock(const Bink2Header& header,
                                      const Bink2FramePlan& plan,
                                      const std::vector<uint8_t>& packet,
                                      Bink2IntraMacroblockProbe* probe,
                                      Bink2DecodedMacroblock* macroblock)
{
    if (probe) *probe = {};
    if (macroblock) *macroblock = {};

    if (!header.Is_Valid()) return false;
    if (packet.empty()) return false;
    if (plan.packet.num_slices == 0) return false;

    const uint32_t width_mb = (header.width + 31u) / 32u;
    const uint32_t slice_rows = plan.packet.slice_heights[0] / 32u;
    const size_t slice_end_bits =
        (plan.packet.num_slices > 1) ? (size_t)plan.packet.slice_offsets[1] * 8u
                                     : packet.size() * 8u;
    if (plan.payload_bit_offset >= slice_end_bits) return false;

    Bink2BitReader bits;
    if (!bits.Reset(packet.data(), packet.size())) return false;
    if (!bits.Skip_Bits(plan.payload_bit_offset)) return false;

    std::vector<int8_t> prev_row_q(width_mb, 0);
    std::vector<int8_t> current_row_q(width_mb, 0);
    std::vector<std::array<int32_t, 16>> prev_row_y(width_mb);
    std::vector<std::array<int32_t, 16>> current_row_y(width_mb);
    std::vector<std::array<int32_t, 16>> prev_row_a(width_mb);
    std::vector<std::array<int32_t, 16>> current_row_a(width_mb);
    std::vector<std::array<int32_t, 4>> prev_row_u(width_mb);
    std::vector<std::array<int32_t, 4>> current_row_u(width_mb);
    std::vector<std::array<int32_t, 4>> prev_row_v(width_mb);
    std::vector<std::array<int32_t, 4>> current_row_v(width_mb);
    uint32_t y_prev_cbp = 0;
    uint32_t u_prev_cbp = 0;
    uint32_t v_prev_cbp = 0;
    uint32_t a_prev_cbp = 0;

    for (uint32_t row = 0; row < slice_rows; ++row) {
        std::fill(current_row_q.begin(), current_row_q.end(), 0);

        for (uint32_t col = 0; col < width_mb; ++col) {
            if (bits.Bits_Read() >= slice_end_bits) return false;

            const uint32_t flags = (row == 0 ? 0x80u : 0u) |
                                   (col == 0 ? 0x20u : 0u) |
                                   (col == 1 ? 0x200u : 0u) |
                                   (col + 1 == width_mb ? 0x40u : 0u);

            const int8_t left_q = (col > 0) ? current_row_q[col - 1] : 0;
            const int8_t top_q = (row > 0) ? prev_row_q[col] : 0;
            const int8_t top_left_q = (row > 0 && col > 0) ? prev_row_q[col - 1] : 0;

            const std::array<int32_t, 16>& left_y = (col > 0) ? current_row_y[col - 1] : prev_row_y[col];
            const std::array<int32_t, 16>& top_y = (row > 0) ? prev_row_y[col] : current_row_y[col];
            const std::array<int32_t, 16>& top_left_y = (row > 0 && col > 0) ? prev_row_y[col - 1] : prev_row_y[col];

            const std::array<int32_t, 4>& left_u = (col > 0) ? current_row_u[col - 1] : prev_row_u[col];
            const std::array<int32_t, 4>& top_u = (row > 0) ? prev_row_u[col] : current_row_u[col];
            const std::array<int32_t, 4>& top_left_u = (row > 0 && col > 0) ? prev_row_u[col - 1] : prev_row_u[col];

            const std::array<int32_t, 4>& left_v = (col > 0) ? current_row_v[col - 1] : prev_row_v[col];
            const std::array<int32_t, 4>& top_v = (row > 0) ? prev_row_v[col] : current_row_v[col];
            const std::array<int32_t, 4>& top_left_v = (row > 0 && col > 0) ? prev_row_v[col - 1] : prev_row_v[col];

            const std::array<int32_t, 16>& left_a = (col > 0) ? current_row_a[col - 1] : prev_row_a[col];
            const std::array<int32_t, 16>& top_a = (row > 0) ? prev_row_a[col] : current_row_a[col];
            const std::array<int32_t, 16>& top_left_a = (row > 0 && col > 0) ? prev_row_a[col - 1] : prev_row_a[col];

            Bink2IntraMacroblockProbe current;
            if (!DecodeKeyframeMacroblock(bits, header, plan.packet.frame_flags, col * 32u, row * 32u,
                                         flags, left_q, top_q, top_left_q,
                                         y_prev_cbp, u_prev_cbp, v_prev_cbp, a_prev_cbp,
                                         left_y, top_y, top_left_y,
                                         left_u, top_u, top_left_u,
                                         left_v, top_v, top_left_v,
                                         left_a, top_a, top_left_a,
                                         current, nullptr)) {
                return false;
            }

            current_row_q[col] = (int8_t)current.intra_q;
            current_row_y[col] = current.y_dc;
            current_row_u[col] = current.u_dc;
            current_row_v[col] = current.v_dc;
            current_row_a[col] = current.a_dc;

            if (current.y_cbp != 0u || current.u_cbp != 0u ||
                current.v_cbp != 0u || current.a_cbp != 0u) {
                if (macroblock) {
                    if (!DecodeAndReconstructMacroblock(bits, current, *macroblock)) return false;
                }
                if (probe) {
                    *probe = macroblock ? macroblock->probe : current;
                }
                return true;
            }
        }

        prev_row_q.swap(current_row_q);
        prev_row_y.swap(current_row_y);
        prev_row_u.swap(current_row_u);
        prev_row_v.swap(current_row_v);
        prev_row_a.swap(current_row_a);
    }

    return false;
}

bool HasNonZeroCbp(const Bink2IntraMacroblockProbe& probe)
{
    return probe.y_cbp != 0u || probe.u_cbp != 0u ||
           probe.v_cbp != 0u || probe.a_cbp != 0u;
}

void AppendMacroblockTraceEntry(const Bink2IntraMacroblockProbe& probe,
                                Bink2KeyframeMacroblockTrace& trace)
{
    Bink2KeyframeMacroblockTraceEntry entry = {};
    entry.probe = probe;
    entry.bits_used =
        probe.bit_offset_after_coeffs >= probe.bit_offset_before
            ? probe.bit_offset_after_coeffs - probe.bit_offset_before
            : 0u;
    entry.has_nonzero_cbp = HasNonZeroCbp(probe);
    trace.entries.push_back(entry);
}

bool WalkFirstKeyframeSlicePrefix(const Bink2Header& header,
                                  const Bink2FramePlan& plan,
                                  const std::vector<uint8_t>& packet,
                                  Bink2DecodedKeyframeSlice* slice,
                                  Bink2KeyframeMacroblockTrace* trace)
{
    if (!slice && !trace) return false;
    if (!header.Is_Valid()) return false;
    if (packet.empty()) return false;
    if (plan.packet.num_slices == 0) return false;

    const uint32_t width_mb = (header.width + 31u) / 32u;
    const uint32_t slice_height = plan.packet.slice_heights[0];
    const uint32_t slice_rows = slice_height / 32u;
    const size_t slice_end_bits =
        (plan.packet.num_slices > 1) ? (size_t)plan.packet.slice_offsets[1] * 8u
                                     : packet.size() * 8u;
    if (plan.payload_bit_offset >= slice_end_bits) return false;

    if (slice) {
        *slice = {};
        slice->has_alpha = header.Has_Alpha();
        slice->slice_index = 0;
        slice->slice_y = 0;
        slice->luma_width = plan.aligned_width;
        slice->luma_height = slice_height;
        slice->chroma_width = plan.aligned_width / 2u;
        slice->chroma_height = slice_height / 2u;
        slice->luma_stride = plan.aligned_width;
        slice->chroma_stride = plan.aligned_width / 2u;
        slice->macroblock_cols = width_mb;
        slice->macroblock_rows = slice_rows;
        slice->macroblock_stride = width_mb;
        slice->bit_offset_begin = plan.payload_bit_offset;
        slice->decoded_macroblocks.assign((size_t)slice->macroblock_stride * slice->macroblock_rows, 0u);
        slice->y.assign((size_t)slice->luma_stride * slice->luma_height, 0u);
        slice->u.assign((size_t)slice->chroma_stride * slice->chroma_height, 0u);
        slice->v.assign((size_t)slice->chroma_stride * slice->chroma_height, 0u);
        if (slice->has_alpha) {
            slice->a.assign((size_t)slice->luma_stride * slice->luma_height, 0u);
        }
    }

    if (trace) {
        *trace = {};
        trace->has_alpha = header.Has_Alpha();
        trace->macroblock_cols = width_mb;
        trace->macroblock_rows = slice_rows;
        trace->bit_offset_begin = plan.payload_bit_offset;
        trace->entries.reserve((size_t)width_mb * slice_rows);
    }

    Bink2BitReader bits;
    if (!bits.Reset(packet.data(), packet.size())) return false;
    if (!bits.Skip_Bits(plan.payload_bit_offset)) return false;

    std::vector<int8_t> prev_row_q(width_mb, 0);
    std::vector<int8_t> current_row_q(width_mb, 0);
    std::vector<std::array<int32_t, 16>> prev_row_y(width_mb);
    std::vector<std::array<int32_t, 16>> current_row_y(width_mb);
    std::vector<std::array<int32_t, 16>> prev_row_a(width_mb);
    std::vector<std::array<int32_t, 16>> current_row_a(width_mb);
    std::vector<std::array<int32_t, 4>> prev_row_u(width_mb);
    std::vector<std::array<int32_t, 4>> current_row_u(width_mb);
    std::vector<std::array<int32_t, 4>> prev_row_v(width_mb);
    std::vector<std::array<int32_t, 4>> current_row_v(width_mb);
    uint32_t y_prev_cbp = 0;
    uint32_t u_prev_cbp = 0;
    uint32_t v_prev_cbp = 0;
    uint32_t a_prev_cbp = 0;

    for (uint32_t row = 0; row < slice_rows; ++row) {
        std::fill(current_row_q.begin(), current_row_q.end(), 0);

        for (uint32_t col = 0; col < width_mb; ++col) {
            const size_t current_bits = bits.Bits_Read();
            if (slice) {
                slice->decode_mb_col = col;
                slice->decode_mb_row = row;
                slice->bit_offset_end = current_bits;
            }
            if (trace) {
                trace->decode_mb_col = col;
                trace->decode_mb_row = row;
                trace->bit_offset_end = current_bits;
            }
            if (current_bits >= slice_end_bits) {
                if (slice) {
                    slice->failure_stage = 1u;
                    slice->bit_offset_end = current_bits;
                }
                if (trace) {
                    trace->failure_stage = 1u;
                    trace->bit_offset_end = current_bits;
                }
                return true;
            }

            const uint32_t flags = (row == 0 ? 0x80u : 0u) |
                                   (col == 0 ? 0x20u : 0u) |
                                   (col == 1 ? 0x200u : 0u) |
                                   (col + 1 == width_mb ? 0x40u : 0u);

            const int8_t left_q = (col > 0) ? current_row_q[col - 1] : 0;
            const int8_t top_q = (row > 0) ? prev_row_q[col] : 0;
            const int8_t top_left_q = (row > 0 && col > 0) ? prev_row_q[col - 1] : 0;

            const std::array<int32_t, 16>& left_y =
                (col > 0) ? current_row_y[col - 1] : prev_row_y[col];
            const std::array<int32_t, 16>& top_y =
                (row > 0) ? prev_row_y[col] : current_row_y[col];
            const std::array<int32_t, 16>& top_left_y =
                (row > 0 && col > 0) ? prev_row_y[col - 1] : prev_row_y[col];

            const std::array<int32_t, 4>& left_u =
                (col > 0) ? current_row_u[col - 1] : prev_row_u[col];
            const std::array<int32_t, 4>& top_u =
                (row > 0) ? prev_row_u[col] : current_row_u[col];
            const std::array<int32_t, 4>& top_left_u =
                (row > 0 && col > 0) ? prev_row_u[col - 1] : prev_row_u[col];

            const std::array<int32_t, 4>& left_v =
                (col > 0) ? current_row_v[col - 1] : prev_row_v[col];
            const std::array<int32_t, 4>& top_v =
                (row > 0) ? prev_row_v[col] : current_row_v[col];
            const std::array<int32_t, 4>& top_left_v =
                (row > 0 && col > 0) ? prev_row_v[col - 1] : prev_row_v[col];

            const std::array<int32_t, 16>& left_a =
                (col > 0) ? current_row_a[col - 1] : prev_row_a[col];
            const std::array<int32_t, 16>& top_a =
                (row > 0) ? prev_row_a[col] : current_row_a[col];
            const std::array<int32_t, 16>& top_left_a =
                (row > 0 && col > 0) ? prev_row_a[col - 1] : prev_row_a[col];

            Bink2BitReader candidate_bits = bits;
            uint32_t y_prev_cbp_next = y_prev_cbp;
            uint32_t u_prev_cbp_next = u_prev_cbp;
            uint32_t v_prev_cbp_next = v_prev_cbp;
            uint32_t a_prev_cbp_next = a_prev_cbp;
            Bink2IntraMacroblockProbe probe;
            uint32_t failure_reason = 0;
            if (!DecodeKeyframeMacroblock(candidate_bits, header, plan.packet.frame_flags,
                                         col * 32u, row * 32u,
                                         flags, left_q, top_q, top_left_q,
                                         y_prev_cbp_next, u_prev_cbp_next,
                                         v_prev_cbp_next, a_prev_cbp_next,
                                         left_y, top_y, top_left_y,
                                         left_u, top_u, top_left_u,
                                         left_v, top_v, top_left_v,
                                         left_a, top_a, top_left_a,
                                         probe, &failure_reason)) {
                const uint32_t stage = 20u + failure_reason;
                if (slice) {
                    slice->failure_stage = stage;
                    slice->bit_offset_end = bits.Bits_Read();
                }
                if (trace) {
                    trace->failure_stage = stage;
                    trace->bit_offset_end = bits.Bits_Read();
                }
                return false;
            }

            Bink2DecodedMacroblock macroblock;
            if (!DecodeAndReconstructMacroblock(candidate_bits, probe, macroblock)) {
                if (slice) {
                    slice->failure_stage = 3u;
                    slice->bit_offset_end = candidate_bits.Bits_Read();
                }
                if (trace) {
                    trace->failure_stage = 3u;
                    trace->bit_offset_end = candidate_bits.Bits_Read();
                }
                return false;
            }

            if (candidate_bits.Bits_Read() > slice_end_bits) {
                if (slice) {
                    slice->failure_stage = 1u;
                    slice->bit_offset_end = bits.Bits_Read();
                }
                if (trace) {
                    trace->failure_stage = 1u;
                    trace->bit_offset_end = bits.Bits_Read();
                }
                return true;
            }

            bits = candidate_bits;
            y_prev_cbp = y_prev_cbp_next;
            u_prev_cbp = u_prev_cbp_next;
            v_prev_cbp = v_prev_cbp_next;
            a_prev_cbp = a_prev_cbp_next;
            current_row_q[col] = (int8_t)probe.intra_q;
            current_row_y[col] = probe.y_dc;
            current_row_u[col] = probe.u_dc;
            current_row_v[col] = probe.v_dc;
            current_row_a[col] = probe.a_dc;

            if (slice && !BlitMacroblockIntoSlice(macroblock, col, row, *slice)) {
                slice->failure_stage = 4u;
                slice->bit_offset_end = bits.Bits_Read();
                if (trace) {
                    trace->failure_stage = 4u;
                    trace->bit_offset_end = bits.Bits_Read();
                }
                return false;
            }

            const bool has_nonzero = HasNonZeroCbp(macroblock.probe);
            if (slice) {
                slice->decoded_macroblocks[(size_t)row * slice->macroblock_stride + col] = 255u;
                ++slice->macroblock_count;
                if (has_nonzero) ++slice->nonzero_macroblock_count;
            }
            if (trace) {
                AppendMacroblockTraceEntry(macroblock.probe, *trace);
                ++trace->macroblock_count;
                if (has_nonzero) ++trace->nonzero_macroblock_count;
            }
        }

        prev_row_q.swap(current_row_q);
        prev_row_y.swap(current_row_y);
        prev_row_u.swap(current_row_u);
        prev_row_v.swap(current_row_v);
        prev_row_a.swap(current_row_a);
    }

    const size_t end_bits = bits.Bits_Read();
    if (slice) {
        slice->complete = true;
        slice->bit_offset_end = end_bits;
    }
    if (trace) {
        trace->complete = true;
        trace->bit_offset_end = end_bits;
    }
    return end_bits <= slice_end_bits;
}

} // namespace

bool Bink2PrepareFramePlan(const Bink2Header& header,
                           const Bink2PacketHeader& packet_header,
                           const std::vector<uint8_t>& packet,
                           Bink2FramePlan& plan)
{
    plan = {};

    if (!header.Is_Valid()) return false;
    if (packet.empty()) return false;
    if (packet_header.num_slices == 0) return false;
    if (packet_header.num_slices > packet_header.slice_offsets.size()) return false;

    Bink2BitReader bits;
    if (!bits.Reset(packet.data(), packet.size())) return false;

    size_t preamble_bits = (size_t)packet_header.num_slices * 32u;
    if (!bits.Skip_Bits(preamble_bits)) return false;

    plan.packet = packet_header;
    plan.aligned_width = Align32(header.width);
    plan.aligned_height = Align32(header.height);
    plan.row_flag_count = (Align16(header.height) >> 3) - 1u;
    plan.col_flag_count = (Align16(header.width) >> 3) - 1u;
    plan.has_global_block_flags = (packet_header.frame_flags & 0x10000u) != 0;
    plan.has_row_flag_stream = plan.has_global_block_flags && !packet_header.Has_Row_Block_Flags();
    plan.has_col_flag_stream = plan.has_global_block_flags && !packet_header.Has_Column_Block_Flags();

    if (plan.has_row_flag_stream &&
        !DecodeFlagStream(bits, 1u, plan.row_flag_count, plan.row_flags)) {
        return false;
    }

    if (plan.has_col_flag_stream &&
        !DecodeFlagStream(bits, 1u, plan.col_flag_count, plan.col_flags)) {
        return false;
    }

    plan.payload_bit_offset = bits.Bits_Read();
    return true;
}

bool Bink2DecodeFirstKeyframeSlicePrefix(const Bink2Header& header,
                                         const Bink2FramePlan& plan,
                                         const std::vector<uint8_t>& packet,
                                         Bink2DecodedKeyframeSlice& slice)
{
    return WalkFirstKeyframeSlicePrefix(header, plan, packet, &slice, nullptr);
}

bool Bink2DecodeFirstKeyframeFramePrefix(const Bink2Header& header,
                                         const Bink2FramePlan& plan,
                                         const std::vector<uint8_t>& packet,
                                         Bink2DecodedFrame& frame)
{
    frame = {};

    if (!header.Is_Valid()) return false;
    if (packet.empty()) return false;

    Bink2DecodedKeyframeSlice slice;
    if (!Bink2DecodeFirstKeyframeSlicePrefix(header, plan, packet, slice)) {
        return false;
    }

    frame.has_alpha = slice.has_alpha;
    frame.complete = slice.complete &&
                     plan.packet.num_slices == 1u &&
                     slice.luma_height == plan.aligned_height;
    frame.width = header.width;
    frame.height = header.height;
    frame.aligned_width = plan.aligned_width;
    frame.aligned_height = plan.aligned_height;
    frame.luma_stride = plan.aligned_width;
    frame.chroma_stride = plan.aligned_width / 2u;
    frame.macroblock_cols = plan.aligned_width / 32u;
    frame.macroblock_rows = plan.aligned_height / 32u;
    frame.macroblock_stride = frame.macroblock_cols;
    frame.decoded_slice_count = 1u;
    frame.macroblock_count = slice.macroblock_count;
    frame.nonzero_macroblock_count = slice.nonzero_macroblock_count;
    frame.decode_mb_col = slice.decode_mb_col;
    frame.decode_mb_row = slice.decode_mb_row;
    frame.failure_stage = slice.failure_stage;
    frame.bit_offset_begin = slice.bit_offset_begin;
    frame.bit_offset_end = slice.bit_offset_end;

    frame.decoded_macroblocks.assign((size_t)frame.macroblock_stride * frame.macroblock_rows, 0u);
    frame.y.assign((size_t)frame.luma_stride * frame.aligned_height, 16u);
    frame.u.assign((size_t)frame.chroma_stride * (frame.aligned_height / 2u), 128u);
    frame.v.assign((size_t)frame.chroma_stride * (frame.aligned_height / 2u), 128u);
    if (frame.has_alpha) {
        frame.a.assign((size_t)frame.luma_stride * frame.aligned_height, 0u);
    }

    if (!BlitPlaneRegion(frame.y.data(),
                         frame.luma_stride,
                         frame.aligned_width,
                         frame.aligned_height,
                         0u,
                         slice.slice_y,
                         slice.y.data(),
                         slice.luma_stride,
                         slice.luma_width,
                         slice.luma_height)) {
        return false;
    }

    const uint32_t chroma_y = slice.slice_y / 2u;
    if (!BlitPlaneRegion(frame.u.data(),
                         frame.chroma_stride,
                         frame.aligned_width / 2u,
                         frame.aligned_height / 2u,
                         0u,
                         chroma_y,
                         slice.u.data(),
                         slice.chroma_stride,
                         slice.chroma_width,
                         slice.chroma_height)) {
        return false;
    }
    if (!BlitPlaneRegion(frame.v.data(),
                         frame.chroma_stride,
                         frame.aligned_width / 2u,
                         frame.aligned_height / 2u,
                         0u,
                         chroma_y,
                         slice.v.data(),
                         slice.chroma_stride,
                         slice.chroma_width,
                         slice.chroma_height)) {
        return false;
    }
    const uint32_t macroblock_y = slice.slice_y / 32u;
    for (uint32_t row = 0; row < slice.macroblock_rows; ++row) {
        const size_t dst_offset = (size_t)(macroblock_y + row) * frame.macroblock_stride;
        const size_t src_offset = (size_t)row * slice.macroblock_stride;
        std::memcpy(frame.decoded_macroblocks.data() + dst_offset,
                    slice.decoded_macroblocks.data() + src_offset,
                    slice.macroblock_cols);
    }
    if (frame.has_alpha &&
        !BlitPlaneRegion(frame.a.data(),
                         frame.luma_stride,
                         frame.aligned_width,
                         frame.aligned_height,
                         0u,
                         slice.slice_y,
                         slice.a.data(),
                         slice.luma_stride,
                         slice.luma_width,
                         slice.luma_height)) {
        return false;
    }

    return true;
}

bool Bink2TraceFirstKeyframeSlicePrefix(const Bink2Header& header,
                                        const Bink2FramePlan& plan,
                                        const std::vector<uint8_t>& packet,
                                        Bink2KeyframeMacroblockTrace& trace)
{
    return WalkFirstKeyframeSlicePrefix(header, plan, packet, nullptr, &trace);
}

bool Bink2ReadPackedFlagBit(const std::vector<uint8_t>& flags,
                            uint32_t bit_index,
                            bool& value)
{
    size_t byte_index = bit_index >> 3;
    if (byte_index >= flags.size()) return false;

    value = ((flags[byte_index] >> (bit_index & 7u)) & 1u) != 0;
    return true;
}

bool Bink2ExpandPackedFlagBits(const std::vector<uint8_t>& flags,
                               uint32_t bit_count,
                               std::vector<uint8_t>& bits)
{
    bits.assign(bit_count, 0u);
    for (uint32_t i = 0; i < bit_count; ++i) {
        bool value = false;
        if (!Bink2ReadPackedFlagBit(flags, i, value)) return false;
        bits[i] = value ? 1u : 0u;
    }
    return true;
}

bool Bink2BuildPackedFlagRuns(const std::vector<uint8_t>& flags,
                              uint32_t bit_count,
                              std::vector<Bink2PackedFlagRun>& runs)
{
    runs.clear();
    if (bit_count == 0) return true;

    bool current_value = false;
    if (!Bink2ReadPackedFlagBit(flags, 0u, current_value)) return false;

    uint32_t run_begin = 0;
    for (uint32_t i = 1; i < bit_count; ++i) {
        bool value = false;
        if (!Bink2ReadPackedFlagBit(flags, i, value)) return false;
        if (value == current_value) continue;

        Bink2PackedFlagRun run = {};
        run.bit_begin = run_begin;
        run.bit_end = i;
        run.value = current_value;
        runs.push_back(run);

        run_begin = i;
        current_value = value;
    }

    Bink2PackedFlagRun run = {};
    run.bit_begin = run_begin;
    run.bit_end = bit_count;
    run.value = current_value;
    runs.push_back(run);
    return true;
}

bool Bink2BuildPackedFlagGroups(const std::vector<uint8_t>& flags,
                                uint32_t bit_count,
                                bool split_on_set_boundary,
                                uint32_t unit_pixels,
                                std::vector<Bink2PackedFlagGroup>& groups)
{
    groups.clear();
    if (unit_pixels == 0) return false;

    uint32_t unit_begin = 0;
    for (uint32_t i = 0; i < bit_count; ++i) {
        bool value = false;
        if (!Bink2ReadPackedFlagBit(flags, i, value)) return false;
        if (value != split_on_set_boundary) continue;

        Bink2PackedFlagGroup group = {};
        group.unit_begin = unit_begin;
        group.unit_end = i + 1u;
        group.pixel_begin = group.unit_begin * unit_pixels;
        group.pixel_end = group.unit_end * unit_pixels;
        group.macroblock_begin = group.pixel_begin / kBink2MacroblockPixels;
        group.macroblock_end =
            (group.pixel_end + kBink2MacroblockPixels - 1u) / kBink2MacroblockPixels;
        groups.push_back(group);

        unit_begin = i + 1u;
    }

    Bink2PackedFlagGroup group = {};
    group.unit_begin = unit_begin;
    group.unit_end = bit_count + 1u;
    group.pixel_begin = group.unit_begin * unit_pixels;
    group.pixel_end = group.unit_end * unit_pixels;
    group.macroblock_begin = group.pixel_begin / kBink2MacroblockPixels;
    group.macroblock_end =
        (group.pixel_end + kBink2MacroblockPixels - 1u) / kBink2MacroblockPixels;
    groups.push_back(group);
    return true;
}

bool Bink2BuildMacroblockRowCoverage(const Bink2DecodedFrame& frame,
                                     std::vector<Bink2MacroblockCoverageLine>& lines)
{
    lines.clear();

    if (frame.macroblock_cols == 0 || frame.macroblock_rows == 0) return false;
    if (frame.decoded_macroblocks.size() <
        (size_t)frame.macroblock_stride * frame.macroblock_rows) {
        return false;
    }

    lines.assign(frame.macroblock_rows, {});
    for (uint32_t row = 0; row < frame.macroblock_rows; ++row) {
        Bink2MacroblockCoverageLine line = {};
        line.line_index = row;

        const size_t row_offset = (size_t)row * frame.macroblock_stride;
        for (uint32_t col = 0; col < frame.macroblock_cols; ++col) {
            if (frame.decoded_macroblocks[row_offset + col] == 0u) break;
            ++line.leading_decoded;
        }
        for (uint32_t col = 0; col < frame.macroblock_cols; ++col) {
            line.total_decoded +=
                frame.decoded_macroblocks[row_offset + col] != 0u ? 1u : 0u;
        }

        lines[row] = line;
    }

    return true;
}

bool Bink2BuildMacroblockColumnCoverage(const Bink2DecodedFrame& frame,
                                        std::vector<Bink2MacroblockCoverageLine>& lines)
{
    lines.clear();

    if (frame.macroblock_cols == 0 || frame.macroblock_rows == 0) return false;
    if (frame.decoded_macroblocks.size() <
        (size_t)frame.macroblock_stride * frame.macroblock_rows) {
        return false;
    }

    lines.assign(frame.macroblock_cols, {});
    for (uint32_t col = 0; col < frame.macroblock_cols; ++col) {
        Bink2MacroblockCoverageLine line = {};
        line.line_index = col;

        for (uint32_t row = 0; row < frame.macroblock_rows; ++row) {
            if (frame.decoded_macroblocks[(size_t)row * frame.macroblock_stride + col] == 0u) break;
            ++line.leading_decoded;
        }
        for (uint32_t row = 0; row < frame.macroblock_rows; ++row) {
            line.total_decoded +=
                frame.decoded_macroblocks[(size_t)row * frame.macroblock_stride + col] != 0u
                    ? 1u
                    : 0u;
        }

        lines[col] = line;
    }

    return true;
}

bool Bink2BuildPackedFlagGroupCoverage(const std::vector<Bink2PackedFlagGroup>& groups,
                                       const std::vector<Bink2MacroblockCoverageLine>& lines,
                                       uint32_t line_capacity,
                                       uint32_t stop_line_index,
                                       std::vector<Bink2PackedFlagGroupCoverage>& coverage)
{
    coverage.clear();

    if (line_capacity == 0) return false;

    coverage.assign(groups.size(), {});
    for (size_t i = 0; i < groups.size(); ++i) {
        Bink2PackedFlagGroupCoverage item = {};
        item.group = groups[i];
        item.line_begin = std::min(groups[i].macroblock_begin, (uint32_t)lines.size());
        item.line_end = std::min(groups[i].macroblock_end, (uint32_t)lines.size());
        item.contains_stop_line =
            stop_line_index >= item.line_begin && stop_line_index < item.line_end;

        if (item.line_begin < item.line_end) {
            item.min_leading_decoded = lines[item.line_begin].leading_decoded;
            item.max_leading_decoded = lines[item.line_begin].leading_decoded;
        }

        for (uint32_t line_index = item.line_begin; line_index < item.line_end; ++line_index) {
            const Bink2MacroblockCoverageLine& line = lines[line_index];
            item.decoded_total += line.total_decoded;
            item.decoded_capacity += line_capacity;
            item.min_leading_decoded =
                std::min(item.min_leading_decoded, line.leading_decoded);
            item.max_leading_decoded =
                std::max(item.max_leading_decoded, line.leading_decoded);

            if (line.total_decoded == line_capacity) {
                ++item.full_lines;
            } else if (line.total_decoded != 0u) {
                ++item.partial_lines;
            }
        }

        coverage[i] = item;
    }

    return true;
}

size_t Bink2CountSetFlagBits(const std::vector<uint8_t>& flags,
                             uint32_t bit_count)
{
    size_t total = 0;
    for (uint32_t i = 0; i < bit_count; ++i) {
        bool bit = false;
        if (!Bink2ReadPackedFlagBit(flags, i, bit)) break;
        total += bit ? 1u : 0u;
    }
    return total;
}

bool Bink2ProbeFirstKeyframeMacroblock(const Bink2Header& header,
                                       const Bink2FramePlan& plan,
                                       const std::vector<uint8_t>& packet,
                                       Bink2IntraMacroblockProbe& probe)
{
    probe = {};

    if (!header.Is_Valid()) return false;
    if (packet.empty()) return false;
    if (plan.payload_bit_offset >= packet.size() * 8u) return false;

    Bink2BitReader bits;
    if (!bits.Reset(packet.data(), packet.size())) return false;
    if (!bits.Skip_Bits(plan.payload_bit_offset)) return false;
    std::array<int32_t, 16> zero_y = {};
    std::array<int32_t, 4> zero_c = {};
    uint32_t y_prev_cbp = 0, u_prev_cbp = 0, v_prev_cbp = 0, a_prev_cbp = 0;
    return DecodeKeyframeMacroblock(bits, header, plan.packet.frame_flags, 0u, 0u,
                                    0x20u | 0x80u, 0, 0, 0,
                                    y_prev_cbp, u_prev_cbp, v_prev_cbp, a_prev_cbp,
                                    zero_y, zero_y, zero_y,
                                    zero_c, zero_c, zero_c,
                                    zero_c, zero_c, zero_c,
                                    zero_y, zero_y, zero_y,
                                    probe, nullptr);
}

bool Bink2FindFirstNonZeroCbpKeyframeMacroblock(const Bink2Header& header,
                                                const Bink2FramePlan& plan,
                                                const std::vector<uint8_t>& packet,
                                                Bink2IntraMacroblockProbe& probe)
{
    return FindNonZeroCbpKeyframeMacroblock(header, plan, packet, &probe, nullptr);
}

bool Bink2DecodeFirstNonZeroCbpKeyframeMacroblock(const Bink2Header& header,
                                                  const Bink2FramePlan& plan,
                                                  const std::vector<uint8_t>& packet,
                                                  Bink2DecodedMacroblock& macroblock)
{
    return FindNonZeroCbpKeyframeMacroblock(header, plan, packet, nullptr, &macroblock);
}

bool Bink2ReconstructFirstMacroblockDcOnly(const Bink2IntraMacroblockProbe& probe,
                                           Bink2MacroblockPixels& pixels)
{
    pixels = {};

    if (probe.y_cbp != 0u || probe.u_cbp != 0u || probe.v_cbp != 0u || probe.a_cbp != 0u)
        return false;

    pixels.has_alpha = probe.has_alpha;
    FillPlaneWithBlocks(pixels.y, 32, probe.y_dc.data(), probe.y_dc.size(), kLumaRepos, true);
    FillPlaneWithBlocks(pixels.u, 16, probe.u_dc.data(), probe.u_dc.size(), nullptr, false);
    FillPlaneWithBlocks(pixels.v, 16, probe.v_dc.data(), probe.v_dc.size(), nullptr, false);

    if (probe.has_alpha)
        FillPlaneWithBlocks(pixels.a, 32, probe.a_dc.data(), probe.a_dc.size(), kLumaRepos, true);

    return true;
}
