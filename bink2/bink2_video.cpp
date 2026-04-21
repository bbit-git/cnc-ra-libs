/*
 * bink2_video.cpp - Early packet-to-frame planning helpers for BK2 video decode.
 */

#include "bink2_video.h"

#include "bink2_bitstream.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
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

static const uint16_t kBink2gInterQmat[4][64] = {
    {
        1024, 1193, 1076,  844, 1052,  914, 1225, 1492,
        1193, 1391, 1254,  983, 1227, 1065, 1463, 1816,
        1076, 1254, 1161,  936, 1195, 1034, 1444, 1741,
         844,  983,  936,  811, 1055,  927, 1305, 1584,
        1052, 1227, 1195, 1055, 1451, 1336, 1912, 2354,
         914, 1065, 1034,  927, 1336, 1313, 1945, 2486,
        1225, 1463, 1444, 1305, 1912, 1945, 3044, 4039,
        1492, 1816, 1741, 1584, 2354, 2486, 4039, 5679
    }, {
        1218, 1419, 1279, 1003, 1252, 1087, 1457, 1774,
        1419, 1654, 1491, 1169, 1459, 1267, 1739, 2159,
        1279, 1491, 1381, 1113, 1421, 1230, 1717, 2070,
        1003, 1169, 1113,  965, 1254, 1103, 1552, 1884,
        1252, 1459, 1421, 1254, 1725, 1589, 2274, 2799,
        1087, 1267, 1230, 1103, 1589, 1562, 2313, 2956,
        1457, 1739, 1717, 1552, 2274, 2313, 3620, 4803,
        1774, 2159, 2070, 1884, 2799, 2956, 4803, 6753
    }, {
        1448, 1688, 1521, 1193, 1488, 1293, 1732, 2110,
        1688, 1967, 1773, 1391, 1735, 1507, 2068, 2568,
        1521, 1773, 1642, 1323, 1690, 1462, 2042, 2462,
        1193, 1391, 1323, 1147, 1492, 1311, 1845, 2241,
        1488, 1735, 1690, 1492, 2052, 1889, 2704, 3328,
        1293, 1507, 1462, 1311, 1889, 1857, 2751, 3515,
        1732, 2068, 2042, 1845, 2704, 2751, 4306, 5712,
        2110, 2568, 2462, 2241, 3328, 3515, 5712, 8031
    }, {
        1722, 2007, 1809, 1419, 1770, 1537, 2060, 2509,
        2007, 2339, 2108, 1654, 2063, 1792, 2460, 3054,
        1809, 2108, 1953, 1574, 2010, 1739, 2428, 2928,
        1419, 1654, 1574, 1364, 1774, 1559, 2195, 2664,
        1770, 2063, 2010, 1774, 2440, 2247, 3216, 3958,
        1537, 1792, 1739, 1559, 2247, 2209, 3271, 4181,
        2060, 2460, 2428, 2195, 3216, 3271, 5120, 6793,
        2509, 3054, 2928, 2664, 3958, 4181, 6793, 9550
    }
};

static const int32_t kBink2MaxQuant = 37;
static const uint32_t kBink2MacroblockPixels = 32u;

// FFmpeg bink2g_mv_codes / bink2g_mv_bits; used to decode MV VLC symbols
// (16-entry table, little-endian bit reading).
static const uint16_t kBink2gMvCodes[16] = {
    0x01, 0x06, 0x0C, 0x1C, 0x18, 0x38, 0x58, 0x78,
    0x68, 0x48, 0x28, 0x08, 0x14, 0x04, 0x02, 0x00,
};
static const uint8_t kBink2gMvBits[16] = {
    1, 3, 5, 5, 7, 7, 7, 7, 7, 7, 7, 7, 5, 5, 3, 4,
};

uint32_t Align16(uint32_t value)
{
    return (value + 15u) & ~15u;
}

uint32_t Align32(uint32_t value)
{
    return (value + 31u) & ~31u;
}

// frame_flags bit 0x80000: "skip alpha decode, fill alpha plane with
// (frame_flags >> 24) instead." LOGO.BK2 uses this to pack a constant-
// alpha image without spending bitstream on alpha residuals. Ported
// from NihAV bink2.rs:1084-1090; FFmpeg n4.3's patched bink2g doesn't
// handle it, which is why both it and we hard-fail on LOGO.BK2.
bool Bink2EffectiveHasAlpha(const Bink2Header& header, uint32_t frame_flags)
{
    return header.Has_Alpha() && (frame_flags & 0x80000u) == 0u;
}

uint8_t Bink2AlphaFillValue(uint32_t frame_flags)
{
    return (uint8_t)((frame_flags >> 24) & 0xffu);
}

int32_t ClipInt(int32_t value, int32_t min_value, int32_t max_value)
{
    return std::max(min_value, std::min(max_value, value));
}

int32_t MidPred(int32_t a, int32_t b, int32_t c)
{
    return a + b + c - std::max(a, std::max(b, c)) - std::min(a, std::min(b, c));
}

int32_t DCMpred(int32_t a, int32_t b, int32_t c)
{
    const int32_t lo = std::min(a, std::min(b, c));
    const int32_t hi = std::max(a, std::max(b, c));
    return std::min(std::max(c + b - a, lo), hi);
}

int32_t DCMpred2(int32_t a, int32_t b)
{
    const int32_t lo = std::min(a, b);
    const int32_t hi = std::max(a, b);
    return std::min(std::max(2 * a - b, lo), hi);
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

// AC coefficient dump state (BK2_DUMP_AC_COEFS). Caller sets
// g_dbg_ac_active before each DecodeAcBlocks call; the sidecar
// g_dbg_luma_{frame,row,col}_idx defined further down are also used.
// Declared here (above the first use) to keep linkage simple — the
// definitions near line ~2770 are moved up into this block.
static int g_dbg_ac_active = 0;
static int g_dbg_ac_group = 0;
static int g_dbg_luma_frame_idx = 0;
static int g_dbg_luma_mb_row = 0;
static int g_dbg_luma_mb_col = 0;
static const char* g_dbg_luma_plane_tag = "Y";

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
            const int64_t dequant_pre =
                (int64_t)coeff * (int64_t)qmat[q & 3][scan_idx] * (int64_t)q_scale;
            const int64_t dequant = (dequant_pre + 64) >> 7;
            blocks[block_index][scan_idx] =
                (int16_t)ClipInt((int32_t)dequant, -32768, 32767);
            if (g_dbg_ac_active) {
                std::fprintf(stderr,
                    "AC f=%d r=%d c=%d plane=%s g=%d b=%d idx=%d scan=%d "
                    "raw=%d qmat=%u qshift=%d q3=%d preshift=%lld deq=%lld\n",
                    g_dbg_luma_frame_idx, g_dbg_luma_mb_row, g_dbg_luma_mb_col,
                    g_dbg_luma_plane_tag, g_dbg_ac_group, (int)block_index,
                    (int)idx, (int)scan_idx, (int)coeff,
                    (unsigned)qmat[q & 3][scan_idx], (int)(q >> 2), (int)(q & 3),
                    (long long)dequant_pre, (long long)dequant);
            }
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

bool DecodeLumaPlaneAc(Bink2BitReader& bits,
                       uint32_t cbp,
                       int32_t q,
                       std::array<std::array<int16_t, 64>, 16>& out_blocks)
{
    std::array<std::array<int16_t, 64>, 4> blocks = {};
    for (size_t g = 0; g < 4; ++g) {
        if (!DecodeAcBlocks(bits, cbp >> (g * 4u), q, kBink2gLumaIntraQmat, blocks))
            return false;
        for (size_t i = 0; i < blocks.size(); ++i) {
            out_blocks[g * 4u + i] = blocks[i];
        }
    }
    return true;
}

bool DecodeChromaPlaneAc(Bink2BitReader& bits,
                         uint32_t cbp,
                         int32_t q,
                         std::array<std::array<int16_t, 64>, 4>& out_blocks)
{
    return DecodeAcBlocks(bits, cbp, q, kBink2gChromaIntraQmat, out_blocks);
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
    BlitPlaneBlock(slice.v.data() + uv_y * slice.chroma_stride + uv_x,
                   slice.chroma_stride,
                   macroblock.pixels.u.data(),
                   16u,
                   16u,
                   16u);
    BlitPlaneBlock(slice.u.data() + uv_y * slice.chroma_stride + uv_x,
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
    // FFmpeg's bink2_get_block_flags uses a signed flags_left — it can go
    // negative when an RLE `extra` run over-consumes, and the outer loop
    // simply exits on the next check. nb_coded keeps the (unclamped) run
    // length for the byte-emit loop.
    int32_t flags_left = (int32_t)flag_count;
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
            if (flags_left < 5) {
                uint32_t literal_bits = flags_left > 0 ? (uint32_t)(flags_left - 1) : 0u;
                if (literal_bits != 0 && !bits.Read_Bits(literal_bits, tail_bits)) return false;
                tail_bits <<= ((bit_offset + 1u) & 31u);
                bit_offset += (uint32_t)flags_left;
                flags_left = 0;
            } else {
                if (!bits.Read_Bits(4u, tail_bits)) return false;
                tail_bits <<= ((bit_offset + 1u) & 31u);
                bit_offset += 5u;
                flags_left -= 5;
            }

            accumulator |= (flag << (cache & 31u)) | tail_bits;
            if (!FlushPackedByte(accumulator, bit_offset, dst, out_index)) return false;
        } else {
            uint32_t bits_for_run = (flags_left < 4) ? 2u : (flags_left < 16 ? 4u : 5u);
            uint32_t coded = bits_for_run + 1u;

            if (mode == 3u) {
                flag ^= 1u;
            } else {
                ++coded;
                if (!bits.Read_Bit(flag)) return false;
            }

            coded = std::min(coded, (uint32_t)flags_left);
            flags_left -= (int32_t)coded;

            if (flags_left > 0) {
                uint32_t extra = 0;
                if (!bits.Read_Bits(bits_for_run, extra)) return false;
                // FFmpeg allows `extra` > flags_left: flags_left may go
                // negative, loop exits next iter, nb_coded keeps the full
                // run for byte emission.
                flags_left -= (int32_t)extra;
                coded += extra;
                mode = (extra == ((1u << bits_for_run) - 1u)) ? 1u : 3u;
            }

            uint32_t fill = flag ? 0xffu : 0u;
            while (coded > 8u) {
                accumulator |= fill << (cache & 31u);
                // When an RLE run over-consumes past flag_count, we may
                // attempt to emit extra bytes beyond dst.size(). Silently
                // drop those — they are padding bits the caller ignores.
                if (out_index < dst.size()) {
                    dst[out_index++] = (uint8_t)(accumulator & 0xffu);
                }
                accumulator >>= 8;
                coded -= 8u;
            }

            if (coded > 0u) {
                bit_offset += coded;
                uint32_t mask = (coded == 32u) ? 0xffffffffu : ((1u << coded) - 1u);
                accumulator |= (mask & fill) << (cache & 31u);
                if (bit_offset >= 8u) {
                    if (out_index < dst.size()) {
                        dst[out_index++] = (uint8_t)(accumulator & 0xffu);
                    }
                    accumulator >>= 8;
                    bit_offset -= 8u;
                }
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
        dst[3]  = ClipInt(DCMpred(dst[0], dst[2], dst[1]) + tdc[3], min_dc, max_dc);
        dst[4]  = ClipInt(DCMpred2(dst[1], dst[3]) + tdc[4], min_dc, max_dc);
        dst[5]  = ClipInt(dst[4] + tdc[5], min_dc, max_dc);
        dst[6]  = ClipInt(DCMpred(dst[1], dst[3], dst[4]) + tdc[6], min_dc, max_dc);
        dst[7]  = ClipInt(DCMpred(dst[4], dst[6], dst[5]) + tdc[7], min_dc, max_dc);
        dst[8]  = ClipInt(DCMpred2(dst[2], dst[3]) + tdc[8], min_dc, max_dc);
        dst[9]  = ClipInt(DCMpred(dst[2], dst[8], dst[3]) + tdc[9], min_dc, max_dc);
        dst[10] = ClipInt(DCMpred2(dst[8], dst[9]) + tdc[10], min_dc, max_dc);
        dst[11] = ClipInt(DCMpred(dst[8], dst[10], dst[9]) + tdc[11], min_dc, max_dc);
        dst[12] = ClipInt(DCMpred(dst[3], dst[9], dst[6]) + tdc[12], min_dc, max_dc);
        dst[13] = ClipInt(DCMpred(dst[6], dst[12], dst[7]) + tdc[13], min_dc, max_dc);
        dst[14] = ClipInt(DCMpred(dst[9], dst[11], dst[12]) + tdc[14], min_dc, max_dc);
        dst[15] = ClipInt(DCMpred(dst[12], dst[14], dst[13]) + tdc[15], min_dc, max_dc);
    } else if (is_luma && (flags & 0x80u)) {
        dst[0]  = ClipInt(DCMpred2(left_dc[5], left_dc[7]) + tdc[0], min_dc, max_dc);
        dst[1]  = ClipInt(dst[0] + tdc[1], min_dc, max_dc);
        dst[2]  = ClipInt(DCMpred(left_dc[5], left_dc[7], dst[0]) + tdc[2], min_dc, max_dc);
        dst[3]  = ClipInt(DCMpred(dst[0], dst[2], dst[1]) + tdc[3], min_dc, max_dc);
        dst[4]  = ClipInt(DCMpred2(dst[1], dst[3]) + tdc[4], min_dc, max_dc);
        dst[5]  = ClipInt(dst[4] + tdc[5], min_dc, max_dc);
        dst[6]  = ClipInt(DCMpred(dst[1], dst[3], dst[4]) + tdc[6], min_dc, max_dc);
        dst[7]  = ClipInt(DCMpred(dst[4], dst[6], dst[5]) + tdc[7], min_dc, max_dc);
        dst[8]  = ClipInt(DCMpred(left_dc[7], left_dc[13], dst[2]) + tdc[8], min_dc, max_dc);
        dst[9]  = ClipInt(DCMpred(dst[2], dst[8], dst[3]) + tdc[9], min_dc, max_dc);
        dst[10] = ClipInt(DCMpred(left_dc[13], left_dc[15], dst[8]) + tdc[10], min_dc, max_dc);
        dst[11] = ClipInt(DCMpred(dst[8], dst[10], dst[9]) + tdc[11], min_dc, max_dc);
        dst[12] = ClipInt(DCMpred(dst[3], dst[9], dst[6]) + tdc[12], min_dc, max_dc);
        dst[13] = ClipInt(DCMpred(dst[6], dst[12], dst[7]) + tdc[13], min_dc, max_dc);
        dst[14] = ClipInt(DCMpred(dst[9], dst[11], dst[12]) + tdc[14], min_dc, max_dc);
        dst[15] = ClipInt(DCMpred(dst[12], dst[14], dst[13]) + tdc[15], min_dc, max_dc);
    } else if (is_luma && (flags & 0x20u)) {
        dst[0]  = ClipInt(DCMpred2(top_dc[10], top_dc[11]) + tdc[0], min_dc, max_dc);
        dst[1]  = ClipInt(DCMpred(top_dc[10], dst[0], top_dc[11]) + tdc[1], min_dc, max_dc);
        dst[2]  = ClipInt(DCMpred2(dst[0], dst[1]) + tdc[2], min_dc, max_dc);
        dst[3]  = ClipInt(DCMpred(dst[0], dst[2], dst[1]) + tdc[3], min_dc, max_dc);
        dst[4]  = ClipInt(DCMpred(top_dc[11], dst[1], top_dc[14]) + tdc[4], min_dc, max_dc);
        dst[5]  = ClipInt(DCMpred(top_dc[14], dst[4], top_dc[15]) + tdc[5], min_dc, max_dc);
        dst[6]  = ClipInt(DCMpred(dst[1], dst[3], dst[4]) + tdc[6], min_dc, max_dc);
        dst[7]  = ClipInt(DCMpred(dst[4], dst[6], dst[5]) + tdc[7], min_dc, max_dc);
        dst[8]  = ClipInt(DCMpred2(dst[2], dst[3]) + tdc[8], min_dc, max_dc);
        dst[9]  = ClipInt(DCMpred(dst[2], dst[8], dst[3]) + tdc[9], min_dc, max_dc);
        dst[10] = ClipInt(DCMpred2(dst[8], dst[9]) + tdc[10], min_dc, max_dc);
        dst[11] = ClipInt(DCMpred(dst[8], dst[10], dst[9]) + tdc[11], min_dc, max_dc);
        dst[12] = ClipInt(DCMpred(dst[3], dst[9], dst[6]) + tdc[12], min_dc, max_dc);
        dst[13] = ClipInt(DCMpred(dst[6], dst[12], dst[7]) + tdc[13], min_dc, max_dc);
        dst[14] = ClipInt(DCMpred(dst[9], dst[11], dst[12]) + tdc[14], min_dc, max_dc);
        dst[15] = ClipInt(DCMpred(dst[12], dst[14], dst[13]) + tdc[15], min_dc, max_dc);
    } else if (is_luma) {
        dst[0]  = ClipInt(DCMpred(top_left_dc[15], left_dc[5], top_dc[10]) + tdc[0], min_dc, max_dc);
        dst[1]  = ClipInt(DCMpred(top_dc[10], dst[0], top_dc[11]) + tdc[1], min_dc, max_dc);
        dst[2]  = ClipInt(DCMpred(left_dc[5], left_dc[7], dst[0]) + tdc[2], min_dc, max_dc);
        dst[3]  = ClipInt(DCMpred(dst[0], dst[2], dst[1]) + tdc[3], min_dc, max_dc);
        dst[4]  = ClipInt(DCMpred(top_dc[11], dst[1], top_dc[14]) + tdc[4], min_dc, max_dc);
        dst[5]  = ClipInt(DCMpred(top_dc[14], dst[4], top_dc[15]) + tdc[5], min_dc, max_dc);
        dst[6]  = ClipInt(DCMpred(dst[1], dst[3], dst[4]) + tdc[6], min_dc, max_dc);
        dst[7]  = ClipInt(DCMpred(dst[4], dst[6], dst[5]) + tdc[7], min_dc, max_dc);
        dst[8]  = ClipInt(DCMpred(left_dc[7], left_dc[13], dst[2]) + tdc[8], min_dc, max_dc);
        dst[9]  = ClipInt(DCMpred(dst[2], dst[8], dst[3]) + tdc[9], min_dc, max_dc);
        dst[10] = ClipInt(DCMpred(left_dc[13], left_dc[15], dst[8]) + tdc[10], min_dc, max_dc);
        dst[11] = ClipInt(DCMpred(dst[8], dst[10], dst[9]) + tdc[11], min_dc, max_dc);
        dst[12] = ClipInt(DCMpred(dst[3], dst[9], dst[6]) + tdc[12], min_dc, max_dc);
        dst[13] = ClipInt(DCMpred(dst[6], dst[12], dst[7]) + tdc[13], min_dc, max_dc);
        dst[14] = ClipInt(DCMpred(dst[9], dst[11], dst[12]) + tdc[14], min_dc, max_dc);
        dst[15] = ClipInt(DCMpred(dst[12], dst[14], dst[13]) + tdc[15], min_dc, max_dc);
    } else if ((flags & 0x20u) && (flags & 0x80u)) {
        dst[0] = ClipInt((min_dc < 0 ? 0 : 1024) + tdc[0], min_dc, max_dc);
        dst[1] = ClipInt(dst[0] + tdc[1], min_dc, max_dc);
        dst[2] = ClipInt(DCMpred2(dst[0], dst[1]) + tdc[2], min_dc, max_dc);
        dst[3] = ClipInt(DCMpred(dst[0], dst[2], dst[1]) + tdc[3], min_dc, max_dc);
    } else if (flags & 0x80u) {
        dst[0] = ClipInt(DCMpred2(left_dc[1], left_dc[3]) + tdc[0], min_dc, max_dc);
        dst[1] = ClipInt(dst[0] + tdc[1], min_dc, max_dc);
        dst[2] = ClipInt(DCMpred(left_dc[1], left_dc[3], dst[0]) + tdc[2], min_dc, max_dc);
        dst[3] = ClipInt(DCMpred(dst[0], dst[2], dst[1]) + tdc[3], min_dc, max_dc);
    } else if (flags & 0x20u) {
        dst[0] = ClipInt(DCMpred2(top_dc[2], top_dc[3]) + tdc[0], min_dc, max_dc);
        dst[1] = ClipInt(DCMpred(top_dc[2], dst[0], top_dc[3]) + tdc[1], min_dc, max_dc);
        dst[2] = ClipInt(DCMpred2(dst[0], dst[1]) + tdc[2], min_dc, max_dc);
        dst[3] = ClipInt(DCMpred(dst[0], dst[2], dst[1]) + tdc[3], min_dc, max_dc);
    } else {
        dst[0] = ClipInt(DCMpred(top_left_dc[3], left_dc[1], top_dc[2]) + tdc[0], min_dc, max_dc);
        dst[1] = ClipInt(DCMpred(top_dc[2], dst[0], top_dc[3]) + tdc[1], min_dc, max_dc);
        dst[2] = ClipInt(DCMpred(left_dc[1], left_dc[3], dst[0]) + tdc[2], min_dc, max_dc);
        dst[3] = ClipInt(DCMpred(dst[0], dst[2], dst[1]) + tdc[3], min_dc, max_dc);
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
                              Bink2DecodedMacroblock& macroblock,
                              uint32_t* failure_reason)
{
    if (failure_reason) *failure_reason = 0;
    macroblock = {};
    Bink2IntraMacroblockProbe& probe = macroblock.probe;
    probe.mb_x = mb_x;
    probe.mb_y = mb_y;
    probe.flags = flags;
    probe.has_alpha = Bink2EffectiveHasAlpha(header, frame_flags);
    probe.bit_offset_before = bits.Bits_Read();
    macroblock.pixels.has_alpha = probe.has_alpha;

    int32_t dq = 0;
    if (!DecodeDQ(bits, dq)) {
        if (failure_reason) *failure_reason = 1;
        return false;
    }
    probe.intra_q = ClipInt(PredictIntraQ(left_q, top_q, top_left_q, dq, flags),
                            0, kBink2MaxQuant - 1);
    probe.bit_offset_after_q = bits.Bits_Read();

    // Y: CBP -> DC -> AC -> (reconstruct later)
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
    if (!DecodeLumaPlaneAc(bits, probe.y_cbp, probe.intra_q, macroblock.y_blocks)) {
        if (failure_reason) *failure_reason = 10;
        return false;
    }
    probe.bit_offset_after_y = bits.Bits_Read();

    // U
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
    if (!DecodeChromaPlaneAc(bits, probe.u_cbp, probe.intra_q, macroblock.u_blocks)) {
        if (failure_reason) *failure_reason = 11;
        return false;
    }
    probe.bit_offset_after_u = bits.Bits_Read();

    // V
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
    if (!DecodeChromaPlaneAc(bits, probe.v_cbp, probe.intra_q, macroblock.v_blocks)) {
        if (failure_reason) *failure_reason = 12;
        return false;
    }
    probe.bit_offset_after_v = bits.Bits_Read();

    // A
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
        if (!DecodeLumaPlaneAc(bits, probe.a_cbp, probe.intra_q, macroblock.a_blocks)) {
            if (failure_reason) *failure_reason = 13;
            return false;
        }
    }
    probe.bit_offset_after_a = bits.Bits_Read();
    probe.bit_offset_after_coeffs = probe.bit_offset_after_a;

    // Reconstruct pixel planes from DC + AC blocks
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

    return true;
}

bool HasNonZeroCbp(const Bink2IntraMacroblockProbe& probe);

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
        y_prev_cbp = 0;
        u_prev_cbp = 0;
        v_prev_cbp = 0;
        a_prev_cbp = 0;

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

            Bink2DecodedMacroblock current = {};
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

            current_row_q[col] = (int8_t)current.probe.intra_q;
            current_row_y[col] = current.probe.y_dc;
            current_row_u[col] = current.probe.u_dc;
            current_row_v[col] = current.probe.v_dc;
            current_row_a[col] = current.probe.a_dc;

            if (HasNonZeroCbp(current.probe)) {
                if (macroblock) *macroblock = current;
                if (probe) *probe = current.probe;
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

bool WalkKeyframeSlicePrefix(const Bink2Header& header,
                             const Bink2FramePlan& plan,
                             const std::vector<uint8_t>& packet,
                             uint32_t slice_index,
                             Bink2DecodedKeyframeSlice* slice,
                             Bink2KeyframeMacroblockTrace* trace)
{
    if (!slice && !trace) return false;
    if (!header.Is_Valid()) return false;
    if (packet.empty()) return false;
    if (plan.packet.num_slices == 0) return false;
    if (slice_index >= plan.packet.num_slices) return false;

    const uint32_t width_mb = (header.width + 31u) / 32u;
    const uint32_t slice_y_begin =
        (slice_index == 0) ? 0u : plan.packet.slice_heights[slice_index - 1];
    const uint32_t slice_y_end = plan.packet.slice_heights[slice_index];
    if (slice_y_end <= slice_y_begin) return false;
    const uint32_t slice_height = slice_y_end - slice_y_begin;
    const uint32_t slice_rows = slice_height / 32u;

    const size_t slice_start_bits =
        (slice_index == 0) ? plan.payload_bit_offset
                           : (size_t)plan.packet.slice_offsets[slice_index] * 8u;
    const size_t slice_end_bits =
        (slice_index + 1u < plan.packet.num_slices)
            ? (size_t)plan.packet.slice_offsets[slice_index + 1u] * 8u
            : packet.size() * 8u;
    if (slice_start_bits >= slice_end_bits) return false;

    if (slice) {
        *slice = {};
        slice->has_alpha = Bink2EffectiveHasAlpha(header, plan.packet.frame_flags);
        slice->slice_index = slice_index;
        slice->slice_y = slice_y_begin;
        slice->luma_width = plan.aligned_width;
        slice->luma_height = slice_height;
        slice->chroma_width = plan.aligned_width / 2u;
        slice->chroma_height = slice_height / 2u;
        slice->luma_stride = plan.aligned_width;
        slice->chroma_stride = plan.aligned_width / 2u;
        slice->macroblock_cols = width_mb;
        slice->macroblock_rows = slice_rows;
        slice->macroblock_stride = width_mb;
        slice->bit_offset_begin = slice_start_bits;
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
        trace->has_alpha = Bink2EffectiveHasAlpha(header, plan.packet.frame_flags);
        trace->macroblock_cols = width_mb;
        trace->macroblock_rows = slice_rows;
        trace->bit_offset_begin = slice_start_bits;
        trace->entries.reserve((size_t)width_mb * slice_rows);
    }

    Bink2BitReader bits;
    if (!bits.Reset(packet.data(), packet.size())) return false;
    if (!bits.Skip_Bits(slice_start_bits)) return false;

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
        y_prev_cbp = 0;
        u_prev_cbp = 0;
        v_prev_cbp = 0;
        a_prev_cbp = 0;

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
            Bink2DecodedMacroblock macroblock = {};
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
                                         macroblock, &failure_reason)) {
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
            const Bink2IntraMacroblockProbe& probe = macroblock.probe;

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

bool DecodeSingleAcBlockMeasured(Bink2BitReader& bits,
                                 int32_t q,
                                 const uint16_t qmat[4][64],
                                 bool use_nonzero_upper_table)
{
    if (q < 0 || q >= kBink2MaxQuant) return false;

    const uint16_t* skip_codes = use_nonzero_upper_table ? kBink2gAcSkipCodes[1]
                                                         : kBink2gAcSkipCodes[0];
    const uint8_t* skip_bits = use_nonzero_upper_table ? kBink2gAcSkipBits[1]
                                                       : kBink2gAcSkipBits[0];
    const int32_t q_scale = 1 << (q >> 2);

    int32_t next = 0;
    int32_t idx = 1;
    while (idx < 64) {
        --next;
        if (next < 1) {
            uint32_t symbol = 0;
            if (!DecodeVlcLittleEndian(bits, skip_codes, skip_bits, 14u, symbol)) return false;
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
        (void)sign;
        (void)qmat;
        (void)q_scale;
        ++idx;
    }
    return true;
}

bool MeasureLumaPlaneAc(Bink2BitReader& bits,
                       uint32_t y_cbp,
                       int32_t q,
                       std::array<size_t, 16>& per_block_bits,
                       size_t& total_bits)
{
    per_block_bits.fill(0);
    total_bits = 0;
    for (uint32_t g = 0; g < 4u; ++g) {
        const uint32_t group_cbp = y_cbp >> (g * 4u);
        const bool nonzero_upper = (group_cbp & 0xffff0000u) != 0;
        for (uint32_t b = 0; b < 4u; ++b) {
            const uint32_t cbp_bit = (group_cbp >> b) & 1u;
            if (cbp_bit == 0) continue;
            const size_t before = bits.Bits_Read();
            if (!DecodeSingleAcBlockMeasured(bits, q, kBink2gLumaIntraQmat, nonzero_upper))
                return false;
            const size_t after = bits.Bits_Read();
            per_block_bits[g * 4u + b] = after - before;
            total_bits += after - before;
        }
    }
    return true;
}

bool MeasureChromaPlaneAc(Bink2BitReader& bits,
                         uint32_t cbp,
                         int32_t q,
                         std::array<size_t, 4>& per_block_bits,
                         size_t& total_bits)
{
    per_block_bits.fill(0);
    total_bits = 0;
    const bool nonzero_upper = (cbp & 0xffff0000u) != 0;
    for (uint32_t b = 0; b < 4u; ++b) {
        const uint32_t cbp_bit = (cbp >> b) & 1u;
        if (cbp_bit == 0) continue;
        const size_t before = bits.Bits_Read();
        if (!DecodeSingleAcBlockMeasured(bits, q, kBink2gChromaIntraQmat, nonzero_upper))
            return false;
        const size_t after = bits.Bits_Read();
        per_block_bits[b] = after - before;
        total_bits += after - before;
    }
    return true;
}

bool DecodeTargetMacroblockBudgetInterleaved(Bink2BitReader& bits,
                                             const Bink2Header& header,
                                             uint32_t frame_flags,
                                             uint32_t mb_col,
                                             uint32_t mb_row,
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
                                             Bink2MacroblockBitBudget& budget)
{
    budget = {};
    budget.mb_col = mb_col;
    budget.mb_row = mb_row;
    budget.flags = flags;
    budget.has_alpha = Bink2EffectiveHasAlpha(header, frame_flags);
    budget.bit_offset_before = bits.Bits_Read();

    size_t t = bits.Bits_Read();

    int32_t dq = 0;
    if (!DecodeDQ(bits, dq)) return false;
    budget.intra_q = ClipInt(PredictIntraQ(left_q, top_q, top_left_q, dq, flags),
                             0, kBink2MaxQuant - 1);
    budget.bits_dq = bits.Bits_Read() - t; t = bits.Bits_Read();

    std::array<int32_t, 16> y_dc = {};
    std::array<int32_t, 4>  u_dc = {};
    std::array<int32_t, 4>  v_dc = {};
    std::array<int32_t, 16> a_dc = {};

    // Y: CBP -> DC -> AC
    if (!DecodeCbpLuma(bits, frame_flags, y_prev_cbp, budget.y_cbp)) return false;
    budget.bits_y_cbp = bits.Bits_Read() - t; t = bits.Bits_Read();
    y_prev_cbp = budget.y_cbp;
    if (!DecodeDcValues(bits, budget.intra_q, true, 0, 2047, flags,
                        left_y_dc, top_y_dc, top_left_y_dc, y_dc)) return false;
    budget.bits_y_dc = bits.Bits_Read() - t; t = bits.Bits_Read();
    if (!MeasureLumaPlaneAc(bits, budget.y_cbp, budget.intra_q,
                            budget.bits_y_ac, budget.bits_y_ac_total)) return false;
    t = bits.Bits_Read();

    // U
    if (!DecodeCbpChroma(bits, u_prev_cbp, budget.u_cbp)) return false;
    budget.bits_u_cbp = bits.Bits_Read() - t; t = bits.Bits_Read();
    u_prev_cbp = budget.u_cbp;
    if (!DecodeDcValues(bits, budget.intra_q, false, 0, 2047, flags,
                        left_u_dc, top_u_dc, top_left_u_dc, u_dc)) return false;
    budget.bits_u_dc = bits.Bits_Read() - t; t = bits.Bits_Read();
    if (!MeasureChromaPlaneAc(bits, budget.u_cbp, budget.intra_q,
                              budget.bits_u_ac, budget.bits_u_ac_total)) return false;
    t = bits.Bits_Read();

    // V
    if (!DecodeCbpChroma(bits, v_prev_cbp, budget.v_cbp)) return false;
    budget.bits_v_cbp = bits.Bits_Read() - t; t = bits.Bits_Read();
    v_prev_cbp = budget.v_cbp;
    if (!DecodeDcValues(bits, budget.intra_q, false, 0, 2047, flags,
                        left_v_dc, top_v_dc, top_left_v_dc, v_dc)) return false;
    budget.bits_v_dc = bits.Bits_Read() - t; t = bits.Bits_Read();
    if (!MeasureChromaPlaneAc(bits, budget.v_cbp, budget.intra_q,
                              budget.bits_v_ac, budget.bits_v_ac_total)) return false;
    t = bits.Bits_Read();

    // A
    if (budget.has_alpha) {
        if (!DecodeCbpLuma(bits, frame_flags, a_prev_cbp, budget.a_cbp)) return false;
        budget.bits_a_cbp = bits.Bits_Read() - t; t = bits.Bits_Read();
        a_prev_cbp = budget.a_cbp;
        if (!DecodeDcValues(bits, budget.intra_q, true, 0, 2047, flags,
                            left_a_dc, top_a_dc, top_left_a_dc, a_dc)) return false;
        budget.bits_a_dc = bits.Bits_Read() - t; t = bits.Bits_Read();
        if (!MeasureLumaPlaneAc(bits, budget.a_cbp, budget.intra_q,
                                budget.bits_a_ac, budget.bits_a_ac_total)) return false;
    }

    budget.bit_offset_after = bits.Bits_Read();
    budget.bits_total = budget.bit_offset_after - budget.bit_offset_before;
    return true;
}

bool DecodeTargetMacroblockBudget(Bink2BitReader& bits,
                                  const Bink2Header& header,
                                  uint32_t frame_flags,
                                  uint32_t mb_col,
                                  uint32_t mb_row,
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
                                  Bink2MacroblockBitBudget& budget)
{
    budget = {};
    budget.mb_col = mb_col;
    budget.mb_row = mb_row;
    budget.flags = flags;
    budget.has_alpha = Bink2EffectiveHasAlpha(header, frame_flags);
    budget.bit_offset_before = bits.Bits_Read();

    size_t t0 = bits.Bits_Read();

    int32_t dq = 0;
    if (!DecodeDQ(bits, dq)) return false;
    budget.intra_q = ClipInt(PredictIntraQ(left_q, top_q, top_left_q, dq, flags),
                             0, kBink2MaxQuant - 1);
    size_t t1 = bits.Bits_Read();
    budget.bits_dq = t1 - t0;

    std::array<int32_t, 16> y_dc = {};
    std::array<int32_t, 4>  u_dc = {};
    std::array<int32_t, 4>  v_dc = {};
    std::array<int32_t, 16> a_dc = {};

    if (!DecodeCbpLuma(bits, frame_flags, y_prev_cbp, budget.y_cbp)) return false;
    size_t t2 = bits.Bits_Read();
    budget.bits_y_cbp = t2 - t1;
    y_prev_cbp = budget.y_cbp;
    if (!DecodeDcValues(bits, budget.intra_q, true, 0, 2047, flags,
                        left_y_dc, top_y_dc, top_left_y_dc, y_dc)) return false;
    size_t t3 = bits.Bits_Read();
    budget.bits_y_dc = t3 - t2;

    if (!DecodeCbpChroma(bits, u_prev_cbp, budget.u_cbp)) return false;
    size_t t4 = bits.Bits_Read();
    budget.bits_u_cbp = t4 - t3;
    u_prev_cbp = budget.u_cbp;
    if (!DecodeDcValues(bits, budget.intra_q, false, 0, 2047, flags,
                        left_u_dc, top_u_dc, top_left_u_dc, u_dc)) return false;
    size_t t5 = bits.Bits_Read();
    budget.bits_u_dc = t5 - t4;

    if (!DecodeCbpChroma(bits, v_prev_cbp, budget.v_cbp)) return false;
    size_t t6 = bits.Bits_Read();
    budget.bits_v_cbp = t6 - t5;
    v_prev_cbp = budget.v_cbp;
    if (!DecodeDcValues(bits, budget.intra_q, false, 0, 2047, flags,
                        left_v_dc, top_v_dc, top_left_v_dc, v_dc)) return false;
    size_t t7 = bits.Bits_Read();
    budget.bits_v_dc = t7 - t6;

    if (budget.has_alpha) {
        if (!DecodeCbpLuma(bits, frame_flags, a_prev_cbp, budget.a_cbp)) return false;
        size_t t8 = bits.Bits_Read();
        budget.bits_a_cbp = t8 - t7;
        a_prev_cbp = budget.a_cbp;
        if (!DecodeDcValues(bits, budget.intra_q, true, 0, 2047, flags,
                            left_a_dc, top_a_dc, top_left_a_dc, a_dc)) return false;
        size_t t9 = bits.Bits_Read();
        budget.bits_a_dc = t9 - t8;
    }

    if (!MeasureLumaPlaneAc(bits, budget.y_cbp, budget.intra_q,
                            budget.bits_y_ac, budget.bits_y_ac_total)) return false;
    if (!MeasureChromaPlaneAc(bits, budget.u_cbp, budget.intra_q,
                              budget.bits_u_ac, budget.bits_u_ac_total)) return false;
    if (!MeasureChromaPlaneAc(bits, budget.v_cbp, budget.intra_q,
                              budget.bits_v_ac, budget.bits_v_ac_total)) return false;
    if (budget.has_alpha) {
        if (!MeasureLumaPlaneAc(bits, budget.a_cbp, budget.intra_q,
                                budget.bits_a_ac, budget.bits_a_ac_total)) return false;
    }

    budget.bit_offset_after = bits.Bits_Read();
    budget.bits_total = budget.bit_offset_after - budget.bit_offset_before;
    (void)mb_col;
    (void)mb_row;
    return true;
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
    // RAD bink2 flag counts: one flag per 8-pixel boundary within the
    // 32-aligned luma grid, minus the rightmost/bottommost boundary that
    // coincides with the MB edge. Columns use Align32 (a half-MB right
    // edge e.g. width=720 still contributes its column boundaries up to
    // MB edge 736). Rows use Align16 — empirically verified against BP64
    // on BLACKOUT (720×396, Align32=416): stream length matches
    // (Align16(h)>>3)-1 = 49, not (Align32(h)>>3)-1 = 51.
    plan.row_flag_count = (Align16(header.height) >> 3) - 1u;
    plan.col_flag_count = (Align32(header.width) >> 3) - 1u;
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
    return WalkKeyframeSlicePrefix(header, plan, packet, 0u, &slice, nullptr);
}

bool Bink2DecodeFirstKeyframeFramePrefix(const Bink2Header& header,
                                         const Bink2FramePlan& plan,
                                         const std::vector<uint8_t>& packet,
                                         Bink2DecodedFrame& frame)
{
    frame = {};

    if (!header.Is_Valid()) return false;
    if (packet.empty()) return false;
    if (plan.packet.num_slices == 0u) return false;

    frame.has_alpha = header.Has_Alpha();
    frame.width = header.width;
    frame.height = header.height;
    frame.aligned_width = plan.aligned_width;
    frame.aligned_height = plan.aligned_height;
    frame.luma_stride = plan.aligned_width;
    frame.chroma_stride = plan.aligned_width / 2u;
    frame.macroblock_cols = plan.aligned_width / 32u;
    frame.macroblock_rows = plan.aligned_height / 32u;
    frame.macroblock_stride = frame.macroblock_cols;
    frame.decoded_macroblocks.assign((size_t)frame.macroblock_stride * frame.macroblock_rows, 0u);
    frame.y.assign((size_t)frame.luma_stride * frame.aligned_height, 16u);
    frame.u.assign((size_t)frame.chroma_stride * (frame.aligned_height / 2u), 128u);
    frame.v.assign((size_t)frame.chroma_stride * (frame.aligned_height / 2u), 128u);
    if (frame.has_alpha) {
        const uint8_t a_fill = Bink2AlphaFillValue(plan.packet.frame_flags);
        frame.a.assign((size_t)frame.luma_stride * frame.aligned_height, a_fill);
    }

    bool all_complete = true;
    for (uint32_t s = 0; s < plan.packet.num_slices; ++s) {
        Bink2DecodedKeyframeSlice slice;
        if (!WalkKeyframeSlicePrefix(header, plan, packet, s, &slice, nullptr)) {
            frame.decoded_slice_count = s;
            frame.macroblock_count += slice.macroblock_count;
            frame.nonzero_macroblock_count += slice.nonzero_macroblock_count;
            frame.decode_mb_col = slice.decode_mb_col;
            frame.decode_mb_row = slice.decode_mb_row;
            frame.failure_stage = slice.failure_stage;
            frame.bit_offset_begin = slice.bit_offset_begin;
            frame.bit_offset_end = slice.bit_offset_end;
            return false;
        }

        if (s == 0) {
            frame.bit_offset_begin = slice.bit_offset_begin;
        }
        frame.bit_offset_end = slice.bit_offset_end;
        frame.macroblock_count += slice.macroblock_count;
        frame.nonzero_macroblock_count += slice.nonzero_macroblock_count;
        all_complete = all_complete && slice.complete;

        if (!BlitPlaneRegion(frame.y.data(), frame.luma_stride,
                             frame.aligned_width, frame.aligned_height,
                             0u, slice.slice_y,
                             slice.y.data(), slice.luma_stride,
                             slice.luma_width, slice.luma_height)) {
            return false;
        }
        const uint32_t chroma_y = slice.slice_y / 2u;
        if (!BlitPlaneRegion(frame.u.data(), frame.chroma_stride,
                             frame.aligned_width / 2u, frame.aligned_height / 2u,
                             0u, chroma_y,
                             slice.u.data(), slice.chroma_stride,
                             slice.chroma_width, slice.chroma_height)) {
            return false;
        }
        if (!BlitPlaneRegion(frame.v.data(), frame.chroma_stride,
                             frame.aligned_width / 2u, frame.aligned_height / 2u,
                             0u, chroma_y,
                             slice.v.data(), slice.chroma_stride,
                             slice.chroma_width, slice.chroma_height)) {
            return false;
        }
        // When alpha-fill is active (frame_flags & 0x80000), the slice
        // doesn't decode alpha and slice.a is empty — frame.a keeps
        // the pre-fill value set in the initial allocation.
        if (frame.has_alpha && !slice.a.empty() &&
            !BlitPlaneRegion(frame.a.data(), frame.luma_stride,
                             frame.aligned_width, frame.aligned_height,
                             0u, slice.slice_y,
                             slice.a.data(), slice.luma_stride,
                             slice.luma_width, slice.luma_height)) {
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
    }

    frame.decoded_slice_count = plan.packet.num_slices;
    frame.complete = all_complete &&
                     frame.decoded_slice_count == plan.packet.num_slices;
    return true;
}

// ---- Inter-frame SKIP-prefix walker (Stage 2 minimum) -----------------------
//
// Decodes only the leading run of SKIP blocks in an inter frame: for each MB
// we parse its type (LRU-coded), and for SKIP we copy the corresponding 32×32
// (+16×16 chroma, +32×32 alpha) region from the previous frame. On the first
// non-SKIP block the walker stops and returns a partial frame.
//
// This is a Stage-2 milestone that validates type-LRU bit alignment and the
// previous-frame blit path. Full inter decode (MOTION/RESIDUE/INTRA handling,
// MV prediction, motion compensation, inter residuals) is Stage 3+.

enum Bink2InterBlockType {
    kBink2BlockIntra   = 0,
    kBink2BlockSkip    = 1,
    kBink2BlockMotion  = 2,
    kBink2BlockResidue = 3,
};

static bool DecodeInterBlockType(Bink2BitReader& bits, int lru[4], int& out_type)
{
    uint32_t u = 0;
    if (!bits.Read_Unary(1u, 3u, u)) return false;
    switch (u) {
    case 0: out_type = lru[0]; break;
    case 1: out_type = lru[1]; std::swap(lru[0], lru[1]); break;
    case 2: out_type = lru[3]; std::swap(lru[2], lru[3]); break;
    case 3: out_type = lru[2]; std::swap(lru[1], lru[2]); break;
    default: return false;
    }
    return true;
}

// 4-vector MV block (one (x,y) per 16x16 luma sub-block).
struct Bink2MvBlock {
    int16_t num_vectors = 0;   // 1 or 4
    int16_t v[4][2] = {};       // [sub-index][x/y]
};

// FFmpeg bink2g_decode_dq.
static bool DecodeInterDq(Bink2BitReader& bits, int32_t& dq)
{
    uint32_t u = 0;
    if (!bits.Read_Unary(1u, 4u, u)) return false;
    int32_t val = (int32_t)u;
    if (val == 3) {
        uint32_t b = 0;
        if (!bits.Read_Bit(b)) return false;
        val += (int32_t)b;
    } else if (val == 4) {
        uint32_t extra = 0;
        if (!bits.Read_Bits(5u, extra)) return false;
        val += (int32_t)extra + 1;
    }
    if (val != 0) {
        uint32_t sign = 0;
        if (!bits.Read_Bit(sign)) return false;
        if (sign) val = -val;
    }
    dq = val;
    return true;
}

// FFmpeg bink2g_decode_mv. Consumes 1 bit + (1 or 4) * 2 VLC symbols with
// optional escape extension; captures the decoded (delta) MV values.
static bool DecodeInterMv(Bink2BitReader& bits, Bink2MvBlock& out)
{
    uint32_t one_mv = 0;
    if (!bits.Read_Bit(one_mv)) return false;
    out.num_vectors = one_mv ? 1 : 4;
    for (uint32_t i = 0; i < 2u; ++i) {
        for (int j = 0; j < out.num_vectors; ++j) {
            uint32_t sym = 0;
            if (!DecodeVlcLittleEndian(bits, kBink2gMvCodes, kBink2gMvBits, 16u, sym)) return false;
            int32_t val = (int32_t)sym;
            if (val >= 8 && val != 15) val = val - 15;
            if (val == 15) {
                uint32_t u = 0;
                if (!bits.Read_Unary(1u, 12u, u)) return false;
                const uint32_t nb = u + 4u;
                uint32_t extra = 0;
                if (!bits.Read_Bits(nb, extra)) return false;
                int32_t big = (int32_t)extra + (int32_t)(1u << nb) - 1;
                if (big & 1) val = -(big >> 1) - 1;
                else         val =  big >> 1;
            }
            out.v[j][i] = (int16_t)val;
        }
    }
    return true;
}

// FFmpeg mid_pred — median of three.
static inline int32_t MidPred3(int32_t a, int32_t b, int32_t c)
{
    return a + b + c - std::max(a, std::max(b, c)) - std::min(a, std::min(b, c));
}

// FFmpeg bink2g_predict_mv. Converts the decoded delta MVs into absolute
// coordinates using neighbour MV predictors; writes results into `current`.
static void PredictMv(uint32_t mb_flags,
                      const Bink2MvBlock& left,       // current_mv[mb-1]
                      const Bink2MvBlock& top_left,   // prev_mv[mb-1]
                      const Bink2MvBlock& top,        // prev_mv[mb]
                      Bink2MvBlock& mv,               // in: delta, out: absolute
                      Bink2MvBlock& current)          // out: this MB
{
    auto mp = MidPred3;
    const bool has_top = (mb_flags & 0x80u) == 0;      // not first row → has top
    const bool has_left = (mb_flags & 0x20u) == 0;     // not first col → has left

    if (mv.num_vectors == 1) {
        if (!has_top) {
            if (has_left) {
                mv.v[0][0] += mp(left.v[0][0], left.v[1][0], left.v[3][0]);
                mv.v[0][1] += mp(left.v[0][1], left.v[1][1], left.v[3][1]);
            }
        } else {
            if (has_left) {
                mv.v[0][0] += mp(top_left.v[3][0], top.v[2][0], left.v[1][0]);
                mv.v[0][1] += mp(top_left.v[3][1], top.v[2][1], left.v[1][1]);
            } else {
                mv.v[0][0] += mp(top.v[0][0], top.v[2][0], top.v[3][0]);
                mv.v[0][1] += mp(top.v[0][1], top.v[2][1], top.v[3][1]);
            }
        }
        for (int k = 0; k < 4; ++k) {
            current.v[k][0] = mv.v[0][0];
            current.v[k][1] = mv.v[0][1];
        }
        current.num_vectors = 1;
        return;
    }

    current.num_vectors = 4;
    if (has_top) {
        if (!has_left) {
            current.v[0][0] = mv.v[0][0] + mp(top.v[0][0], top.v[2][0], top.v[3][0]);
            current.v[0][1] = mv.v[0][1] + mp(top.v[0][1], top.v[2][1], top.v[3][1]);
            current.v[1][0] = mv.v[1][0] + mp(top.v[2][0], top.v[3][0], current.v[0][0]);
            current.v[1][1] = mv.v[1][1] + mp(top.v[2][1], top.v[3][1], current.v[0][1]);
            current.v[2][0] = mv.v[2][0] + mp(top.v[2][0], current.v[0][0], current.v[1][0]);
            current.v[2][1] = mv.v[2][1] + mp(top.v[2][1], current.v[0][1], current.v[1][1]);
            current.v[3][0] = mv.v[3][0] + mp(current.v[0][0], current.v[1][0], current.v[2][0]);
            current.v[3][1] = mv.v[3][1] + mp(current.v[0][1], current.v[1][1], current.v[2][1]);
        } else {
            current.v[0][0] = mv.v[0][0] + mp(top.v[2][0], top_left.v[3][0], left.v[1][0]);
            current.v[0][1] = mv.v[0][1] + mp(top.v[2][1], top_left.v[3][1], left.v[1][1]);
            current.v[1][0] = mv.v[1][0] + mp(top.v[2][0], top.v[3][0],     current.v[0][0]);
            current.v[1][1] = mv.v[1][1] + mp(top.v[2][1], top.v[3][1],     current.v[0][1]);
            current.v[2][0] = mv.v[2][0] + mp(left.v[1][0], left.v[3][0],   current.v[0][0]);
            current.v[2][1] = mv.v[2][1] + mp(left.v[1][1], left.v[3][1],   current.v[0][1]);
            current.v[3][0] = mv.v[3][0] + mp(current.v[0][0], current.v[1][0], current.v[2][0]);
            current.v[3][1] = mv.v[3][1] + mp(current.v[0][1], current.v[1][1], current.v[2][1]);
        }
    } else {
        if (!has_left) {
            current.v[0][0] = mv.v[0][0];
            current.v[0][1] = mv.v[0][1];
            current.v[1][0] = mv.v[1][0] + mv.v[0][0];
            current.v[1][1] = mv.v[1][1] + mv.v[0][1];
            current.v[2][0] = mv.v[2][0] + mv.v[0][0];
            current.v[2][1] = mv.v[2][1] + mv.v[0][1];
            current.v[3][0] = mv.v[3][0] + mp(current.v[0][0], current.v[1][0], current.v[2][0]);
            current.v[3][1] = mv.v[3][1] + mp(current.v[0][1], current.v[1][1], current.v[2][1]);
        } else {
            current.v[0][0] = mv.v[0][0] + mp(left.v[0][0], left.v[1][0], left.v[3][0]);
            current.v[0][1] = mv.v[0][1] + mp(left.v[0][1], left.v[1][1], left.v[3][1]);
            current.v[2][0] = mv.v[2][0] + mp(left.v[1][0], left.v[3][0], current.v[0][0]);
            current.v[2][1] = mv.v[2][1] + mp(left.v[1][1], left.v[3][1], current.v[0][1]);
            current.v[1][0] = mv.v[1][0] + mp(left.v[1][0], current.v[0][0], current.v[2][0]);
            current.v[1][1] = mv.v[1][1] + mp(left.v[1][1], current.v[0][1], current.v[2][1]);
            current.v[3][0] = mv.v[3][0] + mp(current.v[0][0], current.v[1][0], current.v[2][0]);
            current.v[3][1] = mv.v[3][1] + mp(current.v[0][1], current.v[1][1], current.v[2][1]);
        }
    }
}

// ---- Motion compensation -------------------------------------------------
// Chroma 1/4-pel filters (FFmpeg CH1/CH2/CH3 + CV1/CV2/CV3).
// Chroma MC filter rounding modes. The quarter-pel `(sum+4)>>3` constant that
// matches the FFmpeg/NihAV public decoders has +0.125/pixel bias on uniform
// integer inputs, which accumulates into the "magenta bleed" class drift on
// motion-heavy GDI* content (landed 2026-04-19 after triage on GDI13 +
// TANKKILL validation). Default is now round-half-to-even on quarter-pel,
// which cuts drift by ~5×. Half-pel stays round-half-up by default — flipping
// it independently over-corrects to green (empirically confirmed in h0q2 vs
// h1q2 sweep); there is a residual small green drift on some samples that is
// still open. Env vars allow flipping each filter independently for debugging:
//   Halfpel  (a+b)/2:
//     0 (default): (a+b+1)>>1     round-half-up,     +0.25/px bias
//     1:           (a+b)>>1        round-half-down,   -0.25/px bias
//     2:           round-half-even, unbiased
//   Quarterpel (6a+2b)/8 or (2a+6b)/8:
//     0:           (sum+4)>>3      round-half-up,     +0.125/px bias  (legacy)
//     1:           (sum+3)>>3      round-half-down,   -0.125/px bias
//     2 (default): round-half-even, unbiased
static int Bk2McHalfpelRoundMode()
{
    static const int mode = []{
        const char* e = std::getenv("BK2_MC_HALFPEL_ROUND");
        if (!e || !e[0]) return 0;
        return std::atoi(e);
    }();
    return mode;
}
static int Bk2McQuarterpelRoundMode()
{
    static const int mode = []{
        const char* e = std::getenv("BK2_MC_QUARTERPEL_ROUND");
        if (!e || !e[0]) return 0;
        return std::atoi(e);
    }();
    return mode;
}
static inline int32_t HalfPelAvg(int a, int b) {
    const int mode = Bk2McHalfpelRoundMode();
    if (mode == 1) return (a + b) >> 1;
    if (mode == 2) {
        const int sum = a + b;
        return (sum + ((sum >> 1) & 1)) >> 1;
    }
    return (a + b + 1) >> 1;
}
static inline int32_t QuarterPelAvg(int sum) {
    const int mode = Bk2McQuarterpelRoundMode();
    if (mode == 1) return (sum + 3) >> 3;
    if (mode == 2) return (sum + 3 + ((sum >> 3) & 1)) >> 3;
    return (sum + 4) >> 3;
}
// BK2_MC_2D_U16_TEMP=1 switches the 2D separable chroma MC path to keep the
// H-pass intermediate at full precision (no per-pass rounding/clipping) and
// apply a single combined shift+clip at V-pass end. See NihAV
// (`nihav-rad/src/codecs/bink2.rs:352-386`) — it does this and drifts in the
// opposite direction on GDI* than us + FFmpeg, both of which do double
// rounding on u8 temp. Gate lets us A/B whether the double-round is the
// structural cause of the residual green drift on h0q2 default.
static inline bool Bk2Mc2dU16Temp() {
    static const bool on = []{
        const char* e = std::getenv("BK2_MC_2D_U16_TEMP");
        if (!e || !e[0]) return true;
        return e[0] != '0';
    }();
    return on;
}
// BK2_MC_LUMA_2D_I16_TEMP=1 switches 2D separable luma MC (mode==3) to keep
// the H-pass numerator at full precision in int16 temp (no internal >>1,
// no round, no clip) and apply one combined (+512)>>10 + Clip255 at V-end.
// The legacy path uses u8 temp with two floor+clip rounds, which biases
// +0.5..+1 LSB per call and accumulates on sub-pel MVs — see SOVIET9
// f380..f450 walkback, the luma analogue of the chroma Bk2Mc2dU16Temp fix.
// Default OFF while under A/B validation.
static inline bool Bk2McLuma2dI16Temp() {
    static const bool on = []{
        const char* e = std::getenv("BK2_MC_LUMA_2D_I16_TEMP");
        if (!e || !e[0]) return false;
        return e[0] != '0';
    }();
    return on;
}
// MC sum-parity histogram. Populated by ChH/ChV when BK2_REPORT_MC_PARITY=1
// is set; dumped by the dtor below. The quarter-pel sum 6a+2b is always
// even, so only buckets {0,2,4,6} see non-zero counts — residents of
// bucket 4 are the half-even tie-breaks (they are the only pixels whose
// result depends on the rounding rule). A skewed distribution in bucket
// counts between samples is the mechanism that makes h0q2 residual-drift
// asymmetric across content (explains why AFRICA stays byte-identical
// while GDI13 drifts green with the same filter).
struct Bink2McParityCounters {
    uint64_t q_sum_hist[8]{};       // 6a+2b (or 2a+6b) mod 8
    uint64_t q_sum_hist_leading[8]{}; // 6a+2b (a-weighted, n=1 direction)
    uint64_t q_sum_hist_trailing[8]{}; // 2a+6b (b-weighted, n=3 direction)
    uint64_t hp_sum_parity[2]{};    // (a+b) & 1
    uint64_t quarterpel_calls = 0;
    uint64_t halfpel_calls = 0;
    ~Bink2McParityCounters() {
        if (const char* e = std::getenv("BK2_REPORT_MC_PARITY");
            !(e && e[0] && e[0] != '0')) return;
        if (!quarterpel_calls && !halfpel_calls) return;
        std::fprintf(stderr,
            "[BK2 mc parity] quarterpel_calls=%llu halfpel_calls=%llu\n",
            (unsigned long long)quarterpel_calls,
            (unsigned long long)halfpel_calls);
        const double q = quarterpel_calls ? (double)quarterpel_calls : 1.0;
        std::fprintf(stderr, "  quarterpel (6a+2b or 2a+6b) mod 8 histogram:\n");
        for (int k = 0; k < 8; ++k) {
            std::fprintf(stderr, "    mod=%d  count=%12llu  frac=%.4f\n",
                k, (unsigned long long)q_sum_hist[k],
                (double)q_sum_hist[k] / q);
        }
        std::fprintf(stderr, "  leading-direction (n=1, 6a+2b) mod 8:\n");
        for (int k = 0; k < 8; ++k) {
            if (q_sum_hist_leading[k])
                std::fprintf(stderr, "    mod=%d  count=%12llu\n",
                    k, (unsigned long long)q_sum_hist_leading[k]);
        }
        std::fprintf(stderr, "  trailing-direction (n=3, 2a+6b) mod 8:\n");
        for (int k = 0; k < 8; ++k) {
            if (q_sum_hist_trailing[k])
                std::fprintf(stderr, "    mod=%d  count=%12llu\n",
                    k, (unsigned long long)q_sum_hist_trailing[k]);
        }
        if (halfpel_calls) {
            std::fprintf(stderr, "  halfpel (a+b) parity: even=%llu odd=%llu frac_odd=%.4f\n",
                (unsigned long long)hp_sum_parity[0],
                (unsigned long long)hp_sum_parity[1],
                (double)hp_sum_parity[1] / (double)halfpel_calls);
        }
    }
};
static Bink2McParityCounters g_mc_parity;
static inline bool Bk2McParityEnabled() {
    static const bool on = []{
        const char* e = std::getenv("BK2_REPORT_MC_PARITY");
        return e && e[0] && e[0] != '0';
    }();
    return on;
}
static inline int32_t ChH(int n, const uint8_t* s, int i) {
    if (n == 1) {
        const int sum = 6*s[i+0] + 2*s[i+1];
        if (Bk2McParityEnabled()) {
            ++g_mc_parity.quarterpel_calls;
            ++g_mc_parity.q_sum_hist[sum & 7];
            ++g_mc_parity.q_sum_hist_leading[sum & 7];
        }
        return QuarterPelAvg(sum);
    }
    if (n == 2) {
        if (Bk2McParityEnabled()) {
            ++g_mc_parity.halfpel_calls;
            ++g_mc_parity.hp_sum_parity[(s[i+0] + s[i+1]) & 1];
        }
        return HalfPelAvg(s[i+0], s[i+1]);
    }
    const int sum = 2*s[i+0] + 6*s[i+1];
    if (Bk2McParityEnabled()) {
        ++g_mc_parity.quarterpel_calls;
        ++g_mc_parity.q_sum_hist[sum & 7];
        ++g_mc_parity.q_sum_hist_trailing[sum & 7];
    }
    return QuarterPelAvg(sum);
}
static inline int32_t ChV(int n, const uint8_t* s, int stride) {
    if (n == 1) {
        const int sum = 6*s[0] + 2*s[stride];
        if (Bk2McParityEnabled()) {
            ++g_mc_parity.quarterpel_calls;
            ++g_mc_parity.q_sum_hist[sum & 7];
            ++g_mc_parity.q_sum_hist_leading[sum & 7];
        }
        return QuarterPelAvg(sum);
    }
    if (n == 2) {
        if (Bk2McParityEnabled()) {
            ++g_mc_parity.halfpel_calls;
            ++g_mc_parity.hp_sum_parity[(s[0] + s[stride]) & 1];
        }
        return HalfPelAvg(s[0], s[stride]);
    }
    const int sum = 2*s[0] + 6*s[stride];
    if (Bk2McParityEnabled()) {
        ++g_mc_parity.quarterpel_calls;
        ++g_mc_parity.q_sum_hist[sum & 7];
        ++g_mc_parity.q_sum_hist_trailing[sum & 7];
    }
    return QuarterPelAvg(sum);
}
static inline uint8_t Clip255(int32_t v) {
    return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
}

// MV-edge instrumentation for ChromaMc8. Env gate BK2_REPORT_MC_EDGE=1.
// Per handoff 2026-04-19 (late evening): residual -0.65 dU chroma drift on
// GDI13 isn't scalar-rounding or structural-u16. Next hypothesis is MV
// edge-handling: the early-return at `mv_x >= width` discards fully-OOB MVs
// but doesn't clamp near-edge MVs where the +1 read-ahead (q-pel H) or
// 9-row 2D temp (q-pel V) extends past the buffer. Count how many MC calls
// fall into each bucket to decide whether edge paths are a meaningful
// share of motion before touching code.
struct Bink2McEdgeCounters {
    uint64_t calls = 0;          // total ChromaMc8 calls
    uint64_t fully_oob = 0;      // current early-return path
    uint64_t in_bounds = 0;      // mv within [0,w)x[0,h), read-ahead also in-bounds
    uint64_t near_right = 0;     // mv_x+ead >= width (read past right edge)
    uint64_t near_bottom = 0;    // mv_y+ead >= height (read past bottom edge)
    uint64_t near_both = 0;      // both right and bottom overflow
    uint64_t pure_copy = 0;      // mode 0/0: no filter, 8x8 memcpy only
    uint64_t hfilter = 0;        // h!=0, v==0 (horizontal only, 8 rows)
    uint64_t vfilter = 0;        // h==0, v!=0 (vertical only, 9 rows)
    uint64_t twoD = 0;           // h!=0 && v!=0 (2D, 9 rows)
    uint64_t near_edge_pure = 0; // near-edge in pure-copy path
    uint64_t near_edge_h = 0;    // near-edge in H-filter path
    uint64_t near_edge_v = 0;    // near-edge in V-filter path
    uint64_t near_edge_2d = 0;   // near-edge in 2D path (primary concern)
    ~Bink2McEdgeCounters() {
        if (const char* e = std::getenv("BK2_REPORT_MC_EDGE");
            !(e && e[0] && e[0] != '0')) return;
        if (!calls) return;
        const double c = (double)calls;
        std::fprintf(stderr,
            "[BK2 mc edge] calls=%llu fully_oob=%llu (%.2f%%) "
            "in_bounds=%llu near_right=%llu near_bottom=%llu near_both=%llu\n",
            (unsigned long long)calls,
            (unsigned long long)fully_oob, 100.0*fully_oob/c,
            (unsigned long long)in_bounds,
            (unsigned long long)near_right,
            (unsigned long long)near_bottom,
            (unsigned long long)near_both);
        const uint64_t near_total = near_right + near_bottom + near_both;
        const uint64_t filtered = calls - fully_oob;
        std::fprintf(stderr,
            "  near_edge_total=%llu (%.2f%% of all calls, %.2f%% of non-oob)\n",
            (unsigned long long)near_total,
            100.0*near_total/c,
            filtered ? 100.0*near_total/(double)filtered : 0.0);
        std::fprintf(stderr,
            "  path  pure=%llu  h=%llu  v=%llu  2d=%llu\n",
            (unsigned long long)pure_copy,
            (unsigned long long)hfilter,
            (unsigned long long)vfilter,
            (unsigned long long)twoD);
        std::fprintf(stderr,
            "  near  pure=%llu  h=%llu  v=%llu  2d=%llu\n",
            (unsigned long long)near_edge_pure,
            (unsigned long long)near_edge_h,
            (unsigned long long)near_edge_v,
            (unsigned long long)near_edge_2d);
    }
};
static Bink2McEdgeCounters g_mc_edge;
static inline bool Bk2McEdgeEnabled() {
    static const bool on = []{
        const char* e = std::getenv("BK2_REPORT_MC_EDGE");
        return e && e[0] && e[0] != '0';
    }();
    return on;
}
// Clamp-MV experiment. Default: off (preserve early-return behaviour).
// When on, fully-OOB or near-edge MVs no longer early-return; instead,
// a 9x9 edge-replicated source tile is materialised and the filter
// runs against that. Hypothesis: GDI13's 57,580 fully-OOB calls leave
// dst stale (prev-frame residue or undefined) which accumulates into
// the residual -0.65 dU chroma drift; AFRICA has 0 OOB calls so is
// unaffected. See 2026-04-19 late-evening-2 memory.
static inline bool Bk2McClampMv() {
    static const bool on = []{
        const char* e = std::getenv("BK2_MC_CLAMP_MV");
        return e && e[0] && e[0] != '0';
    }();
    return on;
}

static void ChromaMc8(uint8_t* dst, int stride,
                      const uint8_t* src, int sstride,
                      int width, int height,
                      int mv_x, int mv_y, int mode)
{
    const int h_mode = mode & 3;
    const int v_mode = (mode >> 2) & 3;
    if (Bk2McEdgeEnabled()) {
        ++g_mc_edge.calls;
        // Read extent (max byte offset relative to msrc top-left). q-pel
        // axes touch +1 ahead; 2D V-pass walks through 9 rows of temp.
        // Pure-copy reads 8 samples x 8 rows; H-only 9 x 8; V-only 8 x 9;
        // 2D 9 x 9.
        const int x_ahead = (h_mode != 0 || v_mode != 0) ? 8 : 7;
        const int y_ahead = (v_mode != 0 || h_mode != 0) ? 8 : 7;
        // Actually: H-only has 8-row extent; V-only 9-row extent.
        // Keep both dimensions' read-ahead conservative (8): any extent
        // past width/height is an overshoot.
        const bool oob = (mv_x < 0 || mv_x >= width ||
                          mv_y < 0 || mv_y >= height);
        if (oob) {
            ++g_mc_edge.fully_oob;
        } else {
            const bool over_r = (mv_x + x_ahead >= width);
            const bool over_b = (mv_y + y_ahead >= height);
            if (over_r && over_b)       ++g_mc_edge.near_both;
            else if (over_r)            ++g_mc_edge.near_right;
            else if (over_b)            ++g_mc_edge.near_bottom;
            else                        ++g_mc_edge.in_bounds;
            const bool near = (over_r || over_b);
            if (h_mode == 0 && v_mode == 0) {
                ++g_mc_edge.pure_copy;
                if (near) ++g_mc_edge.near_edge_pure;
            } else if (v_mode == 0) {
                ++g_mc_edge.hfilter;
                if (near) ++g_mc_edge.near_edge_h;
            } else if (h_mode == 0) {
                ++g_mc_edge.vfilter;
                if (near) ++g_mc_edge.near_edge_v;
            } else {
                ++g_mc_edge.twoD;
                if (near) ++g_mc_edge.near_edge_2d;
            }
        }
    }
    const int h = mode & 3;         // horizontal 1/4-pel sub-mode (0..3)
    const int v = (mode >> 2) & 3;  // vertical   1/4-pel sub-mode (0..3)

    const uint8_t* msrc;
    int msrc_stride;
    uint8_t clamp_tile[9 * 9];
    if (Bk2McClampMv()) {
        // Worst-case filter footprint is 9x9 (2D q-pel). Build an
        // edge-clamped 9x9 tile starting at (mv_x, mv_y). For pure-copy
        // and 1D filter modes this is slightly more work than needed
        // but the extra 8-17 bytes of clamped fetch are negligible next
        // to the drift-correlation signal we're testing.
        for (int j = 0; j < 9; ++j) {
            int sy = mv_y + j;
            if (sy < 0) sy = 0;
            else if (sy >= height) sy = height - 1;
            const uint8_t* row = src + sy * sstride;
            for (int i = 0; i < 9; ++i) {
                int sx = mv_x + i;
                if (sx < 0) sx = 0;
                else if (sx >= width) sx = width - 1;
                clamp_tile[j * 9 + i] = row[sx];
            }
        }
        msrc = clamp_tile;
        msrc_stride = 9;
    } else {
        if (mv_x < 0 || mv_x >= width || mv_y < 0 || mv_y >= height) return;
        msrc = src + mv_x + mv_y * sstride;
        msrc_stride = sstride;
    }

    if (h == 0 && v == 0) {
        for (int j = 0; j < 8; ++j) {
            std::memcpy(dst + j*stride, msrc + j*msrc_stride, 8);
        }
        return;
    }
    if (v == 0) {
        // horizontal-only filter
        for (int j = 0; j < 8; ++j) {
            for (int i = 0; i < 8; ++i) dst[i] = Clip255(ChH(h, msrc, i));
            dst  += stride;
            msrc += msrc_stride;
        }
        return;
    }
    if (h == 0) {
        // vertical-only filter
        for (int j = 0; j < 8; ++j) {
            for (int i = 0; i < 8; ++i) dst[i*stride] = Clip255(ChV(v, msrc + i*msrc_stride, msrc_stride));
            dst  += 1;
            msrc += 1;
        }
        return;
    }
    if (Bk2Mc2dU16Temp()) {
        // Raw-sum u16 temp: store H-pass numerator without /denom/clip.
        // n=1 -> 6a+2b (max 6*255+2*255=2040, fits u16)
        // n=2 -> a+b   (max 510)
        // n=3 -> 2a+6b (max 2040)
        // Then V pass forms v-combination of those and applies a single
        // combined shift+round+clip. Denominators: q-pel axes are /8,
        // half-pel axis /2. Combined shift = log2(h_den * v_den).
        uint16_t temp[9 * 8];
        auto h_raw = [&](const uint8_t* s, int i) -> int32_t {
            if (h == 1) return 6 * s[i+0] + 2 * s[i+1];
            if (h == 2) return     s[i+0] +     s[i+1];
            return             2 * s[i+0] + 6 * s[i+1];
        };
        for (int i = 0; i < 9; ++i) {
            for (int j = 0; j < 8; ++j) temp[i*8+j] = (uint16_t)h_raw(msrc, j);
            msrc += msrc_stride;
        }
        // Combined denom.
        const int h_den = (h == 2) ? 2 : 8;
        const int v_den = (v == 2) ? 2 : 8;
        const int combined = h_den * v_den;            // 4/16/64
        const int shift = (combined == 64) ? 6 : (combined == 16 ? 4 : 2);
        const int rounder = 1 << (shift - 1);
        const int halfpel_mode = Bk2McHalfpelRoundMode();
        const int quarterpel_mode = Bk2McQuarterpelRoundMode();
        // Pick rounding mode aligned with the more-critical axis (q-pel if
        // present, else half-pel). Half-even matches h0q2 default intent.
        const int round_mode = (h_den == 8 || v_den == 8)
            ? quarterpel_mode
            : halfpel_mode;
        auto combine = [&](int32_t a, int32_t b) -> int32_t {
            if (v == 1) return 6 * a + 2 * b;
            if (v == 2) return     a +     b;
            return             2 * a + 6 * b;
        };
        for (int j = 0; j < 8; ++j) {
            for (int i = 0; i < 8; ++i) {
                const int32_t s = combine((int32_t)temp[j*8 + i],
                                          (int32_t)temp[(j+1)*8 + i]);
                int32_t out;
                if (round_mode == 1) {
                    out = (s + rounder - 1) >> shift;       // half-down
                } else if (round_mode == 2) {
                    out = (s + rounder - 1 + ((s >> shift) & 1)) >> shift; // half-even
                } else {
                    out = (s + rounder) >> shift;           // half-up
                }
                dst[i] = Clip255(out);
            }
            dst += stride;
        }
        return;
    }
    // 2D separable: horizontal pass into temp(9x8), then vertical pass.
    uint8_t temp[9 * 8];
    for (int i = 0; i < 9; ++i) {
        for (int j = 0; j < 8; ++j) temp[i*8+j] = Clip255(ChH(h, msrc, j));
        msrc += msrc_stride;
    }
    for (int j = 0; j < 8; ++j) {
        for (int i = 0; i < 8; ++i) dst[i] = Clip255(ChV(v, temp + j*8 + i, 8));
        dst += stride;
    }
}

static void ChromaMcMacroblock(const Bink2MvBlock& mv,
                               int x, int y,
                               uint8_t* dst, int stride,
                               const uint8_t* src, int sstride,
                               int width, int height)
{
    for (int k = 0; k < 4; ++k) {
        const int sub_x = (k & 1) * 8;
        const int sub_y = (k >> 1) * 8;
        const int mv_x = (mv.v[k][0] >> 2) + x + sub_x;
        const int mv_y = (mv.v[k][1] >> 2) + y + sub_y;
        const int mode = (mv.v[k][0] & 3) | ((mv.v[k][1] & 3) << 2);
        ChromaMc8(dst + x + sub_x + (y + sub_y) * stride, stride,
                  src, sstride, width, height, mv_x, mv_y, mode);
    }
}

// Luma 6-tap 1/2-pel filters.
static inline int32_t LumaH(const uint8_t* s, int i) {
    return ((((int32_t)s[i]+s[i+1])*19 >> 1)
            - ((int32_t)s[i-1]+s[i+2])*2
            + (((int32_t)s[i-2]+s[i+3]) >> 1) + 8) >> 4;
}
static inline int32_t LumaV(const uint8_t* s, int stride) {
    return ((((int32_t)s[0]+s[stride])*19 >> 1)
            - ((int32_t)s[-stride]+s[2*stride])*2
            + (((int32_t)s[-2*stride]+s[3*stride]) >> 1) + 8) >> 4;
}

// M9.1 diag: defined near Bk2DumpLumaInit; forward-declared for LumaMc16.
static int g_bk2_luma_mc_skip_mode[4];

static void LumaMc16(uint8_t* dst, int stride,
                     const uint8_t* src, int sstride,
                     int width, int height,
                     int mv_x, int mv_y, int mode)
{
    if (mv_x < 0 || mv_x >= width || mv_y < 0 || mv_y >= height) return;
    // M9.1 diag: optionally substitute integer MC for a specific sub-pel mode
    // to isolate which mode produces MC-path drift. Env-gated via
    // BK2_LUMA_MC_SKIP_MODE{1,2,3}; defaults are 0 (no-op).
    if (mode >= 1 && mode <= 3 && g_bk2_luma_mc_skip_mode[mode]) mode = 0;
    const uint8_t* msrc = src + mv_x + mv_y * sstride;

    if (mode == 0) {
        for (int j = 0; j < 16; ++j)
            std::memcpy(dst + j*stride, msrc + j*sstride, 16);
    } else if (mode == 1) {
        for (int j = 0; j < 16; ++j) {
            for (int i = 0; i < 16; ++i) dst[i] = Clip255(LumaH(msrc, i));
            dst  += stride;
            msrc += sstride;
        }
    } else if (mode == 2) {
        for (int j = 0; j < 16; ++j) {
            for (int i = 0; i < 16; ++i) dst[i*stride] = Clip255(LumaV(msrc + i*sstride, sstride));
            dst  += 1;
            msrc += 1;
        }
    } else if (Bk2McLuma2dI16Temp()) {
        // Full-precision H-pass numerator: 19*(a+b) - 4*(c+d) + (e+f).
        // Equivalent to LumaH without the two internal floor-shifts and the
        // final +8>>4 round+clip. Range fits int16: [-2040, 10200].
        // V-pass combines on int16 with the same 6-tap weights; single
        // combined shift+round+clip at the end: (sum + 512) >> 10.
        int16_t temp[21 * 16];
        msrc -= 2 * sstride;
        for (int i = 0; i < 21; ++i) {
            for (int j = 0; j < 16; ++j) {
                const int32_t a = msrc[j] + msrc[j+1];
                const int32_t c = msrc[j-1] + msrc[j+2];
                const int32_t e = msrc[j-2] + msrc[j+3];
                temp[i*16 + j] = (int16_t)(19*a - 4*c + e);
            }
            msrc += sstride;
        }
        for (int j = 0; j < 16; ++j) {
            const int16_t* row = temp + (j+2)*16;
            for (int i = 0; i < 16; ++i) {
                const int32_t a = (int32_t)row[i]       + row[i +  16];
                const int32_t c = (int32_t)row[i - 16]  + row[i + 2*16];
                const int32_t e = (int32_t)row[i - 2*16]+ row[i + 3*16];
                const int32_t s = 19*a - 4*c + e;
                dst[i] = Clip255((s + 512) >> 10);
            }
            dst += stride;
        }
    } else {
        uint8_t temp[21 * 16];
        msrc -= 2 * sstride;
        for (int i = 0; i < 21; ++i) {
            for (int j = 0; j < 16; ++j) temp[i*16+j] = Clip255(LumaH(msrc, j));
            msrc += sstride;
        }
        for (int j = 0; j < 16; ++j) {
            for (int i = 0; i < 16; ++i) dst[i] = Clip255(LumaV(temp + (j+2)*16 + i, 16));
            dst += stride;
        }
    }
}

static void LumaMcMacroblock(const Bink2MvBlock& mv,
                             int x, int y,
                             uint8_t* dst, int stride,
                             const uint8_t* src, int sstride,
                             int width, int height)
{
    for (int k = 0; k < 4; ++k) {
        const int sub_x = (k & 1) * 16;
        const int sub_y = (k >> 1) * 16;
        const int mv_x = (mv.v[k][0] >> 1) + x + sub_x;
        const int mv_y = (mv.v[k][1] >> 1) + y + sub_y;
        const int mode = (mv.v[k][0] & 1) | ((mv.v[k][1] & 1) << 1);
        LumaMc16(dst + x + sub_x + (y + sub_y) * stride, stride,
                 src, sstride, width, height, mv_x, mv_y, mode);
    }
}

// Diagnostic counters for chroma IDCT-add saturation events. Updated only
// when Bink2gIdctAdd is called via the chroma residual path (see
// DecodeAndAddInterChromaPlane). Snapshot+reset around the per-frame outer
// decode and print at process exit.
//
// `by_dc` buckets per-pixel residuals by |DC| of the originating 8x8 block:
// 0 = DC==0, 1 = 1..31, 2 = 32..127, 3 = 128..511, 4 = 512+.
// `by_ac` buckets by max |AC| of the block (AC = all block[] except [0]):
// 0 = max==0, 1 = 1..15, 2 = 16..63, 3 = 64..255, 4 = 256+.
// `by_q` buckets by the block's inter q: 0..9, 10..19, 20..29, 30..36.
struct Bink2ChromaSatBucket {
    uint64_t pixels = 0;
    int64_t  sum = 0;
};
struct Bink2ChromaSatCounters {
    uint64_t low = 0;   // pixels clamped at 0 (sum < 0)
    uint64_t high = 0;  // pixels clamped at 255 (sum > 255)
    uint64_t pixels = 0; // total pixels seen
    int64_t  sum_residual = 0; // signed sum of all chroma IDCT residuals
    Bink2ChromaSatBucket by_dc[5];
    Bink2ChromaSatBucket by_ac[5];
    Bink2ChromaSatBucket by_q[4];
    // Decoded-DC aggregates (pre-IDCT, pre-bias): helps decide whether the
    // residual green bias comes from the IDCT path (mean_dc==0) or from
    // biased DC decode (mean_dc!=0).
    uint64_t dc_blocks = 0;
    int64_t  sum_dc = 0;    // signed sum of raw dc[j] values
    int64_t  sum_abs_dc = 0; // sum of |dc[j]|
    ~Bink2ChromaSatCounters() {
        if (const char* e = std::getenv("BK2_REPORT_CHROMA_SAT");
            e && e[0] && e[0] != '0' && pixels) {
            std::fprintf(stderr,
                "[BK2 chroma sat] pixels=%llu low=%llu high=%llu (asymmetry=%+lld) sum_residual=%+lld mean_resid=%+.4f\n",
                (unsigned long long)pixels,
                (unsigned long long)low,
                (unsigned long long)high,
                (long long)((int64_t)high - (int64_t)low),
                (long long)sum_residual,
                (double)sum_residual / (double)pixels);
            static const char* dc_names[5] = {"DC=0    ", "DC=1-31 ", "DC=32-127", "DC=128-511", "DC=512+  "};
            static const char* ac_names[5] = {"|AC|=0   ", "|AC|=1-15", "|AC|=16-63", "|AC|=64-255", "|AC|=256+ "};
            static const char* q_names[4] = {"q=0-9  ", "q=10-19", "q=20-29", "q=30-36"};
            for (int i = 0; i < 5; ++i) {
                const double m = by_dc[i].pixels ? (double)by_dc[i].sum / (double)by_dc[i].pixels : 0.0;
                std::fprintf(stderr, "  %s  pixels=%10llu  mean=%+.4f  sum=%+lld\n",
                    dc_names[i], (unsigned long long)by_dc[i].pixels, m, (long long)by_dc[i].sum);
            }
            for (int i = 0; i < 5; ++i) {
                const double m = by_ac[i].pixels ? (double)by_ac[i].sum / (double)by_ac[i].pixels : 0.0;
                std::fprintf(stderr, "  %s  pixels=%10llu  mean=%+.4f  sum=%+lld\n",
                    ac_names[i], (unsigned long long)by_ac[i].pixels, m, (long long)by_ac[i].sum);
            }
            for (int i = 0; i < 4; ++i) {
                const double m = by_q[i].pixels ? (double)by_q[i].sum / (double)by_q[i].pixels : 0.0;
                std::fprintf(stderr, "  %s  pixels=%10llu  mean=%+.4f  sum=%+lld\n",
                    q_names[i], (unsigned long long)by_q[i].pixels, m, (long long)by_q[i].sum);
            }
            if (dc_blocks) {
                std::fprintf(stderr, "  decoded_dc: blocks=%llu mean=%+.4f mean_abs=%+.4f\n",
                    (unsigned long long)dc_blocks,
                    (double)sum_dc / (double)dc_blocks,
                    (double)sum_abs_dc / (double)dc_blocks);
            }
        }
    }
};
static Bink2ChromaSatCounters g_chroma_sat;

static inline int Bk2DcBucket(int32_t dc) {
    const int32_t a = dc < 0 ? -dc : dc;
    if (a == 0) return 0;
    if (a < 32) return 1;
    if (a < 128) return 2;
    if (a < 512) return 3;
    return 4;
}
static inline int Bk2AcBucket(int32_t max_ac) {
    if (max_ac == 0) return 0;
    if (max_ac < 16) return 1;
    if (max_ac < 64) return 2;
    if (max_ac < 256) return 3;
    return 4;
}
static inline int Bk2QBucket(int32_t q) {
    if (q < 10) return 0;
    if (q < 20) return 1;
    if (q < 30) return 2;
    return 3;
}

// idct_add: run IDCT on `block` and add (saturating) into dst[8x8].
static void Bink2gIdctAdd(uint8_t* dst, int stride, int16_t* block)
{
    for (int i = 0; i < 8; ++i) Bink2gIdct1d(block + i,     8, 0);
    for (int i = 0; i < 8; ++i) Bink2gIdct1d(block + i * 8, 1, 6);
    for (int i = 0; i < 8; ++i) {
        for (int j = 0; j < 8; ++j)
            dst[j] = Clip255((int32_t)dst[j] + block[j * 8 + i]);
        dst += stride;
    }
}

// Chroma variant: same arithmetic, plus saturation-event counters.
// dc_hint / max_ac_hint / q_hint are for diagnostic bucketing only.
static void Bink2gChromaIdctAdd(uint8_t* dst, int stride, int16_t* block,
                                int32_t dc_hint, int32_t max_ac_hint, int32_t q_hint)
{
    for (int i = 0; i < 8; ++i) Bink2gIdct1d(block + i,     8, 0);
    for (int i = 0; i < 8; ++i) Bink2gIdct1d(block + i * 8, 1, 6);
    const int dc_b = Bk2DcBucket(dc_hint);
    const int ac_b = Bk2AcBucket(max_ac_hint);
    const int q_b  = Bk2QBucket(q_hint);
    int64_t block_sum = 0;
    for (int i = 0; i < 8; ++i) {
        for (int j = 0; j < 8; ++j) {
            const int32_t r = block[j * 8 + i];
            const int32_t s = (int32_t)dst[j] + r;
            g_chroma_sat.sum_residual += r;
            ++g_chroma_sat.pixels;
            block_sum += r;
            if (s < 0)        ++g_chroma_sat.low;
            else if (s > 255) ++g_chroma_sat.high;
            dst[j] = Clip255(s);
        }
        dst += stride;
    }
    g_chroma_sat.by_dc[dc_b].pixels += 64; g_chroma_sat.by_dc[dc_b].sum += block_sum;
    g_chroma_sat.by_ac[ac_b].pixels += 64; g_chroma_sat.by_ac[ac_b].sum += block_sum;
    g_chroma_sat.by_q[q_b].pixels  += 64; g_chroma_sat.by_q[q_b].sum  += block_sum;
}

// ---- BK2_DUMP_INTER_LUMA_MB / BK2_DUMP_LUMA_MC diagnostic ----
// Env-gated luma inter dumps for M5 SOVIET9 chroma-motion-bleed
// classification (bink2-soviet9-chroma-motion-bleed.md). Independent
// of BK2_MB_TRACE. Single per-call frame counter; sidecar coords set
// by the slice loop around the luma calls. Not concurrency-safe.
//   BK2_DUMP_INTER_LUMA_MB=1       dump dc/cbp/sub[b] pre-IDCT + dst
//   BK2_DUMP_LUMA_MC=1             dump 32x32 MC tile at call site
//   BK2_DUMP_INTER_LUMA_MB_FRAME=N  (1-indexed inter-frame; omit=all)
//   BK2_DUMP_INTER_LUMA_MB_ROWS=r0:r1 / _COLS=c0:c1 (inclusive)
//   BK2_DUMP_LUMA_MC_{FRAME,ROWS,COLS} fall back to the _MB triplet
//   if not set, so a single filter can drive both dumps.
static std::atomic<int> g_bk2_dump_luma_interframe_counter{0};
static int g_bk2_dump_luma_residue_enable = -1;
static int g_bk2_dump_luma_mc_enable = -1;
static int g_bk2_dump_luma_mc_src_enable = 0;
static int g_bk2_dump_ac_coefs_enable = -1;
static int g_bk2_dump_luma_frame = -1;
static int g_bk2_dump_luma_frame1 = -1;
static int g_bk2_dump_luma_r0 = 0, g_bk2_dump_luma_r1 = 1 << 30;
static int g_bk2_dump_luma_c0 = 0, g_bk2_dump_luma_c1 = 1 << 30;

// M9.1 globals defined above LumaMc16 so the per-mode skip check compiles;
// init is in Bk2DumpLumaInit below.

// g_dbg_luma_{frame_idx,mb_row,mb_col,plane_tag} moved above DecodeAcBlocks.

static void Bk2DumpLumaInit()
{
    if (g_bk2_dump_luma_residue_enable != -1) return;
    auto envb = [](const char* n){
        const char* e = std::getenv(n);
        return (e && e[0] && e[0] != '0') ? 1 : 0;
    };
    g_bk2_dump_luma_residue_enable = envb("BK2_DUMP_INTER_LUMA_MB");
    g_bk2_dump_luma_mc_enable      = envb("BK2_DUMP_LUMA_MC");
    g_bk2_dump_luma_mc_src_enable  = envb("BK2_DUMP_LUMA_MC_SRC");
    g_bk2_dump_ac_coefs_enable     = envb("BK2_DUMP_AC_COEFS");
    g_bk2_luma_mc_skip_mode[1] = envb("BK2_LUMA_MC_SKIP_MODE1");
    g_bk2_luma_mc_skip_mode[2] = envb("BK2_LUMA_MC_SKIP_MODE2");
    g_bk2_luma_mc_skip_mode[3] = envb("BK2_LUMA_MC_SKIP_MODE3");
    if (!g_bk2_dump_luma_residue_enable && !g_bk2_dump_luma_mc_enable
        && !g_bk2_dump_luma_mc_src_enable && !g_bk2_dump_ac_coefs_enable) return;
    const char* f = std::getenv("BK2_DUMP_INTER_LUMA_MB_FRAME");
    if (!f) f = std::getenv("BK2_DUMP_LUMA_MC_FRAME");
    if (f) {
        int a = -1, b = -1;
        int n = std::sscanf(f, "%d:%d", &a, &b);
        g_bk2_dump_luma_frame = a;
        g_bk2_dump_luma_frame1 = (n == 2) ? b : a;
    }
    const char* r = std::getenv("BK2_DUMP_INTER_LUMA_MB_ROWS");
    if (!r) r = std::getenv("BK2_DUMP_LUMA_MC_ROWS");
    if (r) std::sscanf(r, "%d:%d", &g_bk2_dump_luma_r0, &g_bk2_dump_luma_r1);
    const char* c = std::getenv("BK2_DUMP_INTER_LUMA_MB_COLS");
    if (!c) c = std::getenv("BK2_DUMP_LUMA_MC_COLS");
    if (c) std::sscanf(c, "%d:%d", &g_bk2_dump_luma_c0, &g_bk2_dump_luma_c1);
}

static bool Bk2DumpLumaMatch()
{
    if (g_bk2_dump_luma_frame >= 0 &&
        (g_dbg_luma_frame_idx < g_bk2_dump_luma_frame ||
         g_dbg_luma_frame_idx > g_bk2_dump_luma_frame1)) return false;
    if (g_dbg_luma_mb_row < g_bk2_dump_luma_r0 ||
        g_dbg_luma_mb_row > g_bk2_dump_luma_r1) return false;
    if (g_dbg_luma_mb_col < g_bk2_dump_luma_c0 ||
        g_dbg_luma_mb_col > g_bk2_dump_luma_c1) return false;
    return true;
}

static void Bk2DumpLumaTile32(const char* tag, const uint8_t* dst, int stride)
{
    std::fprintf(stderr, "%s f=%d r=%d c=%d plane=%s 32x32=",
                 tag, g_dbg_luma_frame_idx,
                 g_dbg_luma_mb_row, g_dbg_luma_mb_col, g_dbg_luma_plane_tag);
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            std::fprintf(stderr, "%u%s",
                         (unsigned)dst[y * stride + x],
                         (y == 31 && x == 31) ? "\n" : ",");
        }
    }
}

// Decode an inter luma plane and ADD its residual into dst[32x32 region].
static bool DecodeAndAddInterLumaPlane(Bink2BitReader& bits,
                                       uint32_t frame_flags,
                                       uint32_t& prev_cbp,
                                       int32_t inter_q,
                                       uint8_t* dst, int stride)
{
    uint32_t cbp = 0;
    if (!DecodeCbpLuma(bits, frame_flags, prev_cbp, cbp)) return false;
    prev_cbp = cbp;

    std::array<int32_t, 16> dc = {};
    std::array<int32_t, 16> zero16 = {};
    if (!DecodeDcValues<16>(bits, inter_q, true, -1023, 1023, 0xA8u,
                            zero16, zero16, zero16, dc)) return false;

    std::array<std::array<int16_t, 64>, 4> sub = {};
    // Diagnostic: BK2_SKIP_LUMA_RESIDUAL=1 consumes bits but skips the
    // IDCT-add for the inter luma path, isolating residue contribution
    // from the MC / copy-prev seed (M4 for SOVIET9 chroma-bleed task).
    static const bool skip_luma_residual = []{
        const char* e = std::getenv("BK2_SKIP_LUMA_RESIDUAL");
        return e && e[0] && e[0] != '0';
    }();
    Bk2DumpLumaInit();
    const bool dump_luma =
        g_bk2_dump_luma_residue_enable && dst && Bk2DumpLumaMatch();
    if (dump_luma) {
        std::fprintf(stderr,
            "LUMA_MB_BEGIN f=%d r=%d c=%d plane=%s q=%d cbp=0x%x\n",
            g_dbg_luma_frame_idx, g_dbg_luma_mb_row, g_dbg_luma_mb_col,
            g_dbg_luma_plane_tag, (int)inter_q, (unsigned)cbp);
        std::fprintf(stderr, "LUMA_MB_DC f=%d r=%d c=%d dc=",
                     g_dbg_luma_frame_idx, g_dbg_luma_mb_row, g_dbg_luma_mb_col);
        for (int i = 0; i < 16; ++i) {
            std::fprintf(stderr, "%d%s", dc[i], i == 15 ? "\n" : ",");
        }
        Bk2DumpLumaTile32("LUMA_MB_BASE", dst, stride);
    }
    const bool dump_ac =
        g_bk2_dump_ac_coefs_enable && Bk2DumpLumaMatch();
    for (uint32_t g = 0; g < 4u; ++g) {
        g_dbg_ac_active = dump_ac ? 1 : 0;
        g_dbg_ac_group = (int)g;
        const bool ac_ok = DecodeAcBlocks(bits, cbp >> (g * 4u), inter_q,
                                          kBink2gInterQmat, sub);
        g_dbg_ac_active = 0;
        if (!ac_ok) return false;
        if (!dst) continue;  // bitstream-only mode (e.g. alpha with no buffer)
        if (skip_luma_residual) continue;
        for (uint32_t b = 0; b < 4u; ++b) {
            const uint32_t decoded_idx = g * 4u + b;
            sub[b][0] = (int16_t)(dc[decoded_idx] * 8 + 32);
            if (dump_luma) {
                std::fprintf(stderr,
                    "LUMA_SUB f=%d r=%d c=%d g=%u b=%u idx=%u coeffs=",
                    g_dbg_luma_frame_idx, g_dbg_luma_mb_row, g_dbg_luma_mb_col,
                    (unsigned)g, (unsigned)b, (unsigned)decoded_idx);
                for (int k = 0; k < 64; ++k) {
                    std::fprintf(stderr, "%d%s",
                                 (int)sub[b][k], k == 63 ? "\n" : ",");
                }
            }
            const uint8_t spatial = kLumaRepos[decoded_idx];
            const int block_x = (spatial & 3) * 8;
            const int block_y = (spatial >> 2) * 8;
            Bink2gIdctAdd(dst + block_y * stride + block_x, stride, sub[b].data());
        }
    }
    if (dump_luma) {
        Bk2DumpLumaTile32("LUMA_MB_POST", dst, stride);
        std::fprintf(stderr, "LUMA_MB_END f=%d r=%d c=%d\n",
                     g_dbg_luma_frame_idx, g_dbg_luma_mb_row, g_dbg_luma_mb_col);
    }
    return true;
}

// Decode an inter chroma plane and ADD its residual into dst[16x16 region].
// Diagnostic: file-static MB coords set by the slice loop immediately
// before each DecodeAndAddInterChromaPlane call. Consumed when
// BK2_DUMP_CHROMA_MB is set so the per-MB summary line can report which
// MB it describes (plane U/V, col, row). Not concurrency-safe — the
// slice loop is single-threaded.
static int  g_dbg_chroma_mb_col = 0;
static int  g_dbg_chroma_mb_row = 0;
static char g_dbg_chroma_mb_plane = 'U';

static bool DecodeAndAddInterChromaPlane(Bink2BitReader& bits,
                                         uint32_t& prev_cbp,
                                         int32_t inter_q,
                                         uint8_t* dst, int stride)
{
    uint32_t cbp = 0;
    if (!DecodeCbpChroma(bits, prev_cbp, cbp)) return false;
    prev_cbp = cbp;

    std::array<int32_t, 4> dc = {};
    std::array<int32_t, 4> zero4 = {};
    if (!DecodeDcValues<4>(bits, inter_q, false, -1023, 1023, 0xA8u,
                           zero4, zero4, zero4, dc)) return false;

    std::array<std::array<int16_t, 64>, 4> sub = {};
    if (!DecodeAcBlocks(bits, cbp, inter_q,
                        kBink2gInterQmat, sub)) return false;
    // Diagnostic: BK2_DUMP_CHROMA_MB=1 emits one line per inter chroma
    // plane call with cbp, per-subblock DC and max|AC|. Used in tandem
    // with the debugger's "FRAME_BEGIN idx=N" stderr marker (emitted by
    // --dump-all-yuva) to locate inter chroma MBs with zero residual —
    // their final chroma pixels equal the MC output and can be diffed
    // directly against BP64 to isolate MC-filter bugs.
    static const bool dump_chroma_mb = []{
        const char* e = std::getenv("BK2_DUMP_CHROMA_MB");
        return e && e[0] && e[0] != '0';
    }();
    if (dump_chroma_mb) {
        int maxac[4] = {0, 0, 0, 0};
        for (int j = 0; j < 4; ++j) {
            for (int k = 1; k < 64; ++k) {
                const int a = sub[j][k] < 0 ? -sub[j][k] : sub[j][k];
                if (a > maxac[j]) maxac[j] = a;
            }
        }
        std::fprintf(stderr,
            "CHROMA_MB col=%d row=%d plane=%c cbp=0x%x q=%d "
            "dc=%d,%d,%d,%d maxac=%d,%d,%d,%d\n",
            g_dbg_chroma_mb_col, g_dbg_chroma_mb_row, g_dbg_chroma_mb_plane,
            cbp, inter_q,
            dc[0], dc[1], dc[2], dc[3],
            maxac[0], maxac[1], maxac[2], maxac[3]);
    }
    if (!dst) return true;
    // Diagnostic: BK2_SKIP_CHROMA_RESIDUAL=1 consumes bits but skips the
    // IDCT-add, isolating MC vs residual contribution to chroma drift.
    static const bool skip_residual = []{
        const char* e = std::getenv("BK2_SKIP_CHROMA_RESIDUAL");
        return e && e[0] && e[0] != '0';
    }();
    if (skip_residual) return true;
    // Diagnostic: BK2_CHROMA_DC_BIAS=N overrides the default +32 DC
    // rounding term. Default 32 matches FFmpeg. 0 matches NihAV (drifts
    // green). Signed-drift sweep re-done 2026-04-19 afternoon 2 after
    // prior MAD sweep proved misleading.
    static const int32_t chroma_dc_bias = []{
        const char* e = std::getenv("BK2_CHROMA_DC_BIAS");
        if (!e || !e[0]) return 32;
        return std::atoi(e);
    }();
    // Diagnostic split: isolate DC vs AC contribution of the chroma inter
    // residual. BK2_SKIP_CHROMA_RESIDUAL_DC=1 zeros the DC term, keeping AC.
    // BK2_SKIP_CHROMA_RESIDUAL_AC=1 zeros AC coefficients, keeping DC.
    static const bool skip_residual_dc = []{
        const char* e = std::getenv("BK2_SKIP_CHROMA_RESIDUAL_DC");
        return e && e[0] && e[0] != '0';
    }();
    static const bool skip_residual_ac = []{
        const char* e = std::getenv("BK2_SKIP_CHROMA_RESIDUAL_AC");
        return e && e[0] && e[0] != '0';
    }();
    for (uint32_t j = 0; j < 4u; ++j) {
        if (skip_residual_ac) {
            for (int k = 1; k < 64; ++k) sub[j][k] = 0;
        }
        int32_t max_ac = 0;
        for (int k = 1; k < 64; ++k) {
            const int32_t v = sub[j][k];
            const int32_t a = v < 0 ? -v : v;
            if (a > max_ac) max_ac = a;
        }
        int32_t dc_val = dc[j];
        if (skip_residual_dc) dc_val = 0;
        ++g_chroma_sat.dc_blocks;
        g_chroma_sat.sum_dc += dc_val;
        g_chroma_sat.sum_abs_dc += (dc_val < 0 ? -dc_val : dc_val);
        sub[j][0] = (int16_t)(dc_val * 8 + chroma_dc_bias);
        const int block_x = (j & 1) * 8;
        const int block_y = (j >> 1) * 8;
        Bink2gChromaIdctAdd(dst + block_y * stride + block_x, stride, sub[j].data(),
                            dc_val, max_ac, inter_q);
    }
    return true;
}

// Pairwise-averaged 8×8 block mean used by the INTRA-in-inter neighbour-DC
// preprocessing. Matches bink2g_average_block.
static int32_t AverageBlockDc8x8(const uint8_t* src, int stride)
{
    int32_t sum = 0;
    for (int i = 0; i < 8; ++i) {
        int avg_a = (src[i + 0*stride] + src[i + 1*stride] + 1) >> 1;
        int avg_b = (src[i + 2*stride] + src[i + 3*stride] + 1) >> 1;
        int avg_c = (src[i + 4*stride] + src[i + 5*stride] + 1) >> 1;
        int avg_d = (src[i + 6*stride] + src[i + 7*stride] + 1) >> 1;
        int avg_e = (avg_a + avg_b + 1) >> 1;
        int avg_f = (avg_c + avg_d + 1) >> 1;
        int avg_g = (avg_e + avg_f + 1) >> 1;
        sum += avg_g;
    }
    return sum;
}

static void AverageLumaDcFromPixels(const uint8_t* mb_top_left, int stride,
                                    std::array<int32_t, 16>& dc)
{
    for (int i = 0; i < 16; ++i) {
        int I = kLumaRepos[i];
        int X = I & 3;
        int Y = I >> 2;
        dc[i] = AverageBlockDc8x8(mb_top_left + X * 8 + Y * 8 * stride, stride);
    }
}

static void AverageChromaDcFromPixels(const uint8_t* mb_top_left, int stride,
                                      std::array<int32_t, 4>& dc)
{
    for (int i = 0; i < 4; ++i) {
        int X = i & 1;
        int Y = i >> 1;
        dc[i] = AverageBlockDc8x8(mb_top_left + X * 8 + Y * 8 * stride, stride);
    }
}

// INTRA MB embedded in an inter frame: full decode + pixel reconstruction.
// Identical CBP→DC→AC→IDCT flow as DecodeKeyframeMacroblock, minus the DQ
// step (intra_q is already resolved by the inter-path's update_q). Returns
// the decoded DCs and the reconstructed pixels in `out_mb` (U/V stored in
// bink2-stream order — caller must blit pixels.u → frame.v and pixels.v →
// frame.u).
static bool DecodeAndReconstructIntraInInter(
    Bink2BitReader& bits,
    uint32_t frame_flags,
    uint32_t mb_flags,
    int32_t intra_q,
    bool has_alpha,
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
    uint32_t& y_prev_cbp,
    uint32_t& u_prev_cbp,
    uint32_t& v_prev_cbp,
    uint32_t& a_prev_cbp,
    std::array<int32_t, 16>& out_y_dc,
    std::array<int32_t, 4>& out_u_dc,
    std::array<int32_t, 4>& out_v_dc,
    std::array<int32_t, 16>& out_a_dc,
    Bink2DecodedMacroblock& out_mb)
{
    out_mb = {};
    out_mb.pixels.has_alpha = has_alpha;

    // Y
    if (!DecodeCbpLuma(bits, frame_flags, y_prev_cbp, out_mb.probe.y_cbp)) return false;
    y_prev_cbp = out_mb.probe.y_cbp;
    if (!DecodeDcValues<16>(bits, intra_q, true, 0, 2047, mb_flags,
                            left_y_dc, top_y_dc, top_left_y_dc, out_y_dc)) return false;
    if (!DecodeLumaPlaneAc(bits, out_mb.probe.y_cbp, intra_q, out_mb.y_blocks)) return false;

    // U (first-decoded chroma, bink2 stream Cr)
    if (!DecodeCbpChroma(bits, u_prev_cbp, out_mb.probe.u_cbp)) return false;
    u_prev_cbp = out_mb.probe.u_cbp;
    if (!DecodeDcValues<4>(bits, intra_q, false, 0, 2047, mb_flags,
                           left_u_dc, top_u_dc, top_left_u_dc, out_u_dc)) return false;
    if (!DecodeChromaPlaneAc(bits, out_mb.probe.u_cbp, intra_q, out_mb.u_blocks)) return false;

    // V (second-decoded chroma, bink2 stream Cb)
    if (!DecodeCbpChroma(bits, v_prev_cbp, out_mb.probe.v_cbp)) return false;
    v_prev_cbp = out_mb.probe.v_cbp;
    if (!DecodeDcValues<4>(bits, intra_q, false, 0, 2047, mb_flags,
                           left_v_dc, top_v_dc, top_left_v_dc, out_v_dc)) return false;
    if (!DecodeChromaPlaneAc(bits, out_mb.probe.v_cbp, intra_q, out_mb.v_blocks)) return false;

    // Alpha
    if (has_alpha) {
        if (!DecodeCbpLuma(bits, frame_flags, a_prev_cbp, out_mb.probe.a_cbp)) return false;
        a_prev_cbp = out_mb.probe.a_cbp;
        if (!DecodeDcValues<16>(bits, intra_q, true, 0, 2047, mb_flags,
                                left_a_dc, top_a_dc, top_left_a_dc, out_a_dc)) return false;
        if (!DecodeLumaPlaneAc(bits, out_mb.probe.a_cbp, intra_q, out_mb.a_blocks)) return false;
    } else {
        out_a_dc.fill(0);
    }

    // Pixel reconstruction
    ReconstructPlaneBlocks(out_mb.pixels.y, 32, out_mb.y_blocks, out_y_dc,
                           kLumaRepos, true);
    ReconstructPlaneBlocks(out_mb.pixels.u, 16, out_mb.u_blocks, out_u_dc,
                           nullptr, false);
    ReconstructPlaneBlocks(out_mb.pixels.v, 16, out_mb.v_blocks, out_v_dc,
                           nullptr, false);
    if (has_alpha) {
        ReconstructPlaneBlocks(out_mb.pixels.a, 32, out_mb.a_blocks, out_a_dc,
                               kLumaRepos, true);
    }
    return true;
}

static void CopyPrevFrameMacroblock(const Bink2DecodedFrame& prev,
                                    Bink2DecodedFrame& frame,
                                    uint32_t mb_col, uint32_t mb_row)
{
    const uint32_t y_x = mb_col * 32u;
    const uint32_t y_y = mb_row * 32u;
    const uint32_t uv_x = mb_col * 16u;
    const uint32_t uv_y = mb_row * 16u;
    for (uint32_t r = 0; r < 32u; ++r) {
        std::memcpy(frame.y.data() + (y_y + r) * frame.luma_stride + y_x,
                    prev.y.data()  + (y_y + r) * prev.luma_stride  + y_x, 32u);
    }
    for (uint32_t r = 0; r < 16u; ++r) {
        std::memcpy(frame.u.data() + (uv_y + r) * frame.chroma_stride + uv_x,
                    prev.u.data()  + (uv_y + r) * prev.chroma_stride  + uv_x, 16u);
        std::memcpy(frame.v.data() + (uv_y + r) * frame.chroma_stride + uv_x,
                    prev.v.data()  + (uv_y + r) * prev.chroma_stride  + uv_x, 16u);
    }
    if (frame.has_alpha && prev.has_alpha &&
        !frame.a.empty() && !prev.a.empty()) {
        for (uint32_t r = 0; r < 32u; ++r) {
            std::memcpy(frame.a.data() + (y_y + r) * frame.luma_stride + y_x,
                        prev.a.data()  + (y_y + r) * prev.luma_stride  + y_x, 32u);
        }
    }
}

// ---- BK2_MB_TRACE per-MB diagnostic ----
// Enable with BK2_MB_TRACE=1. Filter:
//   BK2_MB_TRACE_FRAME=N   (N-th inter frame, 1-indexed; omit = all)
//   BK2_MB_TRACE_ROWS=r0:r1  (inclusive)
//   BK2_MB_TRACE_COLS=c0:c1  (inclusive)
// One stderr line per MB with type, MV, q-intra/q-inter, CBP intra/inter.
static std::atomic<int> g_bk2_mbtrace_interframe_counter{0};
static int g_bk2_mbtrace_enable = -1;
static int g_bk2_mbtrace_frame  = -1;
static int g_bk2_mbtrace_r0 = 0, g_bk2_mbtrace_r1 = 1 << 30;
static int g_bk2_mbtrace_c0 = 0, g_bk2_mbtrace_c1 = 1 << 30;
static void Bk2MbTraceInit()
{
    if (g_bk2_mbtrace_enable != -1) return;
    const char* e = std::getenv("BK2_MB_TRACE");
    g_bk2_mbtrace_enable = (e && e[0] && e[0] != '0') ? 1 : 0;
    if (!g_bk2_mbtrace_enable) return;
    if (const char* f = std::getenv("BK2_MB_TRACE_FRAME"))
        g_bk2_mbtrace_frame = std::atoi(f);
    if (const char* r = std::getenv("BK2_MB_TRACE_ROWS"))
        std::sscanf(r, "%d:%d", &g_bk2_mbtrace_r0, &g_bk2_mbtrace_r1);
    if (const char* c = std::getenv("BK2_MB_TRACE_COLS"))
        std::sscanf(c, "%d:%d", &g_bk2_mbtrace_c0, &g_bk2_mbtrace_c1);
}

bool Bink2DecodeInterFrameSkipPrefix(const Bink2Header& header,
                                     const Bink2FramePlan& plan,
                                     const std::vector<uint8_t>& packet,
                                     const Bink2DecodedFrame& prev_frame,
                                     Bink2DecodedFrame& frame)
{
    frame = {};

    Bk2MbTraceInit();
    const int bk2_trace_frame_idx =
        g_bk2_mbtrace_enable
            ? (g_bk2_mbtrace_interframe_counter.fetch_add(1) + 1)
            : 0;
    const bool bk2_trace_this_frame =
        g_bk2_mbtrace_enable &&
        (g_bk2_mbtrace_frame < 0 || g_bk2_mbtrace_frame == bk2_trace_frame_idx);

    Bk2DumpLumaInit();
    if (g_bk2_dump_luma_residue_enable || g_bk2_dump_luma_mc_enable
        || g_bk2_dump_luma_mc_src_enable) {
        g_dbg_luma_frame_idx =
            g_bk2_dump_luma_interframe_counter.fetch_add(1) + 1;
    }

    if (!header.Is_Valid()) return false;
    if (packet.empty()) return false;
    if (plan.packet.num_slices == 0u) return false;
    if (prev_frame.y.empty()) return false;
    if (prev_frame.aligned_width != plan.aligned_width ||
        prev_frame.aligned_height != plan.aligned_height) return false;

    frame.has_alpha       = header.Has_Alpha();
    frame.width           = header.width;
    frame.height          = header.height;
    frame.aligned_width   = plan.aligned_width;
    frame.aligned_height  = plan.aligned_height;
    frame.luma_stride     = plan.aligned_width;
    frame.chroma_stride   = plan.aligned_width / 2u;
    frame.macroblock_cols = plan.aligned_width / 32u;
    frame.macroblock_rows = plan.aligned_height / 32u;
    frame.macroblock_stride = frame.macroblock_cols;
    frame.decoded_macroblocks.assign((size_t)frame.macroblock_stride * frame.macroblock_rows, 0u);
    frame.y.assign((size_t)frame.luma_stride * frame.aligned_height, 16u);
    frame.u.assign((size_t)frame.chroma_stride * (frame.aligned_height / 2u), 128u);
    frame.v.assign((size_t)frame.chroma_stride * (frame.aligned_height / 2u), 128u);
    if (frame.has_alpha) {
        const uint8_t a_fill = Bink2AlphaFillValue(plan.packet.frame_flags);
        frame.a.assign((size_t)frame.luma_stride * frame.aligned_height, a_fill);
    }

    Bink2BitReader bits;
    if (!bits.Reset(packet.data(), packet.size())) return false;

    const uint32_t width_mb = frame.macroblock_cols;
    bool stopped = false;

    for (uint32_t s = 0; s < plan.packet.num_slices && !stopped; ++s) {
        const uint32_t slice_y_begin =
            (s == 0) ? 0u : plan.packet.slice_heights[s - 1];
        const uint32_t slice_y_end = plan.packet.slice_heights[s];
        const uint32_t slice_rows = (slice_y_end - slice_y_begin) / 32u;

        const size_t slice_start_bits =
            (s == 0) ? plan.payload_bit_offset
                     : (size_t)plan.packet.slice_offsets[s] * 8u;
        const size_t slice_end_bits =
            (s + 1u < plan.packet.num_slices)
                ? (size_t)plan.packet.slice_offsets[s + 1u] * 8u
                : packet.size() * 8u;

        if (!bits.Reset(packet.data(), packet.size())) return false;
        if (!bits.Skip_Bits(slice_start_bits)) return false;
        if (s == 0) frame.bit_offset_begin = slice_start_bits;

        // CBP predictor state is per MB row, and separate for intra- and
        // inter-coded MBs (FFmpeg bink2g_decode_slice lines 968-969). Sharing
        // a single chain across types corrupts the predictor when INTRA MBs
        // follow RESIDUE MBs.
        uint32_t y_cbp_intra = 0, u_cbp_intra = 0, v_cbp_intra = 0, a_cbp_intra = 0;
        uint32_t y_cbp_inter = 0, u_cbp_inter = 0, v_cbp_inter = 0, a_cbp_inter = 0;

        // Per-slice MV + q state: current row being decoded + prev row.
        // prev_q/prev_mv are explicitly memset to zero at slice start
        // (FFmpeg bink2g_decode_slice lines 971-972).
        std::vector<Bink2MvBlock> prev_row_mv(width_mb);
        std::vector<Bink2MvBlock> current_row_mv(width_mb);
        std::vector<int8_t> prev_row_intra_q(width_mb, 0);
        std::vector<int8_t> current_row_intra_q(width_mb, 0);
        std::vector<int8_t> prev_row_inter_q(width_mb, 0);
        std::vector<int8_t> current_row_inter_q(width_mb, 0);

        // Per-slice INTRA-in-inter DC + block-type predictor state. Swapped
        // each MB row to provide `prev_idc` (row above) / `current_idc`
        // (current row) neighbours for DC prediction. Non-INTRA neighbours
        // get their DC slots filled on-demand via the averaging preprocess.
        std::vector<uint8_t> prev_row_intra_flag(width_mb, 0);
        std::vector<uint8_t> current_row_intra_flag(width_mb, 0);
        std::vector<std::array<int32_t, 16>> prev_row_dc_y(width_mb);
        std::vector<std::array<int32_t, 16>> current_row_dc_y(width_mb);
        std::vector<std::array<int32_t, 4>>  prev_row_dc_u(width_mb);
        std::vector<std::array<int32_t, 4>>  current_row_dc_u(width_mb);
        std::vector<std::array<int32_t, 4>>  prev_row_dc_v(width_mb);
        std::vector<std::array<int32_t, 4>>  current_row_dc_v(width_mb);
        std::vector<std::array<int32_t, 16>> prev_row_dc_a(width_mb);
        std::vector<std::array<int32_t, 16>> current_row_dc_a(width_mb);

        for (uint32_t row = 0; row < slice_rows && !stopped; ++row) {
            const uint32_t frame_row = slice_y_begin / 32u + row;
            // LRU is reset at the start of every MB row (FFmpeg: declared
            // inside the y-loop of bink2g_decode_slice).
            int lru[4] = { kBink2BlockMotion, kBink2BlockResidue, kBink2BlockSkip, kBink2BlockIntra };
            y_cbp_intra = u_cbp_intra = v_cbp_intra = a_cbp_intra = 0;
            y_cbp_inter = u_cbp_inter = v_cbp_inter = a_cbp_inter = 0;
            for (auto& m : current_row_mv) m = Bink2MvBlock{};
            std::fill(current_row_intra_q.begin(), current_row_intra_q.end(), (int8_t)0);
            std::fill(current_row_inter_q.begin(), current_row_inter_q.end(), (int8_t)0);
            std::fill(current_row_intra_flag.begin(), current_row_intra_flag.end(), (uint8_t)0);

            for (uint32_t col = 0; col < width_mb; ++col) {
                if (bits.Bits_Read() >= slice_end_bits) { stopped = true; break; }

                int type = 0;
                if (!DecodeInterBlockType(bits, lru, type)) {
                    frame.decode_mb_col = col; frame.decode_mb_row = frame_row;
                    frame.failure_stage = 110u;
                    stopped = true; break;
                }

                // mb_flags is computed below; the INTRA branch is moved
                // to AFTER the flags computation.

                const uint32_t mb_flags =
                    (row == 0 ? 0x80u : 0u) |
                    (col == 0 ? 0x20u : 0u) |
                    (col == 1 ? 0x200u : 0u) |
                    (col + 1u == width_mb ? 0x40u : 0u);

                // FFmpeg's update_inter_q / update_intra_q predictor,
                // applied for every MB (with dq=0 for SKIP/MOTION types).
                // FFmpeg rejects the frame when q ends up < 0 or >= 37; we
                // clamp instead, which keeps the decoder making forward
                // progress on samples (e.g. INSITES.BK2) whose encoder
                // emits DQ deltas that under/overshoot the q range. On
                // clean samples (AFRICA, EUROPE) this clamp is never
                // triggered and the output remains byte-identical.
                auto update_q = [&](int32_t dq, bool inter) -> int32_t {
                    auto& cur = inter ? current_row_inter_q : current_row_intra_q;
                    auto& prv = inter ? prev_row_inter_q     : prev_row_intra_q;
                    int32_t q;
                    if ((mb_flags & 0x80u) && (mb_flags & 0x20u)) {
                        q = 16 + dq;
                    } else if (mb_flags & 0x80u) {
                        q = cur[col - 1] + dq;
                    } else if (mb_flags & 0x20u) {
                        q = prv[col] + dq;
                    } else {
                        q = MidPred3(prv[col], cur[col - 1], prv[col - 1]) + dq;
                    }
                    if (q < 0) q = 0;
                    if (q >= kBink2MaxQuant) q = kBink2MaxQuant - 1;
                    cur[col] = (int8_t)q;
                    return q;
                };

                Bink2MvBlock raw_mv = {};
                Bink2MvBlock absolute_mv = {};
                const bool needs_mv = (type == kBink2BlockMotion ||
                                       type == kBink2BlockResidue);
                if (needs_mv) {
                    if (!DecodeInterMv(bits, raw_mv)) {
                        frame.decode_mb_col = col; frame.decode_mb_row = frame_row;
                        frame.failure_stage = 120u; stopped = true; break;
                    }
                    const Bink2MvBlock& l_mv = (col > 0) ? current_row_mv[col - 1]
                                                        : Bink2MvBlock{};
                    const Bink2MvBlock& t_mv = (row > 0) ? prev_row_mv[col]
                                                        : Bink2MvBlock{};
                    const Bink2MvBlock& lt_mv =
                        (row > 0 && col > 0) ? prev_row_mv[col - 1]
                                             : Bink2MvBlock{};
                    PredictMv(mb_flags, l_mv, lt_mv, t_mv, raw_mv, absolute_mv);
                    current_row_mv[col] = absolute_mv;
                }

                // For SKIP/MOTION/RESIDUE we first seed dst from prev frame
                // (SKIP uses this as-is; MOTION overwrites with MC; RESIDUE
                // overwrites with MC then adds residual).
                if (type != kBink2BlockIntra) {
                    CopyPrevFrameMacroblock(prev_frame, frame, col, frame_row);
                }

                if (type == kBink2BlockMotion || type == kBink2BlockResidue) {
                    const uint32_t y_x = col * 32u;
                    const uint32_t y_y = frame_row * 32u;
                    // FFmpeg's MC uses the ORIGINAL visible dimensions for
                    // its out-of-bounds MV check, not the aligned ones.
                    // Passing aligned here lets us read into vertical/
                    // horizontal padding pixels that FFmpeg skips, which
                    // shows up as a drift on non-32-aligned samples.
                    const int mc_w  = (int)header.width;
                    const int mc_h  = (int)header.height;
                    const int mc_cw = (int)header.width  / 2;
                    const int mc_ch = (int)header.height / 2;
                    LumaMcMacroblock(absolute_mv, y_x, y_y,
                                     frame.y.data(), frame.luma_stride,
                                     prev_frame.y.data(), prev_frame.luma_stride,
                                     mc_w, mc_h);
                    if (g_bk2_dump_luma_mc_src_enable) {
                        g_dbg_luma_mb_row = (int)frame_row;
                        g_dbg_luma_mb_col = (int)col;
                        g_dbg_luma_plane_tag = "Y";
                        if (Bk2DumpLumaMatch()) {
                            for (int k = 0; k < 4; ++k) {
                                const int sub_x = (k & 1) * 16;
                                const int sub_y = (k >> 1) * 16;
                                const int sp_mv_x = ((int)absolute_mv.v[k][0] >> 1) + (int)y_x + sub_x;
                                const int sp_mv_y = ((int)absolute_mv.v[k][1] >> 1) + (int)y_y + sub_y;
                                const int sp_mode = ((int)absolute_mv.v[k][0] & 1) | (((int)absolute_mv.v[k][1] & 1) << 1);
                                if (sp_mv_x < 0 || sp_mv_x >= mc_w || sp_mv_y < 0 || sp_mv_y >= mc_h) continue;
                                std::fprintf(stderr,
                                    "LUMA_MC_SRC f=%d r=%u c=%u k=%d mode=%d mv_x=%d mv_y=%d raw_mv=%d,%d 21x21=",
                                    g_dbg_luma_frame_idx, (unsigned)frame_row, (unsigned)col,
                                    k, sp_mode, sp_mv_x, sp_mv_y,
                                    (int)absolute_mv.v[k][0], (int)absolute_mv.v[k][1]);
                                for (int jj = -2; jj < 19; ++jj) {
                                    for (int ii = -2; ii < 19; ++ii) {
                                        const int sy = sp_mv_y + jj;
                                        const int sx = sp_mv_x + ii;
                                        const int cy = sy < 0 ? 0 : (sy >= mc_h ? mc_h - 1 : sy);
                                        const int cx = sx < 0 ? 0 : (sx >= mc_w ? mc_w - 1 : sx);
                                        std::fprintf(stderr, "%s%u",
                                            (ii == -2 && jj == -2) ? "" : ",",
                                            (unsigned)prev_frame.y[cy * (int)prev_frame.luma_stride + cx]);
                                    }
                                }
                                std::fprintf(stderr, "\n");
                                const uint8_t* sub_dst = frame.y.data()
                                    + ((int)y_y + sub_y) * (int)frame.luma_stride
                                    + (int)y_x + sub_x;
                                std::fprintf(stderr,
                                    "LUMA_MC_OUT f=%d r=%u c=%u k=%d 16x16=",
                                    g_dbg_luma_frame_idx, (unsigned)frame_row, (unsigned)col, k);
                                for (int jj = 0; jj < 16; ++jj) {
                                    for (int ii = 0; ii < 16; ++ii) {
                                        std::fprintf(stderr, "%s%u",
                                            (ii == 0 && jj == 0) ? "" : ",",
                                            (unsigned)sub_dst[jj * (int)frame.luma_stride + ii]);
                                    }
                                }
                                std::fprintf(stderr, "\n");
                            }
                        }
                    }
                    if (g_bk2_dump_luma_mc_enable) {
                        g_dbg_luma_mb_row = (int)frame_row;
                        g_dbg_luma_mb_col = (int)col;
                        g_dbg_luma_plane_tag = "Y";
                        if (Bk2DumpLumaMatch()) {
                            std::fprintf(stderr,
                                "LUMA_MC_MV f=%d r=%u c=%u mv=[%d,%d|%d,%d|%d,%d|%d,%d]\n",
                                g_dbg_luma_frame_idx,
                                (unsigned)frame_row, (unsigned)col,
                                (int)absolute_mv.v[0][0], (int)absolute_mv.v[0][1],
                                (int)absolute_mv.v[1][0], (int)absolute_mv.v[1][1],
                                (int)absolute_mv.v[2][0], (int)absolute_mv.v[2][1],
                                (int)absolute_mv.v[3][0], (int)absolute_mv.v[3][1]);
                            Bk2DumpLumaTile32(
                                "LUMA_MC",
                                frame.y.data() + y_y * frame.luma_stride + y_x,
                                (int)frame.luma_stride);
                        }
                    }
                    // Diagnostic: BK2_SKIP_CHROMA_MC=1 leaves chroma seeded
                    // by CopyPrevFrameMacroblock (pure passthrough at MB
                    // position 0,0) and skips the 1/4-pel filter — isolates
                    // MC-filter contribution from state-carryover.
                    static const bool skip_chroma_mc = []{
                        const char* e = std::getenv("BK2_SKIP_CHROMA_MC");
                        return e && e[0] && e[0] != '0';
                    }();
                    if (!skip_chroma_mc) {
                        ChromaMcMacroblock(absolute_mv, y_x / 2u, y_y / 2u,
                                           frame.u.data(), frame.chroma_stride,
                                           prev_frame.u.data(), prev_frame.chroma_stride,
                                           mc_cw, mc_ch);
                        ChromaMcMacroblock(absolute_mv, y_x / 2u, y_y / 2u,
                                           frame.v.data(), frame.chroma_stride,
                                           prev_frame.v.data(), prev_frame.chroma_stride,
                                           mc_cw, mc_ch);
                    }
                    if (Bink2EffectiveHasAlpha(header, plan.packet.frame_flags) &&
                        !frame.a.empty() && !prev_frame.a.empty()) {
                        LumaMcMacroblock(absolute_mv, y_x, y_y,
                                         frame.a.data(), frame.luma_stride,
                                         prev_frame.a.data(), prev_frame.luma_stride,
                                         mc_w, mc_h);
                    }
                }

                // SKIP / MOTION update both q streams with dq=0.
                int32_t inter_q = 0;
                if (type == kBink2BlockSkip || type == kBink2BlockMotion) {
                    update_q(0, true);
                    update_q(0, false);
                }
                // Diagnostic: MOTION MBs have pure-MC chroma (no residual).
                // Their 16x16 chroma region in the output frame IS the MC
                // filter output, which makes them a direct target for pixel
                // diff against BP64 to localize chroma-MC bugs.
                if (type == kBink2BlockMotion) {
                    static const bool dump_chroma_mb = []{
                        const char* e = std::getenv("BK2_DUMP_CHROMA_MB");
                        return e && e[0] && e[0] != '0';
                    }();
                    if (dump_chroma_mb) {
                        std::fprintf(stderr,
                            "MOTION_MB col=%u row=%u mv0=%d,%d mv1=%d,%d mv2=%d,%d mv3=%d,%d\n",
                            (unsigned)col, (unsigned)frame_row,
                            (int)absolute_mv.v[0][0], (int)absolute_mv.v[0][1],
                            (int)absolute_mv.v[1][0], (int)absolute_mv.v[1][1],
                            (int)absolute_mv.v[2][0], (int)absolute_mv.v[2][1],
                            (int)absolute_mv.v[3][0], (int)absolute_mv.v[3][1]);
                    }
                }

                if (type == kBink2BlockIntra) {
                    // FFmpeg invokes bink2g_predict_mv with a zero-delta MV
                    // for INTRA so that `current_mv[mb_pos]` gets populated
                    // from neighbour predictors — later MOTION/RESIDUE MBs
                    // in the same row read this as their L-neighbour MV.
                    {
                        Bink2MvBlock zero_raw = {};
                        zero_raw.num_vectors = 4;
                        Bink2MvBlock absolute_mv = {};
                        const Bink2MvBlock& l_mv = (col > 0) ? current_row_mv[col - 1]
                                                            : Bink2MvBlock{};
                        const Bink2MvBlock& t_mv = (row > 0) ? prev_row_mv[col]
                                                            : Bink2MvBlock{};
                        const Bink2MvBlock& lt_mv =
                            (row > 0 && col > 0) ? prev_row_mv[col - 1]
                                                 : Bink2MvBlock{};
                        PredictMv(mb_flags, l_mv, lt_mv, t_mv, zero_raw, absolute_mv);
                        current_row_mv[col] = absolute_mv;
                    }

                    // INTRA updates inter_q with 0, then decodes dq for intra_q.
                    update_q(0, true);
                    int32_t dq = 0;
                    if (!DecodeInterDq(bits, dq)) {
                        frame.decode_mb_col = col; frame.decode_mb_row = frame_row;
                        frame.failure_stage = 131u;
                        stopped = true; break;
                    }
                    int32_t intra_q = update_q(dq, false);

                    // Neighbour-DC averaging preprocessing: when a spatial
                    // neighbour referenced by DC prediction was not itself
                    // INTRA, its dc[] slot has no decoded values — fill it
                    // by computing the pairwise-averaged mean of the
                    // already-reconstructed pixels at that neighbour's MB
                    // position. Writes into prev/current row DC arrays.
                    const uint32_t mb_x_px = col * 32u;
                    const uint32_t mb_y_px = frame_row * 32u;
                    const int y_stride  = (int)frame.luma_stride;
                    const int uv_stride = (int)frame.chroma_stride;
                    const uint8_t* y_plane = frame.y.data();
                    // Note: bink2-stream "u" (first decoded chroma) lives in
                    // frame.v after the U/V swap; "v" lives in frame.u.
                    const uint8_t* u_plane = frame.v.data();
                    const uint8_t* v_plane = frame.u.data();
                    const uint8_t* a_plane =
                        (Bink2EffectiveHasAlpha(header, plan.packet.frame_flags) &&
                         !frame.a.empty())
                            ? frame.a.data() : nullptr;

                    auto avg_luma_at = [&](const uint8_t* plane, int stride,
                                           uint32_t px, uint32_t py,
                                           std::array<int32_t, 16>& dc) {
                        AverageLumaDcFromPixels(plane + py * stride + px, stride, dc);
                    };
                    auto avg_chroma_at = [&](const uint8_t* plane, int stride,
                                             uint32_t px, uint32_t py,
                                             std::array<int32_t, 4>& dc) {
                        AverageChromaDcFromPixels(plane + py * stride + px, stride, dc);
                    };

                    // LT (prev_idc[col-1])
                    if (!(mb_flags & 0xA0u) && col > 0u && row > 0u &&
                        !prev_row_intra_flag[col - 1]) {
                        const uint32_t nx = mb_x_px - 32u;
                        const uint32_t ny = mb_y_px - 32u;
                        avg_luma_at(y_plane, y_stride, nx, ny,
                                    prev_row_dc_y[col - 1]);
                        avg_chroma_at(u_plane, uv_stride, nx / 2u, ny / 2u,
                                      prev_row_dc_u[col - 1]);
                        avg_chroma_at(v_plane, uv_stride, nx / 2u, ny / 2u,
                                      prev_row_dc_v[col - 1]);
                        if (a_plane) avg_luma_at(a_plane, y_stride, nx, ny,
                                                 prev_row_dc_a[col - 1]);
                    }
                    // L (current_idc[col-1])
                    if (!(mb_flags & 0x20u) && col > 0u &&
                        !current_row_intra_flag[col - 1]) {
                        const uint32_t nx = mb_x_px - 32u;
                        const uint32_t ny = mb_y_px;
                        avg_luma_at(y_plane, y_stride, nx, ny,
                                    current_row_dc_y[col - 1]);
                        avg_chroma_at(u_plane, uv_stride, nx / 2u, ny / 2u,
                                      current_row_dc_u[col - 1]);
                        avg_chroma_at(v_plane, uv_stride, nx / 2u, ny / 2u,
                                      current_row_dc_v[col - 1]);
                        if (a_plane) avg_luma_at(a_plane, y_stride, nx, ny,
                                                 current_row_dc_a[col - 1]);
                    }
                    // TR (prev_idc[col+1]) — only when first col of a non-top row.
                    if ((mb_flags & 0x20u) && !(mb_flags & 0x80u) &&
                        col + 1u < width_mb && row > 0u &&
                        !prev_row_intra_flag[col + 1]) {
                        const uint32_t nx = mb_x_px + 32u;
                        const uint32_t ny = mb_y_px - 32u;
                        avg_luma_at(y_plane, y_stride, nx, ny,
                                    prev_row_dc_y[col + 1]);
                        avg_chroma_at(u_plane, uv_stride, nx / 2u, ny / 2u,
                                      prev_row_dc_u[col + 1]);
                        avg_chroma_at(v_plane, uv_stride, nx / 2u, ny / 2u,
                                      prev_row_dc_v[col + 1]);
                        if (a_plane) avg_luma_at(a_plane, y_stride, nx, ny,
                                                 prev_row_dc_a[col + 1]);
                    }
                    // T (prev_idc[col])
                    if (!(mb_flags & 0x80u) && row > 0u &&
                        !prev_row_intra_flag[col]) {
                        const uint32_t nx = mb_x_px;
                        const uint32_t ny = mb_y_px - 32u;
                        avg_luma_at(y_plane, y_stride, nx, ny,
                                    prev_row_dc_y[col]);
                        avg_chroma_at(u_plane, uv_stride, nx / 2u, ny / 2u,
                                      prev_row_dc_u[col]);
                        avg_chroma_at(v_plane, uv_stride, nx / 2u, ny / 2u,
                                      prev_row_dc_v[col]);
                        if (a_plane) avg_luma_at(a_plane, y_stride, nx, ny,
                                                 prev_row_dc_a[col]);
                    }

                    // Assemble DC-prediction neighbours. At slice/row edges
                    // the corresponding arrays are unused by the predictor
                    // (edge-specific flag branches don't read them), so the
                    // fallback value is irrelevant but must be well-defined.
                    const std::array<int32_t, 16>& l_y =
                        (col > 0u) ? current_row_dc_y[col - 1] : prev_row_dc_y[col];
                    const std::array<int32_t, 16>& t_y =
                        (row > 0u) ? prev_row_dc_y[col] : current_row_dc_y[col];
                    const std::array<int32_t, 16>& lt_y =
                        (row > 0u && col > 0u) ? prev_row_dc_y[col - 1]
                                               : prev_row_dc_y[col];
                    const std::array<int32_t, 4>& l_u =
                        (col > 0u) ? current_row_dc_u[col - 1] : prev_row_dc_u[col];
                    const std::array<int32_t, 4>& t_u =
                        (row > 0u) ? prev_row_dc_u[col] : current_row_dc_u[col];
                    const std::array<int32_t, 4>& lt_u =
                        (row > 0u && col > 0u) ? prev_row_dc_u[col - 1]
                                               : prev_row_dc_u[col];
                    const std::array<int32_t, 4>& l_v =
                        (col > 0u) ? current_row_dc_v[col - 1] : prev_row_dc_v[col];
                    const std::array<int32_t, 4>& t_v =
                        (row > 0u) ? prev_row_dc_v[col] : current_row_dc_v[col];
                    const std::array<int32_t, 4>& lt_v =
                        (row > 0u && col > 0u) ? prev_row_dc_v[col - 1]
                                               : prev_row_dc_v[col];
                    const std::array<int32_t, 16>& l_a =
                        (col > 0u) ? current_row_dc_a[col - 1] : prev_row_dc_a[col];
                    const std::array<int32_t, 16>& t_a =
                        (row > 0u) ? prev_row_dc_a[col] : current_row_dc_a[col];
                    const std::array<int32_t, 16>& lt_a =
                        (row > 0u && col > 0u) ? prev_row_dc_a[col - 1]
                                               : prev_row_dc_a[col];

                    Bink2DecodedMacroblock mb = {};
                    if (!DecodeAndReconstructIntraInInter(
                            bits, plan.packet.frame_flags, mb_flags, intra_q,
                            Bink2EffectiveHasAlpha(header, plan.packet.frame_flags),
                            l_y, t_y, lt_y,
                            l_u, t_u, lt_u,
                            l_v, t_v, lt_v,
                            l_a, t_a, lt_a,
                            y_cbp_intra, u_cbp_intra, v_cbp_intra, a_cbp_intra,
                            current_row_dc_y[col], current_row_dc_u[col],
                            current_row_dc_v[col], current_row_dc_a[col],
                            mb)) {
                        frame.decode_mb_col = col; frame.decode_mb_row = frame_row;
                        frame.failure_stage = 130u;
                        stopped = true; break;
                    }
                    current_row_intra_flag[col] = 1u;

                    // Blit decoded pixels into frame (U/V swap: mb.pixels.u
                    // is the first-decoded chroma, writes to frame.v).
                    {
                        const uint32_t y_x = mb_x_px;
                        const uint32_t y_y = mb_y_px;
                        const uint32_t uv_x = y_x / 2u;
                        const uint32_t uv_y = y_y / 2u;
                        for (uint32_t r = 0; r < 32u; ++r) {
                            std::memcpy(frame.y.data() + (y_y + r) * frame.luma_stride + y_x,
                                        mb.pixels.y.data() + r * 32u, 32u);
                        }
                        // Diagnostic: BK2_SKIP_INTRA_IN_INTER_CHROMA=1 leaves
                        // chroma at copy-prev (or whatever the inter seed was)
                        // for INTRA-in-inter MBs. Isolates IIB chroma blit
                        // contribution to drift.
                        static const bool skip_iib_chroma = []{
                            const char* e = std::getenv("BK2_SKIP_INTRA_IN_INTER_CHROMA");
                            return e && e[0] && e[0] != '0';
                        }();
                        if (skip_iib_chroma) {
                            // Seed from prev_frame chroma (CopyPrev) instead
                            // of leaving stale/uninit data at IIB position.
                            for (uint32_t r = 0; r < 16u; ++r) {
                                std::memcpy(frame.u.data() + (uv_y + r) * frame.chroma_stride + uv_x,
                                            prev_frame.u.data() + (uv_y + r) * prev_frame.chroma_stride + uv_x, 16u);
                                std::memcpy(frame.v.data() + (uv_y + r) * frame.chroma_stride + uv_x,
                                            prev_frame.v.data() + (uv_y + r) * prev_frame.chroma_stride + uv_x, 16u);
                            }
                        } else {
                            for (uint32_t r = 0; r < 16u; ++r) {
                                std::memcpy(frame.v.data() + (uv_y + r) * frame.chroma_stride + uv_x,
                                            mb.pixels.u.data() + r * 16u, 16u);
                                std::memcpy(frame.u.data() + (uv_y + r) * frame.chroma_stride + uv_x,
                                            mb.pixels.v.data() + r * 16u, 16u);
                            }
                        }
                        if (Bink2EffectiveHasAlpha(header, plan.packet.frame_flags) &&
                            !frame.a.empty()) {
                            for (uint32_t r = 0; r < 32u; ++r) {
                                std::memcpy(frame.a.data() + (y_y + r) * frame.luma_stride + y_x,
                                            mb.pixels.a.data() + r * 32u, 32u);
                            }
                        }
                    }

                    frame.decoded_macroblocks[(size_t)frame_row * frame.macroblock_stride + col] = 255u;
                    ++frame.macroblock_count;
                    ++frame.nonzero_macroblock_count;
                    continue;
                }

                if (type == kBink2BlockResidue) {
                    int32_t dq = 0;
                    if (!DecodeInterDq(bits, dq)) {
                        frame.decode_mb_col = col; frame.decode_mb_row = frame_row;
                        frame.failure_stage = 121u; stopped = true; break;
                    }
                    update_q(0, false);          // intra_q gets 0
                    inter_q = update_q(dq, true);

                    const uint32_t y_x = col * 32u;
                    const uint32_t y_y = frame_row * 32u;
                    uint8_t* y_dst = frame.y.data() + y_y * frame.luma_stride + y_x;
                    uint8_t* u_dst = frame.u.data() + (y_y / 2u) * frame.chroma_stride + (y_x / 2u);
                    uint8_t* v_dst = frame.v.data() + (y_y / 2u) * frame.chroma_stride + (y_x / 2u);
                    uint8_t* a_dst =
                        (Bink2EffectiveHasAlpha(header, plan.packet.frame_flags) &&
                         !frame.a.empty())
                            ? frame.a.data() + y_y * frame.luma_stride + y_x
                            : nullptr;

                    g_dbg_luma_mb_row = (int)frame_row;
                    g_dbg_luma_mb_col = (int)col;
                    g_dbg_luma_plane_tag = "Y";
                    if (!DecodeAndAddInterLumaPlane(bits, plan.packet.frame_flags,
                                                    y_cbp_inter, inter_q,
                                                    y_dst, (int)frame.luma_stride)) {
                        frame.decode_mb_col = col; frame.decode_mb_row = frame_row;
                        frame.failure_stage = 122u; stopped = true; break;
                    }

                    uint32_t chroma_flag = 0;
                    if (!bits.Read_Bit(chroma_flag)) {
                        frame.decode_mb_col = col; frame.decode_mb_row = frame_row;
                        frame.failure_stage = 123u; stopped = true; break;
                    }
                    if (chroma_flag) {
                        // Bink2 stream stores chroma in Cr,Cb (swapped) order.
                        g_dbg_chroma_mb_col = (int)col;
                        g_dbg_chroma_mb_row = (int)frame_row;
                        g_dbg_chroma_mb_plane = 'V'; // first call writes V plane
                        if (!DecodeAndAddInterChromaPlane(bits, u_cbp_inter, inter_q,
                                                          v_dst, (int)frame.chroma_stride)) {
                            frame.decode_mb_col = col; frame.decode_mb_row = frame_row;
                            frame.failure_stage = 124u; stopped = true; break;
                        }
                        g_dbg_chroma_mb_plane = 'U';
                        if (!DecodeAndAddInterChromaPlane(bits, v_cbp_inter, inter_q,
                                                          u_dst, (int)frame.chroma_stride)) {
                            frame.decode_mb_col = col; frame.decode_mb_row = frame_row;
                            frame.failure_stage = 124u; stopped = true; break;
                        }
                    } else {
                        u_cbp_inter = 0;
                        v_cbp_inter = 0;
                    }

                    if (Bink2EffectiveHasAlpha(header, plan.packet.frame_flags)) {
                        g_dbg_luma_mb_row = (int)frame_row;
                        g_dbg_luma_mb_col = (int)col;
                        g_dbg_luma_plane_tag = "A";
                        if (!DecodeAndAddInterLumaPlane(bits, plan.packet.frame_flags,
                                                         a_cbp_inter, inter_q,
                                                         a_dst, (int)frame.luma_stride)) {
                            frame.decode_mb_col = col; frame.decode_mb_row = frame_row;
                            frame.failure_stage = 125u; stopped = true; break;
                        }
                    }
                }

                if (bk2_trace_this_frame &&
                    (int)frame_row >= g_bk2_mbtrace_r0 && (int)frame_row <= g_bk2_mbtrace_r1 &&
                    (int)col      >= g_bk2_mbtrace_c0 && (int)col      <= g_bk2_mbtrace_c1) {
                    const char* tn =
                        type == kBink2BlockMotion  ? "MOTION"  :
                        type == kBink2BlockResidue ? "RESIDUE" :
                        type == kBink2BlockSkip    ? "SKIP"    :
                        type == kBink2BlockIntra   ? "INTRA"   : "?";
                    const Bink2MvBlock& mv = current_row_mv[col];
                    std::fprintf(stderr,
                        "MBTRACE f=%d r=%u c=%u type=%-7s "
                        "mv=[%d,%d|%d,%d|%d,%d|%d,%d] "
                        "qI=%d qP=%d "
                        "cbpI=[y=%x u=%x v=%x a=%x] cbpP=[y=%x u=%x v=%x a=%x]\n",
                        bk2_trace_frame_idx, (unsigned)frame_row, (unsigned)col, tn,
                        (int)mv.v[0][0], (int)mv.v[0][1],
                        (int)mv.v[1][0], (int)mv.v[1][1],
                        (int)mv.v[2][0], (int)mv.v[2][1],
                        (int)mv.v[3][0], (int)mv.v[3][1],
                        (int)current_row_intra_q[col], (int)current_row_inter_q[col],
                        (unsigned)y_cbp_intra, (unsigned)u_cbp_intra,
                        (unsigned)v_cbp_intra, (unsigned)a_cbp_intra,
                        (unsigned)y_cbp_inter, (unsigned)u_cbp_inter,
                        (unsigned)v_cbp_inter, (unsigned)a_cbp_inter);
                }

                frame.decoded_macroblocks[(size_t)frame_row * frame.macroblock_stride + col] = 255u;
                ++frame.macroblock_count;
                if (type != kBink2BlockSkip) ++frame.nonzero_macroblock_count;
            }

            prev_row_mv.swap(current_row_mv);
            prev_row_intra_q.swap(current_row_intra_q);
            prev_row_inter_q.swap(current_row_inter_q);
            prev_row_intra_flag.swap(current_row_intra_flag);
            prev_row_dc_y.swap(current_row_dc_y);
            prev_row_dc_u.swap(current_row_dc_u);
            prev_row_dc_v.swap(current_row_dc_v);
            prev_row_dc_a.swap(current_row_dc_a);
        }

        if (!stopped) ++frame.decoded_slice_count;
    }

    frame.bit_offset_end = bits.Bits_Read();
    frame.complete = !stopped &&
                     frame.decoded_slice_count == plan.packet.num_slices;
    return true;
}

bool Bink2TraceFirstKeyframeSlicePrefix(const Bink2Header& header,
                                        const Bink2FramePlan& plan,
                                        const std::vector<uint8_t>& packet,
                                        Bink2KeyframeMacroblockTrace& trace)
{
    return WalkKeyframeSlicePrefix(header, plan, packet, 0u, nullptr, &trace);
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
    Bink2DecodedMacroblock macroblock = {};
    if (!DecodeKeyframeMacroblock(bits, header, plan.packet.frame_flags, 0u, 0u,
                                  0x20u | 0x80u, 0, 0, 0,
                                  y_prev_cbp, u_prev_cbp, v_prev_cbp, a_prev_cbp,
                                  zero_y, zero_y, zero_y,
                                  zero_c, zero_c, zero_c,
                                  zero_c, zero_c, zero_c,
                                  zero_y, zero_y, zero_y,
                                  macroblock, nullptr)) return false;
    probe = macroblock.probe;
    return true;
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

static bool Bink2ProbeMacroblockBitBudgetImpl(const Bink2Header& header,
                                              const Bink2FramePlan& plan,
                                              const std::vector<uint8_t>& packet,
                                              uint32_t target_mb_col,
                                              uint32_t target_mb_row,
                                              bool interleaved,
                                              Bink2MacroblockBitBudget& budget)
{
    budget = {};

    if (!header.Is_Valid()) return false;
    if (packet.empty()) return false;
    if (plan.packet.num_slices == 0) return false;

    const uint32_t width_mb = (header.width + 31u) / 32u;
    const uint32_t slice_height = plan.packet.slice_heights[0];
    const uint32_t slice_rows = slice_height / 32u;
    if (target_mb_col >= width_mb) return false;
    if (target_mb_row >= slice_rows) return false;

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

    for (uint32_t row = 0; row <= target_mb_row; ++row) {
        std::fill(current_row_q.begin(), current_row_q.end(), 0);
        const uint32_t last_col = (row == target_mb_row) ? target_mb_col : width_mb - 1u;

        for (uint32_t col = 0; col <= last_col; ++col) {
            if (bits.Bits_Read() >= slice_end_bits) return false;

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

            if (row == target_mb_row && col == target_mb_col) {
                if (interleaved) {
                    if (!DecodeTargetMacroblockBudgetInterleaved(bits, header, plan.packet.frame_flags,
                                                                 col, row, flags,
                                                                 left_q, top_q, top_left_q,
                                                                 y_prev_cbp, u_prev_cbp,
                                                                 v_prev_cbp, a_prev_cbp,
                                                                 left_y, top_y, top_left_y,
                                                                 left_u, top_u, top_left_u,
                                                                 left_v, top_v, top_left_v,
                                                                 left_a, top_a, top_left_a,
                                                                 budget)) return false;
                } else {
                    if (!DecodeTargetMacroblockBudget(bits, header, plan.packet.frame_flags,
                                                      col, row, flags,
                                                      left_q, top_q, top_left_q,
                                                      y_prev_cbp, u_prev_cbp,
                                                      v_prev_cbp, a_prev_cbp,
                                                      left_y, top_y, top_left_y,
                                                      left_u, top_u, top_left_u,
                                                      left_v, top_v, top_left_v,
                                                      left_a, top_a, top_left_a,
                                                      budget)) return false;
                }
                return true;
            }

            Bink2DecodedMacroblock macroblock = {};
            uint32_t failure_reason = 0;
            if (!DecodeKeyframeMacroblock(bits, header, plan.packet.frame_flags,
                                          col * 32u, row * 32u,
                                          flags, left_q, top_q, top_left_q,
                                          y_prev_cbp, u_prev_cbp,
                                          v_prev_cbp, a_prev_cbp,
                                          left_y, top_y, top_left_y,
                                          left_u, top_u, top_left_u,
                                          left_v, top_v, top_left_v,
                                          left_a, top_a, top_left_a,
                                          macroblock, &failure_reason)) return false;

            current_row_q[col] = (int8_t)macroblock.probe.intra_q;
            current_row_y[col] = macroblock.probe.y_dc;
            current_row_u[col] = macroblock.probe.u_dc;
            current_row_v[col] = macroblock.probe.v_dc;
            current_row_a[col] = macroblock.probe.a_dc;
        }

        prev_row_q.swap(current_row_q);
        prev_row_y.swap(current_row_y);
        prev_row_u.swap(current_row_u);
        prev_row_v.swap(current_row_v);
        prev_row_a.swap(current_row_a);
    }

    return false;
}

namespace {

bool TraceDcDeltasPhase(Bink2BitReader& bits,
                        int32_t intra_q,
                        size_t count,
                        Bink2DcDeltaTrace& trace)
{
    trace = {};
    trace.intra_q = intra_q;
    trace.bit_offset_begin = bits.Bits_Read();

    int32_t q = std::max<int32_t>(intra_q, 8);
    if ((size_t)q >= (sizeof(kBink2gDcPat) / sizeof(kBink2gDcPat[0]))) return false;
    trace.pat = kBink2gDcPat[q];

    trace.has_delta_bit_present = true;
    uint32_t has_delta = 0;
    if (!bits.Read_Bit(has_delta)) return false;
    trace.has_delta_value = has_delta;

    if (has_delta != 0) {
        for (size_t i = 0; i < count; ++i) {
            Bink2DcDeltaTraceEntry entry = {};
            entry.index = (uint32_t)i;
            entry.bit_offset_start = bits.Bits_Read();

            uint32_t unary = 0;
            if (!bits.Read_Unary(0u, 12u, unary)) return false;
            entry.unary = unary;
            entry.bit_offset_after_unary = bits.Bits_Read();

            int32_t delta = (int32_t)unary;
            if (delta > 3) {
                uint32_t extra = 0;
                if (!bits.Read_Bits((uint32_t)delta - 3u, extra)) return false;
                entry.has_extra = true;
                entry.extra_bits = (uint32_t)delta - 3u;
                entry.extra_value = extra;
                delta = (1 << (delta - 3)) + (int32_t)extra + 2;
            }
            entry.bit_offset_after_extra = bits.Bits_Read();

            if (delta != 0) {
                uint32_t sign = 0;
                if (!bits.Read_Bit(sign)) return false;
                entry.has_sign = true;
                entry.sign_bit = sign;
                if (sign != 0) delta = -delta;
            }
            entry.bit_offset_after_sign = bits.Bits_Read();

            entry.delta = delta;
            entry.tdc = (delta * trace.pat + 0x200) >> 10;
            trace.entries.push_back(entry);
        }
    }
    trace.bit_offset_end = bits.Bits_Read();
    return true;
}

}  // namespace

bool Bink2TraceDcDeltasForMacroblock(const Bink2Header& header,
                                     const Bink2FramePlan& plan,
                                     const std::vector<uint8_t>& packet,
                                     uint32_t target_mb_col,
                                     uint32_t target_mb_row,
                                     Bink2DcDeltaTrace& y_trace,
                                     Bink2DcDeltaTrace& u_trace,
                                     Bink2DcDeltaTrace& v_trace,
                                     Bink2DcDeltaTrace& a_trace)
{
    y_trace = {};
    u_trace = {};
    v_trace = {};
    a_trace = {};

    Bink2MacroblockBitBudget budget = {};
    if (!Bink2ProbeMacroblockBitBudgetInterleaved(header, plan, packet,
                                                  target_mb_col, target_mb_row,
                                                  budget)) {
        return false;
    }

    const size_t base = budget.bit_offset_before;
    const size_t y_dc_start = base + budget.bits_dq + budget.bits_y_cbp;
    const size_t u_dc_start = y_dc_start + budget.bits_y_dc
                              + budget.bits_y_ac_total + budget.bits_u_cbp;
    const size_t v_dc_start = u_dc_start + budget.bits_u_dc
                              + budget.bits_u_ac_total + budget.bits_v_cbp;
    const size_t a_dc_start = v_dc_start + budget.bits_v_dc
                              + budget.bits_v_ac_total + budget.bits_a_cbp;

    auto replay = [&](size_t start_bit, int32_t q, size_t count,
                      Bink2DcDeltaTrace& trace) -> bool {
        Bink2BitReader bits;
        if (!bits.Reset(packet.data(), packet.size())) return false;
        if (!bits.Skip_Bits(start_bit)) return false;
        return TraceDcDeltasPhase(bits, q, count, trace);
    };

    if (!replay(y_dc_start, budget.intra_q, 16u, y_trace)) return false;
    if (!replay(u_dc_start, budget.intra_q, 4u, u_trace)) return false;
    if (!replay(v_dc_start, budget.intra_q, 4u, v_trace)) return false;
    if (budget.has_alpha) {
        if (!replay(a_dc_start, budget.intra_q, 16u, a_trace)) return false;
    }
    return true;
}

bool Bink2ProbeMacroblockBitBudget(const Bink2Header& header,
                                   const Bink2FramePlan& plan,
                                   const std::vector<uint8_t>& packet,
                                   uint32_t target_mb_col,
                                   uint32_t target_mb_row,
                                   Bink2MacroblockBitBudget& budget)
{
    return Bink2ProbeMacroblockBitBudgetImpl(header, plan, packet,
                                             target_mb_col, target_mb_row,
                                             false, budget);
}

bool Bink2ProbeMacroblockBitBudgetInterleaved(const Bink2Header& header,
                                              const Bink2FramePlan& plan,
                                              const std::vector<uint8_t>& packet,
                                              uint32_t target_mb_col,
                                              uint32_t target_mb_row,
                                              Bink2MacroblockBitBudget& budget)
{
    return Bink2ProbeMacroblockBitBudgetImpl(header, plan, packet,
                                             target_mb_col, target_mb_row,
                                             true, budget);
}
