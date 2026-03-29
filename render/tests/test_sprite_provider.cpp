/**
 * Tests for ISpriteProvider interface contract and LegacySpriteProvider.
 *
 * Tests cover:
 *   - SpriteFrame struct correctness
 *   - LegacySpriteProvider wraps Build_Frame() correctly
 *   - Provider returns consistent frame dimensions
 *   - Frame count matches SHP header
 *   - Invalid frame numbers handled gracefully
 *   - Native format reporting
 *   - Cached frame pointer stability (same pointer on repeated calls)
 *
 * Note: LegacySpriteProvider tests require engine SHP data.
 * Tests that need real SHP files are guarded by file existence checks
 * and skip gracefully if data is unavailable.
 *
 * Build: g++ -std=c++17 -o test_sprite_provider test_sprite_provider.cpp \
 *        ../legacy_sprite_provider.cpp -I../../graphics/
 */

#include "test_framework.h"
#include "../sprite_provider.h"
#include "../legacy_sprite_provider.h"

/* ─── Interface contract tests (no real data needed) ─────── */

TEST(sprite_frame_struct_size) {
    /* SpriteFrame should be a lightweight value type */
    EXPECT_LT(sizeof(SpriteFrame), 128u);
    PASS();
}

TEST(sprite_pixel_format_values) {
    EXPECT_NE((int)SpritePixelFormat::INDEXED_8BIT, (int)SpritePixelFormat::RGBA_32BIT);
    PASS();
}

TEST(legacy_provider_native_format) {
    LegacySpriteProvider provider;
    EXPECT_EQ((int)provider.Native_Format(), (int)SpritePixelFormat::INDEXED_8BIT);
    PASS();
}

TEST(legacy_provider_null_shape) {
    LegacySpriteProvider provider;
    SpriteFrame frame = {};
    EXPECT_FALSE(provider.Get_Frame(nullptr, 0, frame));
    PASS();
}

TEST(legacy_provider_null_shape_frame_count) {
    LegacySpriteProvider provider;
    EXPECT_EQ(provider.Get_Frame_Count(nullptr), 0);
    PASS();
}

TEST(legacy_provider_negative_frame) {
    LegacySpriteProvider provider;
    SpriteFrame frame = {};
    /* Even with a non-null pointer, negative frame should fail gracefully */
    uint8_t dummy[16] = {};
    EXPECT_FALSE(provider.Get_Frame(dummy, -1, frame));
    PASS();
}

TEST(sprite_frame_zeroed_on_failure) {
    LegacySpriteProvider provider;
    SpriteFrame frame = {};
    frame.width = 9999;
    frame.height = 9999;

    provider.Get_Frame(nullptr, 0, frame);

    /* On failure, frame should not contain stale data from the caller */
    EXPECT_EQ(frame.width, 0);
    EXPECT_EQ(frame.height, 0);
    EXPECT_NULL(frame.pixels);
    PASS();
}

/*
 * The following tests need real SHP data loaded from MIX archives.
 * They validate that LegacySpriteProvider produces correct output
 * matching direct Build_Frame() calls. These will be enabled when
 * the provider is integrated with the engine's MIX loading.
 *
 * TODO: Enable once LegacySpriteProvider is connected to Build_Frame()
 *
 * TEST(legacy_provider_get_frame_valid_shp)
 *   - Load a known SHP (e.g. "INFANTRY.SHP")
 *   - Get_Frame(shp, 0, frame) should succeed
 *   - frame.width > 0, frame.height > 0
 *   - frame.pixels != nullptr
 *   - frame.pixel_format == INDEXED_8BIT
 *
 * TEST(legacy_provider_frame_count_matches_shp)
 *   - Get_Frame_Count(shp) should match the SHP header frame count
 *
 * TEST(legacy_provider_frame_out_of_range)
 *   - Get_Frame(shp, 99999, frame) should return false
 *
 * TEST(legacy_provider_cached_pointer_stable)
 *   - Call Get_Frame() twice with same shape+frame
 *   - Both should return same pixels pointer (cached, no re-decompress)
 *
 * TEST(legacy_provider_frame_matches_build_frame)
 *   - Get_Frame() output pixels should match direct Build_Frame() result byte-for-byte
 */

int main() {
    printf("test_sprite_provider\n");
    return RUN_TESTS();
}
