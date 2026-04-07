#pragma once
#include "sprite_provider.h"

struct HDTerrainProvider_Impl;

class HDTerrainProvider {
public:
    HDTerrainProvider();
    ~HDTerrainProvider();

    bool Open(const char* meg_path);
    void Set_Theater(const char* theater);

    /**
     * Look up and decode a terrain tile.
     * WARNING: The returned SpriteFrame.pixels pointer is owned by the provider.
     * It is valid until the LRU evicts this tile; upload to GPU before calling
     * Get_Tile again.
     */
    bool Get_Tile(const char* name, int frame, SpriteFrame& out);

    /**
     * Same as Get_Tile but keyed by pre-computed FNV-1a hash of the tile name.
     * Use when the name string is unavailable (e.g., recorded as a hash in StampCmd).
     */
    bool Get_Tile_By_Hash(uint32_t name_hash, int frame, SpriteFrame& out);

private:
    HDTerrainProvider_Impl* impl_;
};
