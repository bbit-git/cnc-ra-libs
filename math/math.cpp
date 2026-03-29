/*
 * math.cpp -- Westwood math library routines.
 *
 * Facing calculations, random numbers, coordinate math, bit manipulation,
 * and fixed-point arithmetic. All ported from original x86 ASM.
 *
 * Origin: FACINGFF.ASM, FACING8.ASM, COORDA.ASM, SUPPORT.ASM,
 *         RANDOM.CPP (EA GPL3 release)
 */

#include "function.h"
#include <cstdlib>
#include <cstring>

/* ---- Random ---- */
unsigned Random(void) { return (unsigned)(rand() & 0xFF); }
int IRandom(int minv, int maxv) {
	if (minv > maxv) { int t = minv; minv = maxv; maxv = t; }
	return minv + rand() % (maxv - minv + 1);
}

/*
 * Desired_Facing256 — exact C port of FACINGFF.ASM.
 * Returns facing 0..255. 0=North, 64=East, 128=South, 192=West.
 * RA declares these as DirType in face.h and provides its own via coord.cpp.
 */
#ifdef ENGLISH
/* RA: face.h declares DirType Desired_Facing256 — provided by engine/ra/coord.cpp */
#else
int Desired_Facing256(int x1, int y1, int x2, int y2) {
    int ebx = 0;

    int ecx = x2 - x1;
    if (ecx < 0) {
        ecx = -ecx;
        ebx = 0xC0;
    }

    int eax = y1 - y2;
    if (eax < 0) {
        ebx ^= 0x40;
        eax = -eax;
    }

    int edx = (ebx & 0x40) ^ 0x40;

    if ((unsigned)eax >= (unsigned)ecx) {
        int t = eax; eax = ecx; ecx = t;
        edx ^= 0x40;
    }

    if (eax > 0xFF) {
        /* eax already fits */
    } else {
        while (ecx > 0xFF) {
            ecx >>= 1;
            eax >>= 1;
        }
    }

    unsigned result;
    if (ecx == 0) {
        result = 0xFFFFFFFF;
    } else {
        result = ((unsigned)eax << 8) / (unsigned)ecx;
    }

    result >>= 3;

    if (edx) {
        edx--;
        result = (unsigned)(-(int)result);
    }
    result += edx;
    result += ebx;

    return (int)(result & 0xFF);
}

/*
 * Desired_Facing8 — returns facing in DirType scale (0,32,64,...224).
 * 0=N, 32=NE, 64=E, 96=SE, 128=S, 160=SW, 192=W, 224=NW.
 */
int Desired_Facing8(int x1, int y1, int x2, int y2) {
    int facing256 = Desired_Facing256(x1, y1, x2, y2);
    int facing8 = ((facing256 + 16) / 32) & 7;
    return facing8 << 5;
}
#endif /* !ENGLISH */

/* Extract_String — wwlib string table accessor */
char const* Extract_String(void const* data, int index) {
    if (!data || index < 0) return "";
    unsigned short const* offsets = (unsigned short const*)data;
    int count = offsets[0] / 2;
    if (index >= count) return "";
    return (char const*)data + offsets[index];
}

void Delay(int) {}
void* Add_Long_To_Pointer(void const* ptr, long offset) { return (char*)ptr + offset; }

/* Confine_Rect — clamp position so rect stays within bounds.
 * C port of DrawMisc.cpp ASM. */
int Confine_Rect(int* x, int* y, int w, int h, int width, int height) {
    int result = 0;
    if (*x < 0) { *x = 0; result = 1; }
    else if (*x + w > width) { *x = width - w; result = 1; }
    if (*y < 0) { *y = 0; result = 1; }
    else if (*y + h > height) { *y = height - h; result = 1; }
    return result;
}

/* ---- Bit manipulation (replacing #pragma aux) ---- */
void Set_Bit(void* array, int bit, int value) {
    unsigned char* b = (unsigned char*)array; unsigned bi = (unsigned)bit >> 3; unsigned mask = 1u << ((unsigned)bit & 7);
    if (value) b[bi] |= mask; else b[bi] &= ~mask;
}
int Get_Bit(void const* array, int bit) { return (((const unsigned char*)array)[(unsigned)bit >> 3] >> ((unsigned)bit & 7)) & 1; }
int First_True_Bit(void const* array, int max_bit) {
    const unsigned char* b = (const unsigned char*)array;
    int total_bytes = (max_bit + 7) / 8;
    for (int i = 0; i < total_bytes; i++) {
        if (b[i]) return i * 8 + __builtin_ctz((unsigned)b[i]);
    }
    return max_bit;
}
int First_False_Bit(void const* array, int max_bit) {
    const unsigned char* b = (const unsigned char*)array;
    int total_bytes = (max_bit + 7) / 8;
    for (int i = 0; i < total_bytes; i++) {
        unsigned char v = ~b[i];
        if (v) return i * 8 + __builtin_ctz((unsigned)v);
    }
    return max_bit;
}

int Bound(int v, int mn, int mx) { if(mn>mx){int t=mn;mn=mx;mx=t;} return v<mn?mn:(v>mx?mx:v); }
unsigned Fixed_To_Cardinal(unsigned base, unsigned fixed) { unsigned long long r=(unsigned long long)base*fixed+0x80; if(r>>24) r=0xFFFFFF; return (unsigned)(r>>8); }
unsigned Cardinal_To_Fixed(unsigned base, unsigned cardinal) { return base?((cardinal<<8)/base):0; }

/*
 * calcx/calcy — C port of COORDA.ASM.
 * RA provides its own in coord.cpp.
 */
#ifndef ENGLISH
/*
 * Multiplies trig value by distance and extracts result.
 */
int calcx(signed short val, short distance) {
    int result = (int)val * (int)distance;
    result <<= 1;
    result >>= 8;
    return result;
}
int calcy(signed short val, short distance) {
    return -calcx(val, distance);
}
#endif /* !ENGLISH */

/* Fat pixel drawing (SUPPORT.ASM) */
void Fat_Put_Pixel(int x, int y, int color, int size, GraphicViewPortClass& vp) {
    for (int dy = 0; dy < size; dy++)
        for (int dx = 0; dx < size; dx++)
            vp.Put_Pixel(x + dx, y + dy, (unsigned char)color);
}

/* CRC — used by MIX file system to hash filenames */
static unsigned int RotL(unsigned int val, int n) { return (val << n) | (val >> (32 - n)); }
static unsigned int RotR(unsigned int val, int n) { return (val >> n) | (val << (32 - n)); }
long Calculate_CRC(void const* buffer, long length) {
    const unsigned char* bytes = (const unsigned char*)buffer;
    unsigned int crc = 0;
    long offset = 0;
    while (offset + 4 <= length) {
        unsigned int chunk = (unsigned int)bytes[offset]
            | ((unsigned int)bytes[offset+1] << 8)
            | ((unsigned int)bytes[offset+2] << 16)
            | ((unsigned int)bytes[offset+3] << 24);
        crc = RotL(crc, 1) + chunk;
        offset += 4;
    }
    long rem = length - offset;
    if (rem > 0) {
        unsigned int chunk = 0;
        for (long i = 0; i < rem; i++) {
            chunk = RotR(chunk, 8);
            chunk |= (unsigned int)bytes[offset + i] << 24;
        }
        chunk = RotR(chunk, (unsigned int)((4 - rem) * 8));
        crc = RotL(crc, 1) + chunk;
    }
    return (long)crc;
}
