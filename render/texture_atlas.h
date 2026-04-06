/**
 * TextureAtlas - Bin-packs sprite frames into GPU-friendly texture pages.
 *
 * Accepts both 8-bit indexed (legacy SHP, palette-converted to RGBA)
 * and 32-bit RGBA (HD TGA) frames. Packs them into power-of-two atlas
 * textures for efficient GPU batched rendering.
 */

#ifndef RENDER_TEXTURE_ATLAS_H
#define RENDER_TEXTURE_ATLAS_H

#include <cstdint>

/**
 * UV rectangle within an atlas page.
 */
struct AtlasRegion {
    uint16_t atlas_id;   // Which atlas texture page
    uint16_t x, y;       // Pixel position within atlas
    uint16_t w, h;       // Pixel dimensions
    int16_t  origin_x;   // Crop offset from the full canvas
    int16_t  origin_y;   // Crop offset from the full canvas
    uint16_t canvas_w;   // Full canvas width before crop
    uint16_t canvas_h;   // Full canvas height before crop
    float    native_scale; // Sprite pixels per legacy sprite pixel
    float    u0, v0;     // Normalized UV top-left
    float    u1, v1;     // Normalized UV bottom-right
};

/**
 * Handle returned when a frame is added to the atlas.
 */
typedef uint32_t AtlasFrameID;

class TextureAtlas {
public:
    TextureAtlas();
    ~TextureAtlas();

    /**
     * Configure atlas page dimensions. Call before adding frames.
     * @param page_size  Width and height of each atlas page (must be power of 2)
     */
    void Init(int page_size = 2048);

    /**
     * Add an RGBA frame to the atlas.
     * @param pixels  RGBA pixel data (width * height * 4 bytes)
     * @param width   Frame width
     * @param height  Frame height
     * @return Frame ID for later lookup, or (uint32_t)-1 on failure
     */
    AtlasFrameID Add_Frame(const void* pixels, int width, int height,
                           int origin_x = 0, int origin_y = 0,
                           int canvas_w = 0, int canvas_h = 0,
                           float native_scale = 1.0f);

    /**
     * Finalize all pages — no more frames can be added after this.
     * Prepares atlas textures for GPU upload.
     */
    void Finalize();

    /**
     * Look up a frame's atlas region.
     */
    bool Get_Region(AtlasFrameID id, AtlasRegion& region_out) const;

    /**
     * @return Number of atlas pages created.
     */
    int Page_Count() const;

    /**
     * Get raw RGBA pixel data for an atlas page (for GPU upload).
     * @param page_index  Page index (0 to Page_Count()-1)
     * @return Pointer to page_size * page_size * 4 bytes of RGBA data
     */
    const void* Page_Pixels(int page_index) const;

    int Page_Size() const;

private:
    struct Impl;
    Impl* impl_;
};

#endif // RENDER_TEXTURE_ATLAS_H
