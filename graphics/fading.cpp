/*
 * fading.cpp -- Palette fading table builders.
 *
 * Build_Fading_Table: build a 256-byte color remap table that fades
 * each palette entry toward a target color at the given intensity.
 *
 * Conquer_Build_Fading_Table: game-specific variant from SUPPORT.ASM.
 *
 * Origin: WIN32LIB/SUPPORT.ASM (EA GPL3 release)
 */

#include "function.h"
#include <climits>

void* Build_Fading_Table(void const* palette, void* dest, int color, int frac) {
    if (!palette || !dest) return dest;
    unsigned char const* pal = (unsigned char const*)palette;
    unsigned char* table = (unsigned char*)dest;
    int tr = pal[color * 3 + 0];
    int tg = pal[color * 3 + 1];
    int tb = pal[color * 3 + 2];
    for (int i = 0; i < 256; i++) {
        int sr = pal[i * 3 + 0];
        int sg = pal[i * 3 + 1];
        int sb = pal[i * 3 + 2];
        int fr = sr + ((tr - sr) * frac) / 256;
        int fg = sg + ((tg - sg) * frac) / 256;
        int fb = sb + ((tb - sb) * frac) / 256;
        int best = 0, best_dist = INT_MAX;
        for (int j = 0; j < 256; j++) {
            int dr = pal[j*3+0] - fr;
            int dg = pal[j*3+1] - fg;
            int db = pal[j*3+2] - fb;
            int dist = dr*dr + dg*dg + db*db;
            if (dist < best_dist) { best_dist = dist; best = j; }
        }
        table[i] = (unsigned char)best;
    }
    return dest;
}

void* Make_Palette_Table(void const*) { return nullptr; }

void* Conquer_Build_Fading_Table(void const* palette, void* dest, int color, int frac) {
    if (!dest) return dest;
    unsigned char* pal = (unsigned char*)palette;
    unsigned char* table = (unsigned char*)dest;
    unsigned char tr = pal[color * 3 + 0];
    unsigned char tg = pal[color * 3 + 1];
    unsigned char tb = pal[color * 3 + 2];
    for (int i = 0; i < 256; i++) {
        int r = pal[i*3+0] + (((int)tr - pal[i*3+0]) * frac) / 256;
        int g = pal[i*3+1] + (((int)tg - pal[i*3+1]) * frac) / 256;
        int b = pal[i*3+2] + (((int)tb - pal[i*3+2]) * frac) / 256;
        int best = 0, best_dist = 999999;
        for (int j = 0; j < 256; j++) {
            int dr = r - pal[j*3+0], dg = g - pal[j*3+1], db = b - pal[j*3+2];
            int dist = dr*dr + dg*dg + db*db;
            if (dist < best_dist) { best_dist = dist; best = j; }
        }
        table[i] = (unsigned char)best;
    }
    return dest;
}
