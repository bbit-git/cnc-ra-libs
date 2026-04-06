#include "test_framework.h"
#include "../hd_terrain_provider.h"
#include <cstdio>
#include <cstdlib>

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

int main() {
    printf("test_hd_terrain_provider\n");
    return RUN_TESTS();
}
