/*
 * shp.cpp -- SHP shape file rendering and extraction.
 *
 * Buffer_Frame_To_Page: complete C port of KEYFBUFF.ASM (D. Dettmer, 1995).
 * Primary shape/sprite rendering function for Command & Conquer.
 * Supports 16 rendering modes (transparency, ghost, fading, predator).
 *
 * Extract_Shape / Get_Shape_*: SHP file format accessors.
 *
 * Origin: WIN32LIB/KEYFBUFF.ASM, WIN32LIB/SHAPE.CPP (EA GPL3 release)
 */

#include "function.h"
#include <cstdlib>
#include <cstring>
#include <cstdarg>

/* SHP frame header */
#pragma pack(push, 1)
typedef struct {
    uint16_t ShapeType;
    uint8_t  Height;
    uint16_t Width;
    uint8_t  OriginalHeight;
    uint16_t ShapeSize;
    uint16_t DataLength;
} Shape_Type;
#pragma pack(pop)

/* SHAPE_TRANS: 0x0040 -- defined in CONQUER.CPP / KEYFBUFF.ASM, not in the enum.
 * We define it locally since callers pass it but it is absent from td_platform.h. */
#ifndef SHAPE_TRANS
#define SHAPE_TRANS 0x0040
#endif

/* Predator shimmer displacement table -- matches BFPredTable in KEYFBUFF.ASM. */
static const int16_t BFPredTable[8] = { 1, 3, 2, 5, 2, 3, 4, 1 };

/* PRED_MASK: the predator offset table index wraps with this mask (0x0E). */
#define PRED_MASK 0x0E

long Buffer_Frame_To_Page(int x, int y, int w, int h, void* shape_buf,
                          GraphicViewPortClass& view, int flags, ...) {
    //DBG_GFX("BF2P: x=%d y=%d %dx%d flags=%x buf=%p", x, y, w, h, flags, shape_buf);
    if (!shape_buf || w <= 0 || h <= 0) return 0;

    unsigned char* src = (unsigned char*)shape_buf;
    unsigned char* dst = (unsigned char*)view.Get_Buffer();
    if (!dst) return 0;

    int vw = view.Get_Width();
    int vh = view.Get_Height();
    int pitch = view.Get_Full_Pitch();

    if (flags & SHAPE_CENTER) {
        x -= w / 2;
        y -= h / 2;
    }

    /* Parse varargs in the exact order the ASM expects */
    unsigned char const* IsTranslucent = nullptr;
    unsigned char const* Translucent = nullptr;
    unsigned char const* FadingTable = nullptr;
    int FadingNum = 0;
    int pred_off = 0;
    int part_pred = 0x100;
    int part_count = 0;

    va_list args;
    va_start(args, flags);

    if (flags & SHAPE_GHOST) {
        unsigned char const* ghost_ptr = (unsigned char const*)va_arg(args, void const*);
        if (ghost_ptr) {
            IsTranslucent = ghost_ptr;
            Translucent = ghost_ptr + 256;
        }
    }

    if (flags & SHAPE_FADING) {
        FadingTable = (unsigned char const*)va_arg(args, void const*);
        FadingNum = va_arg(args, int) & 0x3F;
        if (FadingNum == 0) {
            flags &= ~SHAPE_FADING;
        }
    }

    if (flags & SHAPE_PREDATOR) {
        int pred_arg = va_arg(args, int);
        pred_arg <<= 1;
        if (pred_arg < 0) pred_arg = -pred_arg;
        pred_off = pred_arg & PRED_MASK;
        part_count = 0;
        part_pred = 0x100;
    }

    if (flags & SHAPE_PARTIAL) {
        part_pred = va_arg(args, int) & 0xFF;
    }

    va_end(args);

    /* Build internal 4-bit flag word for the drawing mode */
    int jflags = 0;
    if (flags & SHAPE_TRANS)    jflags |= 1;
    if (flags & SHAPE_GHOST)    jflags |= 2;
    if (flags & SHAPE_FADING)   jflags |= 4;
    if (flags & SHAPE_PREDATOR) jflags |= 8;

    /* Clip source rectangle against destination viewport bounds */
    int scr_x = 0, scr_y = 0;
    int x1 = x + w;
    int y1 = y + h;

    if (x < 0)    { scr_x = -x; x = 0; }
    if (y < 0)    { scr_y = -y; y = 0; }
    if (x1 > vw)  { x1 = vw; }
    if (y1 > vh)  { y1 = vh; }

    int draw_w = x1 - x;
    int draw_h = y1 - y;
    if (draw_w <= 0 || draw_h <= 0) return 0;

    unsigned char* sp = src + scr_y * w + scr_x;
    unsigned char* dp = dst + y * pitch + x;

    /* Main rendering loop — 16 modes */
    for (int row = 0; row < draw_h; row++) {

        switch (jflags) {

        case 0: /* BF_Copy */
            memcpy(dp, sp, draw_w);
            break;

        case 1: /* BF_Trans */
            for (int col = 0; col < draw_w; col++) {
                unsigned char pixel = sp[col];
                if (pixel) dp[col] = pixel;
            }
            break;

        case 2: /* BF_Ghost */
            for (int col = 0; col < draw_w; col++) {
                unsigned char pixel = sp[col];
                unsigned char trans_idx = IsTranslucent[pixel];
                if (trans_idx != 0xFF) {
                    pixel = Translucent[((unsigned)trans_idx << 8) + dp[col]];
                }
                dp[col] = pixel;
            }
            break;

        case 3: /* BF_Ghost_Trans */
            for (int col = 0; col < draw_w; col++) {
                unsigned char pixel = sp[col];
                if (pixel == 0) continue;
                unsigned char trans_idx = IsTranslucent[pixel];
                if (trans_idx != 0xFF) {
                    pixel = Translucent[((unsigned)trans_idx << 8) + dp[col]];
                }
                dp[col] = pixel;
            }
            break;

        case 4: /* BF_Fading */
            for (int col = 0; col < draw_w; col++) {
                unsigned char pixel = sp[col];
                for (int f = 0; f < FadingNum; f++) pixel = FadingTable[pixel];
                dp[col] = pixel;
            }
            break;

        case 5: /* BF_Fading_Trans */
            for (int col = 0; col < draw_w; col++) {
                unsigned char pixel = sp[col];
                if (pixel == 0) continue;
                for (int f = 0; f < FadingNum; f++) pixel = FadingTable[pixel];
                dp[col] = pixel;
            }
            break;

        case 6: /* BF_Ghost_Fading */
            for (int col = 0; col < draw_w; col++) {
                unsigned char pixel = sp[col];
                unsigned char trans_idx = IsTranslucent[pixel];
                if (trans_idx != 0xFF) {
                    pixel = Translucent[((unsigned)trans_idx << 8) + dp[col]];
                }
                for (int f = 0; f < FadingNum; f++) pixel = FadingTable[pixel];
                dp[col] = pixel;
            }
            break;

        case 7: /* BF_Ghost_Fading_Trans */
            for (int col = 0; col < draw_w; col++) {
                unsigned char pixel = sp[col];
                if (pixel == 0) continue;
                unsigned char trans_idx = IsTranslucent[pixel];
                if (trans_idx != 0xFF) {
                    pixel = Translucent[((unsigned)trans_idx << 8) + dp[col]];
                }
                for (int f = 0; f < FadingNum; f++) pixel = FadingTable[pixel];
                dp[col] = pixel;
            }
            break;

        case 8: /* BF_Predator */
            for (int col = 0; col < draw_w; col++) {
                int new_part = part_count + part_pred;
                if (new_part >> 8) {
                    part_count = new_part & 0xFF;
                    int disp = BFPredTable[pred_off >> 1];
                    pred_off = (pred_off + 2) & PRED_MASK;
                    int ri = col + disp;
                    if (ri >= 0 && ri < pitch) {
                        dp[col] = dp[ri];
                    }
                } else {
                    part_count = new_part & 0xFFFF;
                }
            }
            break;

        case 9: /* BF_Predator_Trans */
            for (int col = 0; col < draw_w; col++) {
                unsigned char pixel = sp[col];
                if (pixel == 0) continue;
                int new_part = part_count + part_pred;
                if (new_part >> 8) {
                    part_count = new_part & 0xFF;
                    int disp = BFPredTable[pred_off >> 1];
                    pred_off = (pred_off + 2) & PRED_MASK;
                    int ri = col + disp;
                    if (ri >= 0 && ri < pitch) pixel = dp[ri];
                } else {
                    part_count = new_part & 0xFFFF;
                }
                dp[col] = pixel;
            }
            break;

        case 10: /* BF_Predator_Ghost */
            for (int col = 0; col < draw_w; col++) {
                unsigned char pixel = sp[col];
                int new_part = part_count + part_pred;
                if (new_part >> 8) {
                    part_count = new_part & 0xFF;
                    int disp = BFPredTable[pred_off >> 1];
                    pred_off = (pred_off + 2) & PRED_MASK;
                    int ri = col + disp;
                    if (ri >= 0 && ri < pitch) pixel = dp[ri];
                } else {
                    part_count = new_part & 0xFFFF;
                }
                unsigned char trans_idx = IsTranslucent[pixel];
                if (trans_idx != 0xFF) {
                    pixel = Translucent[((unsigned)trans_idx << 8) + dp[col]];
                }
                dp[col] = pixel;
            }
            break;

        case 11: /* BF_Predator_Ghost_Trans */
            for (int col = 0; col < draw_w; col++) {
                unsigned char pixel = sp[col];
                if (pixel == 0) continue;
                int new_part = part_count + part_pred;
                if (new_part >> 8) {
                    part_count = new_part & 0xFF;
                    int disp = BFPredTable[pred_off >> 1];
                    pred_off = (pred_off + 2) & PRED_MASK;
                    int ri = col + disp;
                    if (ri >= 0 && ri < pitch) pixel = dp[ri];
                } else {
                    part_count = new_part & 0xFFFF;
                }
                unsigned char trans_idx = IsTranslucent[pixel];
                if (trans_idx != 0xFF) {
                    pixel = Translucent[((unsigned)trans_idx << 8) + dp[col]];
                }
                dp[col] = pixel;
            }
            break;

        case 12: /* BF_Predator_Fading */
            for (int col = 0; col < draw_w; col++) {
                unsigned char pixel = sp[col];
                int new_part = part_count + part_pred;
                if (new_part >> 8) {
                    part_count = new_part & 0xFF;
                    int disp = BFPredTable[pred_off >> 1];
                    pred_off = (pred_off + 2) & PRED_MASK;
                    int ri = col + disp;
                    if (ri >= 0 && ri < pitch) pixel = dp[ri];
                } else {
                    part_count = new_part & 0xFFFF;
                }
                for (int f = 0; f < FadingNum; f++) pixel = FadingTable[pixel];
                dp[col] = pixel;
            }
            break;

        case 13: /* BF_Predator_Fading_Trans */
            for (int col = 0; col < draw_w; col++) {
                unsigned char pixel = sp[col];
                if (pixel == 0) continue;
                int new_part = part_count + part_pred;
                if (new_part >> 8) {
                    part_count = new_part & 0xFF;
                    int disp = BFPredTable[pred_off >> 1];
                    pred_off = (pred_off + 2) & PRED_MASK;
                    int ri = col + disp;
                    if (ri >= 0 && ri < pitch) pixel = dp[ri];
                } else {
                    part_count = new_part & 0xFFFF;
                }
                for (int f = 0; f < FadingNum; f++) pixel = FadingTable[pixel];
                dp[col] = pixel;
            }
            break;

        case 14: /* BF_Predator_Ghost_Fading */
            for (int col = 0; col < draw_w; col++) {
                unsigned char pixel = sp[col];
                int new_part = part_count + part_pred;
                if (new_part >> 8) {
                    part_count = new_part & 0xFF;
                    int disp = BFPredTable[pred_off >> 1];
                    pred_off = (pred_off + 2) & PRED_MASK;
                    int ri = col + disp;
                    if (ri >= 0 && ri < pitch) pixel = dp[ri];
                } else {
                    part_count = new_part & 0xFFFF;
                }
                unsigned char trans_idx = IsTranslucent[pixel];
                if (trans_idx != 0xFF) {
                    pixel = Translucent[((unsigned)trans_idx << 8) + dp[col]];
                }
                for (int f = 0; f < FadingNum; f++) pixel = FadingTable[pixel];
                dp[col] = pixel;
            }
            break;

        case 15: /* BF_Predator_Ghost_Fading_Trans */
            for (int col = 0; col < draw_w; col++) {
                unsigned char pixel = sp[col];
                if (pixel == 0) continue;
                int new_part = part_count + part_pred;
                if (new_part >> 8) {
                    part_count = new_part & 0xFF;
                    int disp = BFPredTable[pred_off >> 1];
                    pred_off = (pred_off + 2) & PRED_MASK;
                    int ri = col + disp;
                    if (ri >= 0 && ri < pitch) pixel = dp[ri];
                } else {
                    part_count = new_part & 0xFFFF;
                }
                unsigned char trans_idx = IsTranslucent[pixel];
                if (trans_idx != 0xFF) {
                    pixel = Translucent[((unsigned)trans_idx << 8) + dp[col]];
                }
                for (int f = 0; f < FadingNum; f++) pixel = FadingTable[pixel];
                dp[col] = pixel;
            }
            break;

        } /* end switch(jflags) */

        sp += w;
        dp += pitch;
    }

    return 0;
}

/*
 * SHP shape file format accessors.
 */
void* Extract_Shape(void const* buffer, int shape) {
    if (!buffer || shape < 0) return nullptr;
    const uint8_t* base = (const uint8_t*)buffer;
    uint16_t num_shapes = *(const uint16_t*)base;
    if (shape >= num_shapes) return nullptr;
    const uint32_t* offsets = (const uint32_t*)(base + 2);
    uint32_t offset = offsets[shape];
    return (void*)(base + 2 + offset);
}

int Extract_Shape_Count(void const* buffer) {
    if (!buffer) return 0;
    return *(const uint16_t*)buffer;
}

int Get_Shape_Width(void const* shape) {
    if (!shape) return 0;
    return ((const Shape_Type*)shape)->Width;
}

int Get_Shape_Height(void const* shape) {
    if (!shape) return 0;
    return ((const Shape_Type*)shape)->Height;
}

int Get_Shape_Uncomp_Size(void const* shape) {
    if (!shape) return 0;
    return ((const Shape_Type*)shape)->DataLength;
}

int Get_Shape_Original_Height(void const* shape) {
    if (!shape) return 0;
    return ((const Shape_Type*)shape)->OriginalHeight;
}

void Set_Shape_Buffer(void* buffer, long size) {
    _ShapeBuffer = (decltype(_ShapeBuffer))buffer;
    _ShapeBufferSize = size;
}

void Write_PCX_File(char const*, GraphicViewPortClass&, unsigned char*) {}
