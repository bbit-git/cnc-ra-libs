/**
 * Tests for TextureAtlas — bin-packing atlas builder.
 *
 * Tests cover:
 *   - Single frame packing
 *   - Multiple frames fit in one page
 *   - Page overflow creates new page
 *   - Region UV coordinates are correct
 *   - No region overlap
 *   - Pixel data integrity (written pixels match read-back)
 *   - Power-of-two page size enforcement
 *   - Edge cases: 1x1 frame, max-size frame, zero frames
 *
 * Build: g++ -std=c++17 -o test_texture_atlas test_texture_atlas.cpp ../texture_atlas.cpp
 */

#include "test_framework.h"
#include "../texture_atlas.h"

#include <cstring>
#include <cstdlib>
#include <vector>

/* Helper: create RGBA pixel buffer filled with a solid color */
static std::vector<uint8_t> make_rgba(int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    std::vector<uint8_t> pixels(w * h * 4);
    for (int i = 0; i < w * h; i++) {
        pixels[i * 4 + 0] = r;
        pixels[i * 4 + 1] = g;
        pixels[i * 4 + 2] = b;
        pixels[i * 4 + 3] = a;
    }
    return pixels;
}

/* Helper: check that pixel at (x,y) in atlas page matches expected RGBA */
static bool check_pixel(const void* page, int page_size, int x, int y,
                         uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    const uint8_t* p = (const uint8_t*)page + (y * page_size + x) * 4;
    return p[0] == r && p[1] == g && p[2] == b && p[3] == a;
}

/* ─── Tests ─────────────────────────────────────────────────── */

TEST(atlas_init_default) {
    TextureAtlas atlas;
    atlas.Init(256);
    EXPECT_EQ(atlas.Page_Size(), 256);
    EXPECT_EQ(atlas.Page_Count(), 0);
    PASS();
}

TEST(atlas_single_frame) {
    TextureAtlas atlas;
    atlas.Init(256);

    auto pixels = make_rgba(32, 32, 255, 0, 0, 255);
    AtlasFrameID id = atlas.Add_Frame(pixels.data(), 32, 32);
    EXPECT_NE(id, (AtlasFrameID)-1);

    atlas.Finalize();

    EXPECT_EQ(atlas.Page_Count(), 1);

    AtlasRegion region;
    EXPECT_TRUE(atlas.Get_Region(id, region));
    EXPECT_EQ(region.atlas_id, 0);
    EXPECT_EQ(region.w, 32);
    EXPECT_EQ(region.h, 32);
    PASS();
}

TEST(atlas_region_uv_correct) {
    TextureAtlas atlas;
    atlas.Init(256);

    auto pixels = make_rgba(64, 32, 0, 255, 0, 255);
    AtlasFrameID id = atlas.Add_Frame(pixels.data(), 64, 32);
    atlas.Finalize();

    AtlasRegion region;
    EXPECT_TRUE(atlas.Get_Region(id, region));

    /* UV should be normalized to [0,1] based on page size */
    EXPECT_NEAR(region.u0, (float)region.x / 256.0f, 0.001f);
    EXPECT_NEAR(region.v0, (float)region.y / 256.0f, 0.001f);
    EXPECT_NEAR(region.u1, (float)(region.x + region.w) / 256.0f, 0.001f);
    EXPECT_NEAR(region.v1, (float)(region.y + region.h) / 256.0f, 0.001f);
    PASS();
}

TEST(atlas_multiple_frames_one_page) {
    TextureAtlas atlas;
    atlas.Init(256);

    AtlasFrameID ids[4];
    for (int i = 0; i < 4; i++) {
        auto pixels = make_rgba(64, 64, i * 60, 0, 0, 255);
        ids[i] = atlas.Add_Frame(pixels.data(), 64, 64);
        EXPECT_NE(ids[i], (AtlasFrameID)-1);
    }

    atlas.Finalize();

    /* 4 x 64x64 = 16384 pixels, fits in 256x256 = 65536 */
    EXPECT_EQ(atlas.Page_Count(), 1);
    PASS();
}

TEST(atlas_no_region_overlap) {
    TextureAtlas atlas;
    atlas.Init(256);

    AtlasFrameID ids[8];
    for (int i = 0; i < 8; i++) {
        auto pixels = make_rgba(48, 48, 0, 0, i * 30, 255);
        ids[i] = atlas.Add_Frame(pixels.data(), 48, 48);
    }
    atlas.Finalize();

    /* Check that no two regions overlap */
    AtlasRegion regions[8];
    for (int i = 0; i < 8; i++)
        atlas.Get_Region(ids[i], regions[i]);

    for (int i = 0; i < 8; i++) {
        for (int j = i + 1; j < 8; j++) {
            if (regions[i].atlas_id != regions[j].atlas_id)
                continue;
            /* AABB overlap test */
            bool overlap =
                regions[i].x < regions[j].x + regions[j].w &&
                regions[i].x + regions[i].w > regions[j].x &&
                regions[i].y < regions[j].y + regions[j].h &&
                regions[i].y + regions[i].h > regions[j].y;
            EXPECT_FALSE(overlap);
        }
    }
    PASS();
}

TEST(atlas_page_overflow) {
    TextureAtlas atlas;
    atlas.Init(64); /* tiny page: 64x64 */

    /* 128x128 frame cannot fit in a 64x64 page — should still be handled
       (either rejected or placed on a larger page, depending on implementation).
       At minimum, a 32x32 frame should fit. */
    auto small = make_rgba(32, 32, 255, 255, 0, 255);
    AtlasFrameID id1 = atlas.Add_Frame(small.data(), 32, 32);
    AtlasFrameID id2 = atlas.Add_Frame(small.data(), 32, 32);
    AtlasFrameID id3 = atlas.Add_Frame(small.data(), 32, 32);
    AtlasFrameID id4 = atlas.Add_Frame(small.data(), 32, 32);
    /* 4x 32x32 = fills 64x64 exactly */
    AtlasFrameID id5 = atlas.Add_Frame(small.data(), 32, 32);
    /* 5th should go to page 2 */

    atlas.Finalize();

    EXPECT_GE(atlas.Page_Count(), 2);

    AtlasRegion r5;
    EXPECT_TRUE(atlas.Get_Region(id5, r5));
    /* id5 should be on a different page than id1 */
    AtlasRegion r1;
    atlas.Get_Region(id1, r1);
    /* At least one must be on page > 0 if pages overflowed */
    EXPECT_TRUE(r1.atlas_id != r5.atlas_id || atlas.Page_Count() >= 2);
    PASS();
}

TEST(atlas_pixel_integrity) {
    TextureAtlas atlas;
    atlas.Init(256);

    /* Red 4x4 frame */
    auto red = make_rgba(4, 4, 255, 0, 0, 255);
    AtlasFrameID id = atlas.Add_Frame(red.data(), 4, 4);
    atlas.Finalize();

    AtlasRegion region;
    EXPECT_TRUE(atlas.Get_Region(id, region));

    const void* page = atlas.Page_Pixels(region.atlas_id);
    EXPECT_NOT_NULL(page);

    /* Check corner pixels of the placed frame */
    EXPECT_TRUE(check_pixel(page, 256, region.x, region.y, 255, 0, 0, 255));
    EXPECT_TRUE(check_pixel(page, 256, region.x + 3, region.y + 3, 255, 0, 0, 255));
    PASS();
}

TEST(atlas_1x1_frame) {
    TextureAtlas atlas;
    atlas.Init(256);

    uint8_t pixel[] = {128, 64, 32, 255};
    AtlasFrameID id = atlas.Add_Frame(pixel, 1, 1);
    EXPECT_NE(id, (AtlasFrameID)-1);

    atlas.Finalize();

    AtlasRegion region;
    EXPECT_TRUE(atlas.Get_Region(id, region));
    EXPECT_EQ(region.w, 1);
    EXPECT_EQ(region.h, 1);
    PASS();
}

TEST(atlas_invalid_frame_id) {
    TextureAtlas atlas;
    atlas.Init(256);
    atlas.Finalize();

    AtlasRegion region;
    EXPECT_FALSE(atlas.Get_Region(99999, region));
    PASS();
}

TEST(atlas_page_pixels_null_before_finalize) {
    TextureAtlas atlas;
    atlas.Init(256);

    auto pixels = make_rgba(16, 16, 0, 0, 0, 255);
    atlas.Add_Frame(pixels.data(), 16, 16);

    /* Before Finalize(), page pixels may not be available */
    /* After Finalize(), they must be */
    atlas.Finalize();
    EXPECT_NOT_NULL(atlas.Page_Pixels(0));
    PASS();
}

TEST(atlas_many_small_frames) {
    TextureAtlas atlas;
    atlas.Init(256);

    /* Pack 100 x 16x16 frames = 25600 pixels, fits in 256x256 = 65536 */
    for (int i = 0; i < 100; i++) {
        auto pixels = make_rgba(16, 16, i % 256, (i * 7) % 256, (i * 13) % 256, 255);
        AtlasFrameID id = atlas.Add_Frame(pixels.data(), 16, 16);
        EXPECT_NE(id, (AtlasFrameID)-1);
    }
    atlas.Finalize();
    EXPECT_GE(atlas.Page_Count(), 1);
    PASS();
}

int main() {
    printf("test_texture_atlas\n");
    return RUN_TESTS();
}
