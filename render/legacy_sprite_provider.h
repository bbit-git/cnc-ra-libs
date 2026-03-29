/**
 * LegacySpriteProvider - ISpriteProvider for classic SHP/MIX assets.
 *
 * Wraps the existing Build_Frame() / BigShapeBuffer pipeline from
 * engine/libs/graphics/. Returns 8-bit indexed pixel data.
 */

#ifndef RENDER_LEGACY_SPRITE_PROVIDER_H
#define RENDER_LEGACY_SPRITE_PROVIDER_H

#include "sprite_provider.h"

class LegacySpriteProvider : public ISpriteProvider {
public:
    LegacySpriteProvider();
    ~LegacySpriteProvider() override;

    bool Get_Frame(const void* shape_id, int frame, SpriteFrame& frame_out) override;
    int  Get_Frame_Count(const void* shape_id) override;
    SpritePixelFormat Native_Format() const override { return SpritePixelFormat::INDEXED_8BIT; }
};

#endif // RENDER_LEGACY_SPRITE_PROVIDER_H
