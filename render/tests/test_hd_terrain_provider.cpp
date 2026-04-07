#include "test_framework.h"
#include "../hd_terrain_provider.h"
#include "../../../cnc/render_bridge/terrain_replay_math.h"
#include <cstdio>
#include <cstdlib>
#include <ctype.h>

static constexpr float TD_REMASTER_NATIVE_SCALE = 128.0f / 24.0f;

// Mirror of the provider's internal hash so we can test Get_Tile_By_Hash.
static uint32_t fnv1a_hash(const char* str) {
    uint32_t hash = 0x811C9DC5;
    while (*str) {
        hash ^= (unsigned char)toupper((unsigned char)*str++);
        hash *= 0x01000193;
    }
    return hash;
}

static const char* remastered_data_dir() {
    const char* dir = getenv("CNC_REMASTERED_DATA");
    if (!dir || dir[0] == '\0') {
        fprintf(stderr, "\nERROR: CNC_REMASTERED_DATA environment variable not set.\n");
        fprintf(stderr, "Please set it to the path of your C&C Remastered 'Data' directory.\n\n");
        exit(1);
    }
    return dir;
}

static const char* remastered_textures_td_meg() {
    static char path[1024];
    snprintf(path, sizeof(path), "%s/TEXTURES_TD_SRGB.MEG", remastered_data_dir());
    return path;
}

TEST(hd_provider_terrain_replay_math_matches_legacy_footprint) {
    StampCmd cmd = {};
    cmd.x = 100;
    cmd.y = 200;

    AtlasRegion region = {};
    region.w = 48;
    region.h = 48;
    region.origin_x = -4;
    region.origin_y = 6;
    region.native_scale = 2.0f;

    TerrainReplayQuad quad = Render_Bridge_Compute_HD_Terrain_Quad(
        cmd, region,
        10, 20, 240, 240,
        1.0f, 0.0f, 0.0f);

    EXPECT_NEAR(quad.screen_x, 108.0f, 0.001f);
    EXPECT_NEAR(quad.screen_y, 223.0f, 0.001f);
    EXPECT_NEAR(quad.screen_w, 24.0f, 0.001f);
    EXPECT_NEAR(quad.screen_h, 24.0f, 0.001f);
    EXPECT_TRUE(quad.visible);
    PASS();
}

TEST(hd_provider_terrain_desert_b1) {
    HDTerrainProvider provider;
    EXPECT_TRUE(provider.Open(remastered_textures_td_meg()));
    provider.Set_Theater("DESERT");

    SpriteFrame frame = {};
    EXPECT_TRUE(provider.Get_Tile("B1", 0, frame));
    EXPECT_EQ((int)frame.pixel_format, (int)SpritePixelFormat::RGBA_32BIT);
    EXPECT_EQ(frame.width, 128); // Spec correction 128x128
    EXPECT_EQ(frame.height, 128);
    EXPECT_EQ(frame.pitch, 512); // 128 * 4
    EXPECT_NOT_NULL(frame.pixels);
    EXPECT_EQ(frame.origin_x, 0);
    EXPECT_EQ(frame.origin_y, 0);
    EXPECT_NEAR(frame.native_scale, TD_REMASTER_NATIVE_SCALE, 0.0001f);
    
    // Test cache hit path
    SpriteFrame frame2 = {};
    EXPECT_TRUE(provider.Get_Tile("B1", 0, frame2));
    EXPECT_EQ(frame2.pitch, 512);
    EXPECT_NOT_NULL(frame2.pixels);
    
    PASS();
}

TEST(hd_provider_terrain_theater_switching) {
    HDTerrainProvider provider;
    EXPECT_TRUE(provider.Open(remastered_textures_td_meg()));
    
    provider.Set_Theater("DESERT");
    SpriteFrame frame = {};
    EXPECT_TRUE(provider.Get_Tile("B1", 0, frame));

    provider.Set_Theater("TEMPERATE");
    EXPECT_TRUE(provider.Get_Tile("B1", 0, frame)); // B1 exists in temperate too
    PASS();
}

TEST(hd_provider_terrain_winter) {
    HDTerrainProvider provider;
    EXPECT_TRUE(provider.Open(remastered_textures_td_meg()));
    provider.Set_Theater("WINTER");

    SpriteFrame frame = {};
    EXPECT_TRUE(provider.Get_Tile("B1", 0, frame));
    EXPECT_NOT_NULL(frame.pixels);
    PASS();
}

TEST(hd_provider_terrain_multiframe) {
    HDTerrainProvider provider;
    EXPECT_TRUE(provider.Open(remastered_textures_td_meg()));
    provider.Set_Theater("DESERT");

    SpriteFrame frame = {};
    EXPECT_TRUE(provider.Get_Tile("BIB1", 7, frame));
    EXPECT_FALSE(provider.Get_Tile("BIB1", 8, frame));
    PASS();
}

TEST(hd_provider_terrain_uninitialized_and_negative) {
    HDTerrainProvider provider;
    
    SpriteFrame frame = {};
    EXPECT_FALSE(provider.Get_Tile("B1", 0, frame)); // Not open

    EXPECT_TRUE(provider.Open(remastered_textures_td_meg()));
    EXPECT_FALSE(provider.Get_Tile("B1", 0, frame)); // Opened but theater not set
    
    provider.Set_Theater("DESERT");
    EXPECT_FALSE(provider.Get_Tile("B1", -1, frame)); // Negative frame
    PASS();
}

TEST(hd_provider_terrain_get_tile_by_hash) {
    HDTerrainProvider provider;
    EXPECT_TRUE(provider.Open(remastered_textures_td_meg()));
    provider.Set_Theater("DESERT");

    uint32_t b1_hash = fnv1a_hash("B1");

    // Cold lookup — triggers decode via Get_Tile delegation.
    SpriteFrame frame = {};
    EXPECT_TRUE(provider.Get_Tile_By_Hash(b1_hash, 0, frame));
    EXPECT_EQ(frame.width, 128);
    EXPECT_EQ(frame.height, 128);
    EXPECT_NOT_NULL(frame.pixels);

    // Warm lookup — cache hit path inside Get_Tile_By_Hash.
    SpriteFrame frame2 = {};
    EXPECT_TRUE(provider.Get_Tile_By_Hash(b1_hash, 0, frame2));
    EXPECT_EQ(frame2.width, 128);
    EXPECT_NOT_NULL(frame2.pixels);

    // Hash zero must return false (sentinel for "no hash").
    SpriteFrame frame3 = {};
    EXPECT_FALSE(provider.Get_Tile_By_Hash(0, 0, frame3));

    // Unknown hash must return false.
    EXPECT_FALSE(provider.Get_Tile_By_Hash(0xDEADBEEF, 0, frame3));

    PASS();
}

TEST(hd_provider_terrain_canvas_dimensions) {
    HDTerrainProvider provider;
    EXPECT_TRUE(provider.Open(remastered_textures_td_meg()));
    provider.Set_Theater("DESERT");

    SpriteFrame frame = {};
    EXPECT_TRUE(provider.Get_Tile("B1", 0, frame));

    // canvas_width/canvas_height must be set (from META or defaulted to frame size).
    // The rendering path relies on these for correct tile sizing.
    EXPECT_GT(frame.canvas_width, 0);
    EXPECT_GT(frame.canvas_height, 0);

    // Canvas dimensions must not exceed the raw texture dimensions.
    EXPECT_TRUE(frame.canvas_width <= frame.width);
    EXPECT_TRUE(frame.canvas_height <= frame.height);

    PASS();
}

int main() {
    printf("test_hd_terrain_provider\n");
    return RUN_TESTS();
}
