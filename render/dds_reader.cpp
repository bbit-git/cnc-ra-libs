/**
 * dds_reader.cpp — DDS texture decoder implementation.
 *
 * Supports BC1 (DXT1), BC2 (DXT3), BC3 (DXT5), and uncompressed
 * RGBA/BGRA/RGB formats. Decodes mip level 0 to RGBA8888.
 */

#include "dds_reader.h"
#include <cstdlib>
#include <cstring>

// DDS magic number: 'DDS ' (0x20534444)
static const uint32_t DDS_MAGIC = 0x20534444;

// DDS_PIXELFORMAT flags
static const uint32_t DDPF_FOURCC       = 0x4;
static const uint32_t DDPF_RGB          = 0x40;
static const uint32_t DDPF_ALPHAPIXELS  = 0x1;

// FourCC codes
static uint32_t make_fourcc(char a, char b, char c, char d) {
    return static_cast<uint32_t>(a)
         | (static_cast<uint32_t>(b) << 8)
         | (static_cast<uint32_t>(c) << 16)
         | (static_cast<uint32_t>(d) << 24);
}

struct DDS_RawHeader {
    uint32_t magic;
    uint32_t size;            // 124
    uint32_t flags;
    uint32_t height;
    uint32_t width;
    uint32_t pitch_or_linear_size;
    uint32_t depth;
    uint32_t mip_count;
    uint32_t reserved1[11];
    // DDS_PIXELFORMAT
    uint32_t pf_size;         // 32
    uint32_t pf_flags;
    uint32_t pf_fourCC;
    uint32_t pf_rgb_bit_count;
    uint32_t pf_r_mask;
    uint32_t pf_g_mask;
    uint32_t pf_b_mask;
    uint32_t pf_a_mask;
    // caps
    uint32_t caps1;
    uint32_t caps2;
    uint32_t reserved2[3];
};

static_assert(sizeof(DDS_RawHeader) == 128, "DDS header must be 128 bytes");

bool DDS_Parse_Header(const void* data, size_t data_size, DDSInfo& info)
{
    if (!data || data_size < 128) return false;

    const auto* hdr = static_cast<const DDS_RawHeader*>(data);
    if (hdr->magic != DDS_MAGIC) return false;
    if (hdr->size != 124) return false;

    info.width     = static_cast<int>(hdr->width);
    info.height    = static_cast<int>(hdr->height);
    info.mip_count = hdr->mip_count > 0 ? static_cast<int>(hdr->mip_count) : 1;
    info.format    = DDS_FORMAT_UNKNOWN;

    if (hdr->pf_flags & DDPF_FOURCC) {
        uint32_t cc = hdr->pf_fourCC;
        // DX10 extended header adds 20 bytes before pixel data; not supported.
        if (cc == make_fourcc('D','X','1','0')) return false;
        if (cc == make_fourcc('D','X','T','1'))      info.format = DDS_FORMAT_DXT1;
        else if (cc == make_fourcc('D','X','T','3'))  info.format = DDS_FORMAT_DXT3;
        else if (cc == make_fourcc('D','X','T','5'))  info.format = DDS_FORMAT_DXT5;
    } else if (hdr->pf_flags & DDPF_RGB) {
        uint32_t bpp = hdr->pf_rgb_bit_count;
        if (bpp == 32 && hdr->pf_r_mask == 0x000000FF && hdr->pf_b_mask == 0x00FF0000) {
            info.format = DDS_FORMAT_RGBA8;
        } else if (bpp == 32 && hdr->pf_r_mask == 0x00FF0000 && hdr->pf_b_mask == 0x000000FF) {
            info.format = DDS_FORMAT_BGRA8;
        } else if (bpp == 24 && hdr->pf_r_mask == 0x00FF0000) {
            info.format = DDS_FORMAT_RGB8;
        }
    }

    return info.format != DDS_FORMAT_UNKNOWN;
}

// --- BC1/BC3 decode helpers ---

static void unpack_565(uint16_t c, uint8_t out[4])
{
    out[0] = static_cast<uint8_t>(((c >> 11) & 0x1F) * 255 / 31);
    out[1] = static_cast<uint8_t>(((c >> 5)  & 0x3F) * 255 / 63);
    out[2] = static_cast<uint8_t>((c & 0x1F) * 255 / 31);
    out[3] = 255;
}

static void decode_bc1_block(const uint8_t* block, uint8_t* out, int stride)
{
    uint16_t c0 = block[0] | (block[1] << 8);
    uint16_t c1 = block[2] | (block[3] << 8);
    uint8_t colors[4][4];
    unpack_565(c0, colors[0]);
    unpack_565(c1, colors[1]);

    if (c0 > c1) {
        for (int i = 0; i < 3; i++) {
            colors[2][i] = static_cast<uint8_t>((2 * colors[0][i] + colors[1][i]) / 3);
            colors[3][i] = static_cast<uint8_t>((colors[0][i] + 2 * colors[1][i]) / 3);
        }
        colors[2][3] = 255;
        colors[3][3] = 255;
    } else {
        for (int i = 0; i < 3; i++) {
            colors[2][i] = static_cast<uint8_t>((colors[0][i] + colors[1][i]) / 2);
        }
        colors[2][3] = 255;
        colors[3][0] = 0; colors[3][1] = 0; colors[3][2] = 0; colors[3][3] = 0;
    }

    uint32_t indices = block[4] | (block[5] << 8) | (block[6] << 16) | (block[7] << 24);
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            int idx = (indices >> ((y * 4 + x) * 2)) & 3;
            memcpy(out + y * stride + x * 4, colors[idx], 4);
        }
    }
}

static void decode_bc2_block(const uint8_t* block, uint8_t* out, int stride)
{
    // Bytes 0-7: explicit alpha (4 bits per pixel, 16 pixels)
    // Bytes 8-15: BC1 color block
    decode_bc1_block(block + 8, out, stride);

    // Apply explicit alpha: each byte holds two 4-bit alpha values
    for (int y = 0; y < 4; y++) {
        uint16_t row = block[y * 2] | (block[y * 2 + 1] << 8);
        for (int x = 0; x < 4; x++) {
            uint8_t a4 = (row >> (x * 4)) & 0xF;
            out[y * stride + x * 4 + 3] = static_cast<uint8_t>(a4 * 255 / 15);
        }
    }
}

static void decode_bc3_block(const uint8_t* block, uint8_t* out, int stride)
{
    // Bytes 0-7: interpolated alpha block
    uint8_t a0 = block[0], a1 = block[1];
    uint8_t alphas[8];
    alphas[0] = a0;
    alphas[1] = a1;
    if (a0 > a1) {
        alphas[2] = static_cast<uint8_t>((6 * a0 + 1 * a1) / 7);
        alphas[3] = static_cast<uint8_t>((5 * a0 + 2 * a1) / 7);
        alphas[4] = static_cast<uint8_t>((4 * a0 + 3 * a1) / 7);
        alphas[5] = static_cast<uint8_t>((3 * a0 + 4 * a1) / 7);
        alphas[6] = static_cast<uint8_t>((2 * a0 + 5 * a1) / 7);
        alphas[7] = static_cast<uint8_t>((1 * a0 + 6 * a1) / 7);
    } else {
        alphas[2] = static_cast<uint8_t>((4 * a0 + 1 * a1) / 5);
        alphas[3] = static_cast<uint8_t>((3 * a0 + 2 * a1) / 5);
        alphas[4] = static_cast<uint8_t>((2 * a0 + 3 * a1) / 5);
        alphas[5] = static_cast<uint8_t>((1 * a0 + 4 * a1) / 5);
        alphas[6] = 0;
        alphas[7] = 255;
    }

    // 48-bit alpha index table (3 bits per pixel)
    uint64_t alpha_bits = 0;
    for (int i = 2; i < 8; i++) {
        alpha_bits |= static_cast<uint64_t>(block[i]) << ((i - 2) * 8);
    }

    // Bytes 8-15: BC1 color block
    decode_bc1_block(block + 8, out, stride);

    // Overwrite alpha from interpolated alpha block
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            int ai = static_cast<int>((alpha_bits >> ((y * 4 + x) * 3)) & 7);
            out[y * stride + x * 4 + 3] = alphas[ai];
        }
    }
}

// Generic block decoder loop for BC-compressed formats.
typedef void (*BlockDecodeFn)(const uint8_t* block, uint8_t* out, int stride);

static void decode_all_blocks(const uint8_t* src, uint8_t* dst,
                               int width, int height, int block_bytes,
                               BlockDecodeFn decode_fn)
{
    int stride = width * 4;
    int bw = (width + 3) / 4;
    int bh = (height + 3) / 4;

    // For images not a multiple of 4, decode into a temp 4x4 then copy valid pixels
    for (int by = 0; by < bh; by++) {
        for (int bx = 0; bx < bw; bx++) {
            int px = bx * 4;
            int py = by * 4;

            // If the block fits entirely, decode directly
            if (px + 4 <= width && py + 4 <= height) {
                decode_fn(src, dst + py * stride + px * 4, stride);
            } else {
                // Partial block at image edge
                uint8_t tmp[4 * 4 * 4]; // 4x4 RGBA
                decode_fn(src, tmp, 16);
                int cw = (px + 4 <= width)  ? 4 : width - px;
                int ch = (py + 4 <= height) ? 4 : height - py;
                for (int row = 0; row < ch; row++) {
                    memcpy(dst + (py + row) * stride + px * 4,
                           tmp + row * 16, cw * 4);
                }
            }
            src += block_bytes;
        }
    }
}

uint8_t* DDS_Decode_RGBA(const void* data, size_t data_size,
                          int& width_out, int& height_out)
{
    DDSInfo info;
    if (!DDS_Parse_Header(data, data_size, info)) return nullptr;

    int w = info.width;
    int h = info.height;
    if (w <= 0 || h <= 0 || w > 16384 || h > 16384) return nullptr;

    const uint8_t* src = static_cast<const uint8_t*>(data) + 128;
    size_t remaining = data_size - 128;

    uint8_t* pixels = static_cast<uint8_t*>(malloc(w * h * 4));
    if (!pixels) return nullptr;

    switch (info.format) {
        case DDS_FORMAT_DXT1: {
            size_t needed = static_cast<size_t>(((w + 3) / 4)) * ((h + 3) / 4) * 8;
            if (remaining < needed) { free(pixels); return nullptr; }
            decode_all_blocks(src, pixels, w, h, 8, decode_bc1_block);
            break;
        }
        case DDS_FORMAT_DXT3: {
            size_t needed = static_cast<size_t>(((w + 3) / 4)) * ((h + 3) / 4) * 16;
            if (remaining < needed) { free(pixels); return nullptr; }
            decode_all_blocks(src, pixels, w, h, 16, decode_bc2_block);
            break;
        }
        case DDS_FORMAT_DXT5: {
            size_t needed = static_cast<size_t>(((w + 3) / 4)) * ((h + 3) / 4) * 16;
            if (remaining < needed) { free(pixels); return nullptr; }
            decode_all_blocks(src, pixels, w, h, 16, decode_bc3_block);
            break;
        }
        case DDS_FORMAT_RGBA8: {
            size_t needed = static_cast<size_t>(w) * h * 4;
            if (remaining < needed) { free(pixels); return nullptr; }
            memcpy(pixels, src, needed);
            break;
        }
        case DDS_FORMAT_BGRA8: {
            size_t needed = static_cast<size_t>(w) * h * 4;
            if (remaining < needed) { free(pixels); return nullptr; }
            memcpy(pixels, src, needed);
            // Swap R and B channels
            for (int i = 0; i < w * h; i++) {
                uint8_t tmp = pixels[i * 4];
                pixels[i * 4]     = pixels[i * 4 + 2];
                pixels[i * 4 + 2] = tmp;
            }
            break;
        }
        case DDS_FORMAT_RGB8: {
            size_t needed = static_cast<size_t>(w) * h * 3;
            if (remaining < needed) { free(pixels); return nullptr; }
            // Expand RGB to RGBA
            for (int i = 0; i < w * h; i++) {
                pixels[i * 4 + 0] = src[i * 3 + 2]; // B→R (RGB8 with r_mask=0x00FF0000 is BGR)
                pixels[i * 4 + 1] = src[i * 3 + 1]; // G
                pixels[i * 4 + 2] = src[i * 3 + 0]; // R→B
                pixels[i * 4 + 3] = 255;
            }
            break;
        }
        default:
            free(pixels);
            return nullptr;
    }

    width_out = w;
    height_out = h;
    return pixels;
}
