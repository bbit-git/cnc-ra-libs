/*
 * wsa.cpp -- WSA (Westwood Studios Animation) decoder.
 *
 * File format: 14-byte header, (total_frames+2)*4 byte offset table,
 * optional 768-byte palette, LCW-compressed XOR delta frames.
 *
 * Origin: WIN32LIB/WSA.CPP + XORDELTA.ASM (EA GPL3 release)
 */

#include "function.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>

/* Forward declarations for file I/O helpers */
static int  wsa_ccfile_read(CCFileClass* f, void* buf, int nbytes) { return f->Read(buf, nbytes); }
static int  wsa_ccfile_seek(CCFileClass* f, long offset, int whence) { return f->Seek(offset, whence); }
static void wsa_ccfile_close(CCFileClass* f) { f->Close(); }

struct WSAHandle {
    unsigned short current_frame;
    unsigned short total_frames;
    unsigned short pixel_x, pixel_y;
    unsigned short pixel_width, pixel_height;
    unsigned short largest_frame_size;
    short flags;
    char* delta_buffer;    /* largest_frame_size bytes — LCW decompress target */
    char* file_buffer;     /* offset table + frame data loaded from file */
    unsigned long anim_mem_size;
};

/* Internal flags (must match WSA.CPP constants) */
#define WSA_SYS_ALLOCATED      0x02
#define WSA_RESIDENT           0x08
#define WSA_TARGET_IN_BUFFER   0x10
#define WSA_LINEAR_ONLY        0x20
#define WSA_FRAME_0_ON_PAGE    0x40
#define WSA_PALETTE_PRESENT    0x100
#define WSA_FRAME_0_IS_DELTA   0x200

/* On-disk header (14 bytes) */
#pragma pack(push, 1)
struct WSA_FileHeader {
    unsigned short total_frames;
    unsigned short pixel_x, pixel_y;
    unsigned short pixel_width, pixel_height;
    unsigned short largest_frame_size;
    short flags;
};
#pragma pack(pop)
#define WSA_FILE_HEADER_SIZE 14

/* The ANIMATE.EXE tool added sizeof(short)+sizeof(long) to largest_frame_size.
** On Win32: 2+4=6 bytes. Must use fixed sizes for LP64 compatibility. */
#define WSA_EXTRA_BYTES (2 + 4)

static uint32_t WSA_Get_Resident_Frame_Offset(char* file_buffer, int frame)
{
    uint32_t* lptr = (uint32_t*)file_buffer;
    uint32_t frame0_size = 0;
    if (lptr[0]) {
        frame0_size = lptr[1] - lptr[0];
    }
    uint32_t val = lptr[frame];
    if (val)
        return val - (frame0_size + WSA_FILE_HEADER_SIZE);
    return 0;
}

static BOOL WSA_Apply_Delta(WSAHandle* sys_header, int curr_frame, char* dest_ptr, int dest_w)
{
    uint32_t frame_offset = WSA_Get_Resident_Frame_Offset(sys_header->file_buffer, curr_frame);
    uint32_t next_offset = WSA_Get_Resident_Frame_Offset(sys_header->file_buffer, curr_frame + 1);
    uint32_t frame_data_size = next_offset - frame_offset;

    if (!frame_offset || !frame_data_size) return FALSE;

    char* data_ptr = sys_header->file_buffer + frame_offset;
    char* delta_back = sys_header->delta_buffer + sys_header->largest_frame_size - frame_data_size;
    memcpy(delta_back, data_ptr, frame_data_size);

    LCW_Uncompress(delta_back, sys_header->delta_buffer, sys_header->largest_frame_size);

    if (sys_header->flags & WSA_TARGET_IN_BUFFER) {
        Apply_XOR_Delta(dest_ptr, sys_header->delta_buffer);
    } else {
        Apply_XOR_Delta_To_Page_Or_Viewport(dest_ptr, sys_header->delta_buffer,
                                            sys_header->pixel_width, dest_w, 0 /*DO_XOR*/);
    }
    return TRUE;
}

void* Open_Animation(char const* file_name, char* user_buffer, long user_buffer_size, int user_flags, void* palette)
{
    if (!file_name) return nullptr;

    CCFileClass* f = new CCFileClass((char*)file_name);
    if (!f || !f->Open(READ)) {
        DBG_GFX("Open_Animation: FAILED to open '%s'", file_name);
        delete f; return nullptr;
    }
    DBG_GFX("Open_Animation: opened '%s' (size=%ld)", file_name, f->Size());

    /* Read 14-byte on-disk header */
    WSA_FileHeader file_header;
    memset(&file_header, 0, sizeof(file_header));
    f->Read(&file_header, WSA_FILE_HEADER_SIZE);

    int anim_flags = 0;
    int palette_adjust = 0;

    if (file_header.flags & 1) {
        anim_flags |= WSA_PALETTE_PRESENT;
        palette_adjust = 768;
    }
    if (file_header.flags & 2) {
        anim_flags |= WSA_FRAME_0_IS_DELTA;
    }

    /* Read the entire offset table: (total_frames + 2) * 4 bytes (uint32 on disk!) */
    unsigned int offsets_count = file_header.total_frames + 2;
    unsigned int offsets_size = offsets_count * sizeof(uint32_t);
    uint32_t* offsets_table = (uint32_t*)calloc(offsets_count, sizeof(uint32_t));
    if (!offsets_table) { f->Close(); delete f; return nullptr; }
    f->Read(offsets_table, offsets_size);

    uint32_t frame0_size = 0;
    if (offsets_table[0]) {
        frame0_size = offsets_table[1] - offsets_table[0];
    } else {
        anim_flags |= WSA_FRAME_0_ON_PAGE;
    }

    /* Get total file size */
    long total_file_size = f->Size();

    /* file_buffer_size = file data minus palette, frame0, and header */
    long file_buffer_size = total_file_size - palette_adjust - (long)frame0_size - WSA_FILE_HEADER_SIZE;

    /* We always use TARGET_IN_BUFFER (indirect) mode */
    anim_flags |= WSA_TARGET_IN_BUFFER;
    long target_buffer_size = (long)file_header.pixel_width * file_header.pixel_height;

    long delta_buffer_size = (long)file_header.largest_frame_size + WSA_EXTRA_BYTES;
    long min_buffer_size = target_buffer_size + delta_buffer_size;
    long max_buffer_size = min_buffer_size + file_buffer_size;

    /* Always load entire animation into memory (resident mode) */
    long alloc_size = (long)sizeof(WSAHandle) + max_buffer_size;
    char* buffer = (char*)calloc(1, alloc_size);
    if (!buffer) { free(offsets_table); f->Close(); delete f; return nullptr; }

    WSAHandle* sys_header = (WSAHandle*)buffer;
    char* target_buffer = buffer + sizeof(WSAHandle);
    char* delta_buffer = target_buffer + target_buffer_size;

    sys_header->current_frame = file_header.total_frames; /* signals "not yet displayed" */
    sys_header->total_frames = file_header.total_frames;
    sys_header->pixel_x = file_header.pixel_x;
    sys_header->pixel_y = file_header.pixel_y;
    sys_header->pixel_width = file_header.pixel_width;
    sys_header->pixel_height = file_header.pixel_height;
    sys_header->anim_mem_size = alloc_size;
    sys_header->delta_buffer = delta_buffer;
    sys_header->largest_frame_size = (unsigned short)delta_buffer_size;

    /* file_buffer stores: offset table + frame data (minus frame0 and palette) */
    sys_header->file_buffer = delta_buffer + sys_header->largest_frame_size;

    /* Copy the offset table we already read */
    memcpy(sys_header->file_buffer, offsets_table, offsets_size);

    /* Read palette if present */
    if (anim_flags & WSA_PALETTE_PRESENT) {
        uint8_t pal_buf[768];
        f->Read(pal_buf, 768);
        if (palette) memcpy(palette, pal_buf, 768);
    }

    /* Read frame 0 compressed data into delta_buffer for decompression */
    if (frame0_size > 0) {
        char* delta_back = delta_buffer + delta_buffer_size - (long)frame0_size;
        f->Read(delta_back, (int)frame0_size);
        LCW_Uncompress(delta_back, delta_buffer, delta_buffer_size);
    }

    /* Read remaining frames (frames 1..N) into file_buffer after offset table */
    long remaining = file_buffer_size - (long)offsets_size;
    if (remaining > 0) {
        f->Read(sys_header->file_buffer + offsets_size, (int)remaining);
    }

    f->Close();
    delete f;
    free(offsets_table);

    /* Check if wrap-around frame exists */
    if (WSA_Get_Resident_Frame_Offset(sys_header->file_buffer, sys_header->total_frames + 1))
        anim_flags |= WSA_RESIDENT;
    else
        anim_flags |= WSA_LINEAR_ONLY | WSA_RESIDENT;

    sys_header->flags = (short)anim_flags;

    return buffer;
}

void Close_Animation(void* handle)
{
    if (handle) free(handle);
}

unsigned long Animate_Frame(void* handle, GraphicViewPortClass& view, int frame_number)
{
    WSAHandle* sys_header = (WSAHandle*)handle;
    if (!handle) return 0;

    int total_frames = sys_header->total_frames;
    if (frame_number >= total_frames) return 0;

    view.Lock();

    int dest_width = view.Get_Width() + view.Get_XAdd() + view.Get_Pitch();
    int x_pixel = (short)sys_header->pixel_x;
    int y_pixel = (short)sys_header->pixel_y;

    char* frame_buffer;
    BOOL direct_to_dest;

    if (sys_header->flags & WSA_TARGET_IN_BUFFER) {
        frame_buffer = (char*)handle + sizeof(WSAHandle);
        direct_to_dest = FALSE;
    } else {
        frame_buffer = (char*)view.Get_Offset();
        frame_buffer += (y_pixel * dest_width) + x_pixel;
        direct_to_dest = TRUE;
    }

    /* If this is the first display, apply frame 0 from delta_buffer */
    if (sys_header->current_frame == total_frames) {
        if (!(sys_header->flags & WSA_FRAME_0_ON_PAGE)) {
            if (direct_to_dest) {
                Apply_XOR_Delta_To_Page_Or_Viewport(frame_buffer, sys_header->delta_buffer,
                    sys_header->pixel_width, dest_width,
                    (sys_header->flags & WSA_FRAME_0_IS_DELTA) ? 0 : 1);
            } else {
                Apply_XOR_Delta(frame_buffer, sys_header->delta_buffer);
            }
        }
        sys_header->current_frame = 0;
    }

    int curr_frame = sys_header->current_frame;
    int distance = (curr_frame > frame_number) ? (curr_frame - frame_number) : (frame_number - curr_frame);

    int search_dir = 1;
    int search_frames;

    if (frame_number > curr_frame) {
        search_frames = total_frames - frame_number + curr_frame;
        if ((search_frames < distance) && !(sys_header->flags & WSA_LINEAR_ONLY)) {
            search_dir = -1;
        } else {
            search_frames = distance;
        }
    } else {
        search_frames = total_frames - curr_frame + frame_number;
        if ((search_frames >= distance) || (sys_header->flags & WSA_LINEAR_ONLY)) {
            search_dir = -1;
            search_frames = distance;
        }
    }

    if (search_dir > 0) {
        for (int i = 0; i < search_frames; i++) {
            curr_frame += search_dir;
            WSA_Apply_Delta(sys_header, curr_frame, frame_buffer, dest_width);
            if (curr_frame == total_frames) curr_frame = 0;
        }
    } else {
        for (int i = 0; i < search_frames; i++) {
            if (curr_frame == 0) curr_frame = total_frames;
            WSA_Apply_Delta(sys_header, curr_frame, frame_buffer, dest_width);
            curr_frame += search_dir;
        }
    }

    sys_header->current_frame = (unsigned short)frame_number;

    if (sys_header->flags & WSA_TARGET_IN_BUFFER) {
        Buffer_To_Page(x_pixel, y_pixel, sys_header->pixel_width,
                       sys_header->pixel_height, frame_buffer, view);
    }

    view.Unlock();
    return 0;
}

int Get_Animation_Frame_Count(void* handle) {
    if (!handle) return 0;
    WSAHandle* sys_header = (WSAHandle*)handle;
    return (short)sys_header->total_frames;
}
