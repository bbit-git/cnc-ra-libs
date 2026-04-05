/**
 * dds_reader.h — DDS (DirectDraw Surface) texture decoder.
 *
 * Decodes DXT1 (BC1), DXT3 (BC2), DXT5 (BC3), and uncompressed
 * RGBA/BGRA/RGB DDS textures to RGBA8888 pixel buffers.
 * Mip level 0 only. No external dependencies.
 */

#ifndef EA_DDS_READER_H
#define EA_DDS_READER_H

#include <cstdint>
#include <cstddef>

enum DDSFormat {
    DDS_FORMAT_UNKNOWN = 0,
    DDS_FORMAT_DXT1,       // BC1 — RGB (1-bit alpha)
    DDS_FORMAT_DXT3,       // BC2 — RGBA (explicit alpha)
    DDS_FORMAT_DXT5,       // BC3 — RGBA (interpolated alpha)
    DDS_FORMAT_RGBA8,      // Uncompressed 32-bit RGBA
    DDS_FORMAT_RGB8,       // Uncompressed 24-bit RGB
    DDS_FORMAT_BGRA8,      // Uncompressed 32-bit BGRA
};

struct DDSInfo {
    int        width;
    int        height;
    int        mip_count;
    DDSFormat  format;
};

/// Parse DDS header and return format info.
/// Returns false if not a valid DDS file.
bool DDS_Parse_Header(const void* data, size_t data_size, DDSInfo& info);

/// Decode DDS texture to RGBA8888 pixels.
/// Returns malloc'd buffer (caller must free), or nullptr on failure.
/// Only decodes mip level 0 (full resolution).
uint8_t* DDS_Decode_RGBA(const void* data, size_t data_size,
                          int& width_out, int& height_out);

#endif // EA_DDS_READER_H
