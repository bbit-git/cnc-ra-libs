/*
 * compat.cpp -- POSIX/Linux replacements for Win32/MSVC C runtime functions.
 *
 * _makepath, _splitpath, strupr, strlwr, memicmp, strtrim, IsBadReadPtr.
 *
 * Origin: MSVC C runtime, WIN32LIB/SUPPORT.ASM (EA GPL3 release)
 */

#include "function.h"
#include <cstring>
#include <cctype>
#include <cstdint>

void _makepath(char* path, const char* drive, const char* dir, const char* fname, const char* ext) {
    path[0] = '\0';
    if (drive && drive[0]) { strcat(path, drive); strcat(path, ":"); }
    if (dir && dir[0]) strcat(path, dir);
    if (fname && fname[0]) strcat(path, fname);
    if (ext && ext[0]) { if (ext[0] != '.') strcat(path, "."); strcat(path, ext); }
}

void _splitpath(const char* path, char* drive, char* dir, char* fname, char* ext) {
    if (drive) drive[0] = '\0'; if (dir) dir[0] = '\0';
    if (fname) fname[0] = '\0'; if (ext) ext[0] = '\0';
    if (!path) return;
    const char* last_sep = nullptr; const char* last_dot = nullptr;
    for (const char* p = path; *p; ++p) { if (*p == '/' || *p == '\\') last_sep = p; if (*p == '.') last_dot = p; }
    const char* fn = last_sep ? last_sep + 1 : path;
    if (dir && last_sep) { int l = (int)(last_sep - path + 1); memcpy(dir, path, l); dir[l] = '\0'; }
    if (last_dot && last_dot > fn) { if (fname) { int l = (int)(last_dot - fn); memcpy(fname, fn, l); fname[l] = '\0'; } if (ext) strcpy(ext, last_dot); }
    else { if (fname) strcpy(fname, fn); }
}

char* strupr(char* s) {
    if (!s) return s;
    for (char* p = s; *p; ++p) *p = (char)toupper((unsigned char)*p);
    return s;
}

char* strlwr(char* s) {
    if (!s) return s;
    for (char* p = s; *p; ++p) *p = (char)tolower((unsigned char)*p);
    return s;
}

int memicmp(void const* s1, void const* s2, size_t n) {
    return strncasecmp((const char*)s1, (const char*)s2, n);
}

void strtrim(char* buffer) {
    if (!buffer) return;

    /* Strip leading whitespace. */
    char* src = buffer;
    while (*src == ' ' || *src == '\t') src++;
    if (src != buffer) memmove(buffer, src, strlen(src) + 1);

    /* Strip trailing whitespace. */
    char* end = buffer + strlen(buffer) - 1;
    while (end >= buffer && (*end == ' ' || *end == '\t')) *end-- = '\0';
}

int IsBadReadPtr(void const* lp, unsigned int) {
    if (!lp) return 1;
    uintptr_t addr = (uintptr_t)lp;
    if (addr < 0x10000) return 1;
    return 0;
}

/* Interpolation (INTERPAL.ASM) — 2x scaling */
extern "C" {
void Asm_Create_Palette_Interpolation_Table(void) {}

void Asm_Interpolate(void* src_ptr, void* dest_ptr, int height, int src_width, int dest_width) {
    unsigned char* sp = (unsigned char*)src_ptr;
    unsigned char* dp = (unsigned char*)dest_ptr;
    int dest_row = dest_width / 2;
    if (dest_row <= src_width) {
        for (int y = 0; y < height; y++) {
            memcpy(dp, sp, src_width);
            sp += src_width;
            dp += src_width;
        }
    } else {
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < src_width; x++) {
                dp[x * 2]     = sp[x];
                dp[x * 2 + 1] = sp[x];
            }
            memcpy(dp + dest_row, dp, src_width * 2);
            sp += src_width;
            dp += dest_width;
        }
    }
}

void Asm_Interpolate_Line_Double(void* src_ptr, void* dest_ptr, int height, int src_width, int dest_width) {
    Asm_Interpolate(src_ptr, dest_ptr, height, src_width, dest_width);
}

void Asm_Interpolate_Line_Interpolate(void* src_ptr, void* dest_ptr, int height, int src_width, int dest_width) {
    Asm_Interpolate(src_ptr, dest_ptr, height, src_width, dest_width);
}
}

void Convert_RGB_To_HSV(unsigned, unsigned, unsigned, unsigned*, unsigned*, unsigned*) {}
void Convert_HSV_To_RGB(unsigned, unsigned, unsigned, unsigned*, unsigned*, unsigned*) {}
