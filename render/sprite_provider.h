/**
 * ISpriteProvider - Abstract interface for sprite frame access.
 *
 * Decouples game rendering code (CC_Draw_Shape) from the underlying
 * asset format. Implementations exist for legacy SHP/MIX and HD MEG/TGA.
 */

#ifndef RENDER_SPRITE_PROVIDER_H
#define RENDER_SPRITE_PROVIDER_H

#include <cstdint>

/**
 * Describes pixel format of a sprite frame returned by a provider.
 */
enum class SpritePixelFormat {
    INDEXED_8BIT,  // 8-bit palette-indexed (legacy SHP)
    RGBA_32BIT,    // 32-bit RGBA (HD TGA from MEG)
};

/**
 * A single sprite frame as returned by a provider.
 * The provider owns the pixel data — caller must not free it.
 */
struct SpriteFrame {
    const void* pixels;         // Pixel data (format depends on pixel_format)
    int         width;          // Frame width in pixels
    int         height;         // Frame height in pixels
    int         pitch;          // Bytes per row (may include padding)
    int         origin_x;       // Anchor X offset (from META crop or SHP header)
    int         origin_y;       // Anchor Y offset (from META crop or SHP header)
    int         canvas_width;   // Original canvas width (for positioning)
    int         canvas_height;  // Original canvas height (for positioning)
    float       native_scale;   // Sprite pixels per legacy sprite pixel (1.0 for legacy, remaster-dependent for HD)
    SpritePixelFormat pixel_format;
};

/**
 * Abstract sprite provider interface.
 *
 * Game code calls Get_Frame() with a shape identifier and frame number.
 * The provider returns pixel data in whatever format the underlying
 * asset source uses. The renderer handles both formats.
 */
class ISpriteProvider {
public:
    virtual ~ISpriteProvider() = default;

    /**
     * Retrieve a sprite frame.
     *
     * @param shape_id   Opaque shape identifier (SHP pointer for legacy,
     *                   entity name hash for HD)
     * @param frame      Frame number within the shape
     * @param frame_out  Filled with frame data on success
     * @return true if frame was found and loaded
     */
    virtual bool Get_Frame(const void* shape_id, int frame, SpriteFrame& frame_out) = 0;

    /**
     * @return Total frame count for the given shape, or 0 if unknown/not found.
     */
    virtual int Get_Frame_Count(const void* shape_id) = 0;

    /**
     * @return The native pixel format this provider produces.
     */
    virtual SpritePixelFormat Native_Format() const = 0;
};

#endif // RENDER_SPRITE_PROVIDER_H
