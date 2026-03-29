/**
 * LegacySpriteProvider - ISpriteProvider for classic SHP/MIX KeyFrame assets.
 *
 * Wraps the existing Build_Frame() / Get_Shape_Header_Data() pipeline.
 * Returns 8-bit palette-indexed pixel data. The pixel buffer is owned
 * by the engine's BigShapeBuffer cache (for KeyFrame) or a local
 * decompression buffer (for SHP).
 *
 * Callers must consume the frame data before the next Get_Frame() call,
 * as decompression buffers may be reused.
 */

#include "legacy_sprite_provider.h"
#include <cstdint>
#include <cstring>
#include <cstdlib>

// Engine functions we link against (from engine/*/modern.src/)
extern unsigned long Build_Frame(void const* dataptr, unsigned short framenumber, void* buffptr);
extern unsigned short Get_Build_Frame_Count(void const* dataptr);
extern unsigned short Get_Build_Frame_Width(void const* dataptr);
extern unsigned short Get_Build_Frame_Height(void const* dataptr);
extern void* Get_Shape_Header_Data(void* ptr);
extern void* Extract_Shape(void const* buffer, int shape);
extern int Extract_Shape_Count(void const* buffer);
extern int Get_Shape_Width(void const* shape);
extern int Get_Shape_Height(void const* shape);
extern unsigned long LCW_Uncompress(void const* source, void* dest,
                                     unsigned long source_len, unsigned long dest_max);

// Engine globals (BOOL is typedef int in td_platform.h / ra_platform.h)
extern "C" {
    extern long _ShapeBufferSize;
    extern char* _ShapeBuffer;
    extern char* BigShapeBufferStart;
    extern int  UseBigShapeBuffer;
}

// SHP frame header (must match shp.cpp Shape_Type)
#pragma pack(push, 1)
struct SHPFrameHeader {
    uint16_t shape_type;
    uint8_t  height;
    uint16_t width;
    uint8_t  original_height;
    uint16_t shape_size;
    uint16_t data_length;
};
#pragma pack(pop)

/// Detect whether a shape data block is SHP format (vs KeyFrame).
/// SHP: [uint16 NumShapes][uint32 offsets...] — Offsets[0] ≈ (NumShapes+1)*4
static bool Is_SHP_Format(const void* data)
{
    const uint8_t* raw = static_cast<const uint8_t*>(data);
    uint16_t nshp = *reinterpret_cast<const uint16_t*>(raw);
    uint32_t off0 = *reinterpret_cast<const uint32_t*>(raw + 2);
    uint32_t expected = static_cast<uint32_t>(nshp + 1) * 4;
    return (nshp > 0 && nshp <= 4096 && off0 >= expected && off0 <= expected + 32);
}

// Local decompression buffer for SHP frames (reused across calls)
static uint8_t* shp_decomp_buf = nullptr;
static int shp_decomp_buf_size = 0;

static uint8_t* Ensure_Decomp_Buffer(int size)
{
    if (size > shp_decomp_buf_size) {
        free(shp_decomp_buf);
        shp_decomp_buf = static_cast<uint8_t*>(malloc(size));
        shp_decomp_buf_size = shp_decomp_buf ? size : 0;
    }
    return shp_decomp_buf;
}

LegacySpriteProvider::LegacySpriteProvider() {}

LegacySpriteProvider::~LegacySpriteProvider()
{
    // We don't own BigShapeBuffer or _ShapeBuffer
}

bool LegacySpriteProvider::Get_Frame(const void* shape_id, int frame, SpriteFrame& frame_out)
{
    memset(&frame_out, 0, sizeof(frame_out));
    if (!shape_id || frame < 0) return false;

    frame_out.pixel_format = SpritePixelFormat::INDEXED_8BIT;

    if (Is_SHP_Format(shape_id)) {
        // SHP format: Extract_Shape → parse header → LCW decompress
        void* raw_frame = Extract_Shape(shape_id, frame);
        if (!raw_frame) return false;

        const SHPFrameHeader* hdr = static_cast<const SHPFrameHeader*>(raw_frame);
        int w = hdr->width;
        int h = hdr->height;
        if (w <= 0 || h <= 0 || w > 1024 || h > 1024) return false;

        int hdr_size = (hdr->shape_type & 1) ? 10 + 16 : 10;
        const uint8_t* comp_data = static_cast<const uint8_t*>(raw_frame) + hdr_size;
        int comp_size = hdr->shape_size - hdr_size;
        if (comp_size <= 0) return false;

        int uncomp_size = w * h;
        uint8_t* buf = Ensure_Decomp_Buffer(uncomp_size);
        if (!buf) return false;

        memset(buf, 0, uncomp_size);
        LCW_Uncompress(comp_data, buf, comp_size, uncomp_size);

        frame_out.pixels        = buf;
        frame_out.width         = w;
        frame_out.height        = h;
        frame_out.pitch         = w;
        frame_out.origin_x      = 0;
        frame_out.origin_y      = 0;
        frame_out.canvas_width  = w;
        frame_out.canvas_height = h;
        return true;
    } else {
        // KeyFrame format: Build_Frame → Get_Shape_Header_Data
        unsigned long result = Build_Frame(shape_id, static_cast<unsigned short>(frame),
                                           _ShapeBuffer);
        if (!result) return false;

        void* pixels = Get_Shape_Header_Data(reinterpret_cast<void*>(result));
        if (!pixels) return false;

        int w = Get_Build_Frame_Width(shape_id);
        int h = Get_Build_Frame_Height(shape_id);
        if (w <= 0 || h <= 0) return false;

        frame_out.pixels        = pixels;
        frame_out.width         = w;
        frame_out.height        = h;
        frame_out.pitch         = w;
        frame_out.origin_x      = 0;
        frame_out.origin_y      = 0;
        frame_out.canvas_width  = w;
        frame_out.canvas_height = h;
        return true;
    }
}

int LegacySpriteProvider::Get_Frame_Count(const void* shape_id)
{
    if (!shape_id) return 0;

    if (Is_SHP_Format(shape_id)) {
        return Extract_Shape_Count(shape_id);
    } else {
        return Get_Build_Frame_Count(shape_id);
    }
}
