/**
 * HDSpriteProvider - ISpriteProvider for remastered MEG/DDS assets.
 *
 * Reads DDS frames + META crop data from MEG archives, with TGA fallback.
 * Returns 32-bit RGBA pixel data.
 */

#ifndef RENDER_HD_SPRITE_PROVIDER_H
#define RENDER_HD_SPRITE_PROVIDER_H

#include "sprite_provider.h"

struct HDSpriteProvider_Impl;

class HDSpriteProvider : public ISpriteProvider {
public:
    HDSpriteProvider();
    ~HDSpriteProvider() override;

    /**
     * Open a MEG archive for reading HD assets.
     * @param meg_path  Filesystem path to the .MEG file
     * @return true if archive was opened and parsed successfully
     */
    bool Open(const char* meg_path);

    /**
     * Load a tileset XML that maps game entity names to frame filenames.
     * @param xml_path  Path to tileset XML (e.g. TD_UNITS.XML) within the MEG
     * @return true if tileset was parsed successfully
     */
    bool Load_Tileset(const char* xml_path);

    /**
     * Load a tileset XML from a different MEG archive (e.g. CONFIG.MEG).
     */
    bool Load_Tileset_From_Meg(const char* meg_path, const char* xml_path);

    /**
     * Set active theater for terrain tiles (e.g. "DESERT", "TEMPERATE").
     * This scans the MEG for terrain tiles matching the theater and caches them.
     */
    void Set_Theater(const char* theater);

    bool Get_Frame(const void* shape_id, int frame, SpriteFrame& frame_out) override;
    int  Get_Frame_Count(const void* shape_id) override;
    SpritePixelFormat Native_Format() const override { return SpritePixelFormat::RGBA_32BIT; }

private:
    HDSpriteProvider_Impl* impl_;
};

#endif // RENDER_HD_SPRITE_PROVIDER_H
