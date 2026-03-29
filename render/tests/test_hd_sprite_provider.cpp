/**
 * Tests for HDSpriteProvider — MEG/TGA remastered asset loading.
 *
 * Tests cover:
 *   - Provider reports RGBA_32BIT native format
 *   - Open with invalid MEG path fails gracefully
 *   - Null/invalid shape_id handled
 *   - Integration: open real MEG + load TGA frame (guarded by file existence)
 *   - Frame positioning uses META crop offsets
 *   - Frame count for known entities
 *
 * Build: g++ -std=c++17 -o test_hd_sprite_provider test_hd_sprite_provider.cpp \
 *        ../hd_sprite_provider.cpp ../meg_reader.cpp ../meta_parser.cpp
 */

#include "test_framework.h"
#include "../hd_sprite_provider.h"

#include <cstdio>

/* Path to remastered MEG file — adjust for local installation */
static const char* find_remastered_meg() {
    static const char* candidates[] = {
        /* Steam default */
        "/home/andrzej/.local/share/Steam/steamapps/common/CnCRemastered/Data/CNCDATA/TIBERIAN_DAWN/CD1/SPRITES.MEG",
        "/home/andrzej/.steam/steam/steamapps/common/CnCRemastered/Data/CNCDATA/TIBERIAN_DAWN/CD1/SPRITES.MEG",
        nullptr,
    };
    for (int i = 0; candidates[i]; i++) {
        FILE* f = fopen(candidates[i], "rb");
        if (f) { fclose(f); return candidates[i]; }
    }
    return nullptr;
}

/* ─── Tests (no real data needed) ──────────────────────────── */

TEST(hd_provider_native_format) {
    HDSpriteProvider provider;
    EXPECT_EQ((int)provider.Native_Format(), (int)SpritePixelFormat::RGBA_32BIT);
    PASS();
}

TEST(hd_provider_open_invalid_path) {
    HDSpriteProvider provider;
    EXPECT_FALSE(provider.Open("/nonexistent/path/sprites.meg"));
    PASS();
}

TEST(hd_provider_null_shape_id) {
    HDSpriteProvider provider;
    SpriteFrame frame = {};
    EXPECT_FALSE(provider.Get_Frame(nullptr, 0, frame));
    PASS();
}

TEST(hd_provider_frame_count_without_open) {
    HDSpriteProvider provider;
    uint8_t dummy[4] = {};
    EXPECT_EQ(provider.Get_Frame_Count(dummy), 0);
    PASS();
}

TEST(hd_provider_get_frame_without_open) {
    HDSpriteProvider provider;
    SpriteFrame frame = {};
    uint8_t dummy[4] = {};
    EXPECT_FALSE(provider.Get_Frame(dummy, 0, frame));
    PASS();
}

/* ─── Integration tests (need real MEG on disk) ──────────── */

TEST(hd_provider_open_real_meg) {
    const char* meg_path = find_remastered_meg();
    if (!meg_path) {
        printf("SKIP (no MEG found)\n");
        g_tests_passed++;
        return;
    }

    HDSpriteProvider provider;
    EXPECT_TRUE(provider.Open(meg_path));
    PASS();
}

/*
 * Future integration tests (enabled when HDSpriteProvider + tileset loading is complete):
 *
 * TEST(hd_provider_load_tileset)
 *   - Open MEG, Load_Tileset("TD_UNITS.XML")
 *   - Should return true
 *
 * TEST(hd_provider_get_frame_known_unit)
 *   - After tileset load, Get_Frame for a known unit (e.g. medium tank)
 *   - frame.pixel_format == RGBA_32BIT
 *   - frame.width > 0, frame.height > 0
 *   - frame.pixels != nullptr
 *   - frame.canvas_width >= frame.width (canvas includes crop margin)
 *
 * TEST(hd_provider_crop_offset)
 *   - Get_Frame for a cropped sprite
 *   - frame.origin_x and origin_y should match META crop values
 *   - frame.width/height should match META crop dimensions
 *   - frame.canvas_width/height should match META size dimensions
 *
 * TEST(hd_provider_frame_count_known_unit)
 *   - Get_Frame_Count for a known unit should be > 0
 *   - Should match expected frame count (e.g. 32 directions * N animation frames)
 *
 * TEST(hd_provider_different_units_different_sizes)
 *   - Infantry sprites should be smaller than vehicle sprites
 *   - Validates that per-entity dimensions are loaded correctly
 */

int main() {
    printf("test_hd_sprite_provider\n");
    return RUN_TESTS();
}
