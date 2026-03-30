/**
 * GLSpriteBatch - Batched quad renderer for GPU sprite drawing.
 *
 * Collects sprite draw calls within a frame and renders them as
 * a single batched draw call per atlas texture page per layer.
 */

#ifndef RENDER_GL_SPRITE_BATCH_H
#define RENDER_GL_SPRITE_BATCH_H

#include "../texture_atlas.h"
#include <cstdint>

struct SpriteBatchEntry {
    AtlasRegion region;     // Source region in atlas
    float       dst_x;     // Destination X (screen/world pixels)
    float       dst_y;     // Destination Y
    float       scale_x;   // Horizontal scale (1.0 = normal)
    float       scale_y;   // Vertical scale
    float       house_hue; // House color hue (-1.0 = no remap)
    uint8_t     flags;     // Flip H/V plus ghost/shadow effect bits
    uint8_t     fade;      // Fade level (0 = none, 255 = full)
};

class GLSpriteBatch {
public:
    GLSpriteBatch();
    ~GLSpriteBatch();

    /**
     * Begin a new batch for a frame.
     */
    void Begin();

    /**
     * Add a sprite to the batch.
     */
    void Add(const SpriteBatchEntry& entry);

    /**
     * Associate an atlas page ID with a GL texture handle.
     */
    void Set_Page_Texture(uint16_t atlas_id, uint32_t texture_id);

    /**
     * Remove all atlas page texture bindings.
     */
    void Clear_Page_Textures();

    /**
     * Flush all queued sprites to the GPU.
     * Sorts by atlas page for minimal texture switches.
     */
    void Flush();

    /**
     * @return Number of draw calls issued in last Flush().
     */
    int Draw_Call_Count() const;

    /**
     * @return Number of sprites rendered in last Flush().
     */
    int Sprite_Count() const;

private:
    struct Impl;
    Impl* impl_;
};

#endif // RENDER_GL_SPRITE_BATCH_H
