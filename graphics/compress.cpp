/*
 * compress.cpp -- LCW compression, XOR delta, and Uncompress_Data.
 *
 * LCW decompression ported from CnC_Remastered_Collection/REDALERT/LCWUNCMP.CPP (GPL3).
 * XOR delta decoder ported from XORDELTA.ASM (GPL3).
 * Uncompress_Data parses CompHeaderType and dispatches to LCW.
 * Load_Uncompress reads compressed data from files via GraphicBufferClass.
 *
 * Origin: WIN32LIB/LCWUNCMP.CPP, WIN32LIB/XORDELTA.ASM, WIN32LIB/LOADFILE.CPP (EA GPL3)
 */

#include "function.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>

/* Legacy stubs — RA provides its own in lcw.cpp */
#ifndef ENGLISH
int LCW_Uncomp(void const*, void*, unsigned long) { return 0; }
unsigned long LCW_Comp(void const*, void*, unsigned long) { return 0; }
#endif

/* LCW decompression — third parameter used as source length for bounds checking. */
unsigned long LCW_Uncompress(void const* source, void* dest, unsigned long source_len, unsigned long dest_max) {
    unsigned char *source_ptr = (unsigned char*)source;
    unsigned char *source_end = source_ptr + source_len;
    unsigned char *dest_ptr = (unsigned char*)dest;
    unsigned char *dest_end = dest_max ? (unsigned char*)dest + dest_max : nullptr;
    unsigned char *copy_ptr, op_code, data;
    unsigned count;

#define LCW_DEST_CHECK(n) if (dest_end && dest_ptr + (n) > dest_end) { \
    count = (unsigned)(dest_end - dest_ptr); if (!count) goto done; }

    while (source_ptr < source_end) {
        op_code = *source_ptr++;
        if (!(op_code & 0x80)) {
            count = (op_code >> 4) + 3;
            if (source_ptr >= source_end) break;
            copy_ptr = dest_ptr - ((unsigned)*source_ptr++ + (((unsigned)op_code & 0x0f) << 8));
            LCW_DEST_CHECK(count);
            while (count--) *dest_ptr++ = *copy_ptr++;
        } else {
            if (!(op_code & 0x40)) {
                if (op_code == 0x80) {
                    return (unsigned long)(dest_ptr - (unsigned char*)dest);
                } else {
                    count = op_code & 0x3f;
                    if (source_ptr + count > source_end) count = (unsigned)(source_end - source_ptr);
                    LCW_DEST_CHECK(count);
                    while (count--) *dest_ptr++ = *source_ptr++;
                }
            } else {
                if (op_code == 0xfe) {
                    if (source_ptr + 3 > source_end) break;
                    count = *source_ptr + ((unsigned)*(source_ptr + 1) << 8);
                    data = *(source_ptr + 2);
                    source_ptr += 3;
                    LCW_DEST_CHECK(count);
                    while (count--) *dest_ptr++ = data;
                } else if (op_code == 0xff) {
                    if (source_ptr + 4 > source_end) break;
                    count = *source_ptr + ((unsigned)*(source_ptr + 1) << 8);
                    copy_ptr = (unsigned char*)dest + *(source_ptr + 2) + ((unsigned)*(source_ptr + 3) << 8);
                    source_ptr += 4;
                    LCW_DEST_CHECK(count);
                    while (count--) *dest_ptr++ = *copy_ptr++;
                } else {
                    if (source_ptr + 2 > source_end) break;
                    count = (op_code & 0x3f) + 3;
                    copy_ptr = (unsigned char*)dest + *source_ptr + ((unsigned)*(source_ptr + 1) << 8);
                    source_ptr += 2;
                    LCW_DEST_CHECK(count);
                    while (count--) *dest_ptr++ = *copy_ptr++;
                }
            }
        }
    }
done:
    return (unsigned long)(dest_ptr - (unsigned char*)dest);
#undef LCW_DEST_CHECK
}

unsigned long Uncompress_Data(void const* src, void* dst) {
    /* Parse CompHeaderType at start of source buffer and decompress.
    ** On-disk/in-memory layout: [Method:1][pad:1][Size:4][Skip:2] = 8 bytes */
    if (!src || !dst) return 0;
    const uint8_t* s = (const uint8_t*)src;
    uint8_t method = s[0];
    uint32_t uncomp_size = *(const uint32_t*)(s + 2);
    uint16_t skip = *(const uint16_t*)(s + 6);
    const uint8_t* data = s + 8 + skip;

    DBG_GFX("Uncompress_Data: method=%d size=%u skip=%u", method, uncomp_size, skip);
    switch (method) {
        case 0: /* NOCOMPRESS */
            memcpy(dst, data, uncomp_size);
            return uncomp_size;
        case 4: /* LCW */
            return LCW_Uncompress(data, dst, uncomp_size * 2, uncomp_size);
        default:
            DBG_GFX("Uncompress_Data: unsupported method %d", method);
            return 0;
    }
}

/* XOR Delta decoder — ported from XORDELTA.ASM (GPL3).
 * Apply_XOR_Delta operates on a linear buffer (no row pitch). */
void Apply_XOR_Delta(void* target, void const* delta)
{
    unsigned char* dst = (unsigned char*)target;
    const unsigned char* src = (const unsigned char*)delta;

    for (;;) {
        unsigned int cmd = *src++;
        if (cmd == 0) {
            /* SHORTRUN */
            unsigned int count = *src++;
            unsigned char val = *src++;
            while (count--) { *dst++ ^= val; }
        } else if (cmd < 0x80) {
            /* SHORTDUMP */
            unsigned int count = cmd;
            while (count--) { *dst++ ^= *src++; }
        } else {
            unsigned int value = cmd - 0x80;
            if (value != 0) {
                /* SHORTSKIP */
                dst += value;
            } else {
                unsigned short word = src[0] | ((unsigned short)src[1] << 8);
                src += 2;
                short sword = (short)word;
                if (sword > 0) {
                    /* LONGSKIP */
                    dst += word;
                } else if (sword == 0) {
                    /* STOP */
                    break;
                } else {
                    unsigned int code = word & 0x7FFF;
                    if (code & 0x4000) {
                        /* LONGRUN */
                        unsigned int count = code & 0x3FFF;
                        unsigned char val = *src++;
                        while (count--) { *dst++ ^= val; }
                    } else {
                        /* LONGDUMP */
                        unsigned int count = code;
                        while (count--) { *dst++ ^= *src++; }
                    }
                }
            }
        }
    }
}

/* Apply_XOR_Delta_To_Page_Or_Viewport — handles row pitch.
 * 'width' is the animation width, 'nextrow' is the destination row stride.
 * 'copy' controls XOR (0) vs COPY (1). */
void Apply_XOR_Delta_To_Page_Or_Viewport(void* target, void const* delta, int width, int nextrow, int copy)
{
    unsigned char* dst = (unsigned char*)target;
    const unsigned char* src = (const unsigned char*)delta;
    int col = 0;
    int bx = width;

    #define ADV_COL() do { \
        col++; dst++; \
        if (col >= bx) { \
            dst -= col; \
            col = 0; \
            dst += nextrow; \
        } \
    } while(0)

    #define DO_SKIP(n) do { \
        unsigned int _skip = (n); \
        dst -= col; \
        col += _skip; \
        while (col >= bx) { \
            col -= bx; \
            dst += nextrow; \
        } \
        dst += col; \
    } while(0)

    for (;;) {
        unsigned int cmd = *src++;
        if (cmd == 0) {
            unsigned int count = *src++;
            unsigned char val = *src++;
            while (count--) {
                if (copy) *dst = val; else *dst ^= val;
                ADV_COL();
            }
        } else if (cmd < 0x80) {
            unsigned int count = cmd;
            while (count--) {
                unsigned char val = *src++;
                if (copy) *dst = val; else *dst ^= val;
                ADV_COL();
            }
        } else {
            unsigned int value = cmd - 0x80;
            if (value != 0) {
                DO_SKIP(value);
            } else {
                unsigned short word = src[0] | ((unsigned short)src[1] << 8);
                src += 2;
                short sword = (short)word;
                if (sword > 0) {
                    DO_SKIP(word);
                } else if (sword == 0) {
                    break;
                } else {
                    unsigned int code = word & 0x7FFF;
                    if (code & 0x4000) {
                        unsigned int count = code & 0x3FFF;
                        unsigned char val = *src++;
                        while (count--) {
                            if (copy) *dst = val; else *dst ^= val;
                            ADV_COL();
                        }
                    } else {
                        unsigned int count = code;
                        while (count--) {
                            unsigned char val = *src++;
                            if (copy) *dst = val; else *dst ^= val;
                            ADV_COL();
                        }
                    }
                }
            }
        }
    }

    #undef ADV_COL
    #undef DO_SKIP
}

/* Load_Uncompress — read compressed data from file into GraphicBufferClass */
long Load_Uncompress(FileClass &file, GraphicBufferClass &uncomp_buff, GraphicBufferClass &dest_buff, void *reserved_data) {
    unsigned short size;
    void *sptr = uncomp_buff.Get_Buffer();
    void *dptr = dest_buff.Get_Buffer();
    int opened = false;
    CompHeaderType header;

    if (!sptr || !dptr) return 0;

    if (!file.Is_Open()) {
        if (!file.Open()) return 0;
        opened = true;
    }

    file.Read(&size, sizeof(size));
    file.Read(&header, sizeof(header));
    size -= sizeof(header);

    if (header.Skip) {
        size -= header.Skip;
        if (reserved_data) {
            file.Read(reserved_data, header.Skip);
        } else {
            file.Seek(header.Skip, SEEK_CUR);
        }
        header.Skip = 0;
    }

    long buf_size = uncomp_buff.Get_Width() * uncomp_buff.Get_Height();
    if (uncomp_buff.Get_Buffer() == dest_buff.Get_Buffer()) {
        sptr = (char*)sptr + buf_size - (size + sizeof(header));
    }

    memcpy(sptr, &header, sizeof(header));
    file.Read((char*)sptr + sizeof(header), size);

    size = (unsigned short)Uncompress_Data(sptr, dptr);

    if (opened) file.Close();
    return (long)size;
}
