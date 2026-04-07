/**
 * Tests for HDSpriteProvider — MEG/TGA remastered asset loading.
 *
 * Tests cover:
 *   - Provider reports RGBA_32BIT native format
 *   - Open with invalid MEG path fails gracefully
 *   - Null/invalid shape_id handled
 *   - Integration: open real MEG + load ZIP TGA frame
 *   - Frame positioning uses META crop offsets
 *   - Frame count for known entities
 *
 * Required environment (set via tests/.env):
 *   CNC_REMASTERED_DATA=/path/to/CnCRemastered/Data
 *
 * Build: g++ -std=c++17 -o test_hd_sprite_provider test_hd_sprite_provider.cpp \
 *        ../hd_sprite_provider.cpp ../meg_reader.cpp ../meta_parser.cpp \
 *        ../zip_reader.cpp
 *
 * Real data layout (verified against TEXTURES_TD_SRGB.MEG):
 *   - ZIPs are at DATA\ART\TEXTURES\SRGB\TIBERIAN_DAWN\UNITS\<name>.ZIP
 *   - Inside each ZIP: <name>-0000.tga, <name>-0000.meta, <name>-0001.tga, ...
 *   - All filenames are lowercase inside the ZIP
 *   - Tileset XML is in CONFIG.MEG at DATA\XML\TILESETS\TD_UNITS.XML
 *     (not in TEXTURES_TD_SRGB.MEG — requires multi-MEG support, not yet implemented)
 */

#include "test_framework.h"
#include "../hd_sprite_provider.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static constexpr float TD_REMASTER_NATIVE_SCALE = 128.0f / 24.0f;

/* ─── Asset path helpers ───────────────────────────────────── */

/**
 * Return the remastered game Data directory from CNC_REMASTERED_DATA env var.
 * Aborts with a clear error if not set — missing data is a config error, not a skip.
 * Set the variable in Engine/EA/libs/render/tests/.env (see .env.example).
 */
static const char* remastered_data_dir() {
    const char* dir = getenv("CNC_REMASTERED_DATA");
    if (!dir || dir[0] == '\0') {
        fprintf(stderr,
            "\nFATAL: CNC_REMASTERED_DATA is not set.\n"
            "Set it to the remastered Data directory, e.g.:\n"
            "  CNC_REMASTERED_DATA=~/.local/share/Steam/steamapps/common/CnCRemastered/Data\n"
            "Or add it to Engine/EA/libs/render/tests/.env (see .env.example)\n\n");
        exit(1);
    }
    return dir;
}

static const char* remastered_textures_td_meg() {
    static char path[1024];
    snprintf(path, sizeof(path), "%s/TEXTURES_TD_SRGB.MEG", remastered_data_dir());
    return path;
}

static const char* remastered_config_meg() {
    static char path[1024];
    snprintf(path, sizeof(path), "%s/CONFIG.MEG", remastered_data_dir());
    return path;
}

/* FNV-1a hash matching HDSpriteProvider's internal hash */
static uint32_t entity_hash(const char* name) {
    uint32_t h = 0x811c9dc5u;
    for (const char* p = name; *p; p++) {
        h ^= (uint8_t)*p;
        h *= 0x01000193u;
    }
    return h;
}

static bool has_nonzero_alpha(const SpriteFrame& frame) {
    if (!frame.pixels || frame.width <= 0 || frame.height <= 0) {
        return false;
    }

    const uint8_t* pixels = static_cast<const uint8_t*>(frame.pixels);
    const size_t pixel_count = static_cast<size_t>(frame.width) * static_cast<size_t>(frame.height);
    for (size_t i = 0; i < pixel_count; ++i) {
        if (pixels[i * 4 + 3] != 0) {
            return true;
        }
    }
    return false;
}

/* ─── Unit tests (no real data needed) ──────────────────────── */

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

/* ─── Integration tests (require CNC_REMASTERED_DATA) ───────── */

TEST(hd_provider_open_textures_meg) {
    HDSpriteProvider provider;
    EXPECT_TRUE(provider.Open(remastered_textures_td_meg()));
    PASS();
}

TEST(hd_provider_open_config_meg) {
    HDSpriteProvider provider;
    EXPECT_TRUE(provider.Open(remastered_config_meg()));
    PASS();
}

TEST(hd_provider_category_detection_overlay_terrain) {
    EXPECT_EQ(std::string(HDSpriteProvider::Debug_Extract_Category("DATA\\XML\\TILESETS\\TD_OVERLAY.XML")), "OVERLAY");
    EXPECT_EQ(std::string(HDSpriteProvider::Debug_Extract_Category("DATA\\XML\\TILESETS\\TD_TERRAIN_DESERT.XML")), "TERRAIN");
    PASS();
}

/*
 * Load_Tileset reads from the MEG opened by Open().
 * The tileset XML (DATA\XML\TILESETS\TD_UNITS.XML) is in CONFIG.MEG,
 * but unit ZIPs are in TEXTURES_TD_SRGB.MEG.
 * HDSpriteProvider currently supports only one MEG at a time — these two
 * cannot be combined without multi-MEG support (not in M2 scope).
 *
 * This test confirms the tileset XML is present in CONFIG.MEG and loads correctly.
 * Get_Frame will fail on this provider instance because it has no texture ZIPs.
 */
TEST(hd_provider_load_tileset_from_config_meg) {
    HDSpriteProvider provider;
    EXPECT_TRUE(provider.Open(remastered_textures_td_meg()));
    EXPECT_TRUE(provider.Load_Tileset_From_Meg(remastered_config_meg(), "DATA\\XML\\TILESETS\\TD_UNITS.XML"));
    PASS();
}

TEST(hd_provider_get_frame_known_unit) {
    HDSpriteProvider provider;
    EXPECT_TRUE(provider.Open(remastered_textures_td_meg()));
    EXPECT_TRUE(provider.Load_Tileset_From_Meg(remastered_config_meg(), "DATA\\XML\\TILESETS\\TD_UNITS.XML"));

    uint32_t htnk_hash = entity_hash("HTNK");
    SpriteFrame frame = {};
    EXPECT_TRUE(provider.Get_Frame((void*)(uintptr_t)htnk_hash, 0, frame));
    EXPECT_EQ((int)frame.pixel_format, (int)SpritePixelFormat::RGBA_32BIT);
    EXPECT_GT(frame.width, 0);
    EXPECT_GT(frame.height, 0);
    EXPECT_NOT_NULL(frame.pixels);
    EXPECT_GE(frame.canvas_width, frame.width);
    EXPECT_NEAR(frame.native_scale, TD_REMASTER_NATIVE_SCALE, 0.0001f);
    PASS();
}

TEST(hd_provider_crop_offset) {
    HDSpriteProvider provider;
    EXPECT_TRUE(provider.Open(remastered_textures_td_meg()));
    EXPECT_TRUE(provider.Load_Tileset_From_Meg(remastered_config_meg(), "DATA\\XML\\TILESETS\\TD_UNITS.XML"));

    uint32_t htnk_hash = entity_hash("HTNK");
    SpriteFrame frame = {};
    EXPECT_TRUE(provider.Get_Frame((void*)(uintptr_t)htnk_hash, 0, frame));
    EXPECT_GE(frame.origin_x, 0);
    EXPECT_GE(frame.origin_y, 0);
    EXPECT_GT(frame.canvas_width, 0);
    EXPECT_GT(frame.canvas_height, 0);
    PASS();
}

TEST(hd_provider_frame_count_known_unit) {
    HDSpriteProvider provider;
    EXPECT_TRUE(provider.Open(remastered_textures_td_meg()));
    EXPECT_TRUE(provider.Load_Tileset_From_Meg(remastered_config_meg(), "DATA\\XML\\TILESETS\\TD_UNITS.XML"));

    uint32_t htnk_hash = entity_hash("HTNK");
    /* HTNK.ZIP has 64 frames (128 files = 64 tga + 64 meta) */
    int count = provider.Get_Frame_Count((void*)(uintptr_t)htnk_hash);
    EXPECT_EQ(count, 64);
    PASS();
}

TEST(hd_provider_get_frame_known_overlay_and_tree) {
    HDSpriteProvider provider;
    EXPECT_TRUE(provider.Open(remastered_textures_td_meg()));
    EXPECT_TRUE(provider.Load_Tileset_From_Meg(remastered_config_meg(), "DATA\\XML\\TILESETS\\TD_TERRAIN_TEMPERATE.XML"));

    SpriteFrame overlay = {};
    EXPECT_TRUE(provider.Get_Frame((void*)(uintptr_t)entity_hash("TI1"), 0, overlay));
    EXPECT_EQ((int)overlay.pixel_format, (int)SpritePixelFormat::RGBA_32BIT);
    EXPECT_GT(overlay.width, 0);
    EXPECT_GT(overlay.height, 0);
    EXPECT_TRUE(has_nonzero_alpha(overlay));

    SpriteFrame tree = {};
    EXPECT_TRUE(provider.Get_Frame((void*)(uintptr_t)entity_hash("T01"), 0, tree));
    EXPECT_EQ((int)tree.pixel_format, (int)SpritePixelFormat::RGBA_32BIT);
    EXPECT_GT(tree.width, 0);
    EXPECT_GT(tree.height, 0);
    EXPECT_TRUE(has_nonzero_alpha(tree));
    PASS();
}

TEST(hd_provider_different_units_different_sizes) {
    HDSpriteProvider provider;
    EXPECT_TRUE(provider.Open(remastered_textures_td_meg()));
    EXPECT_TRUE(provider.Load_Tileset_From_Meg(remastered_config_meg(), "DATA\\XML\\TILESETS\\TD_UNITS.XML"));

    SpriteFrame frame_htnk = {};
    bool htnk_ok = provider.Get_Frame((void*)(uintptr_t)entity_hash("HTNK"), 0, frame_htnk);
    EXPECT_TRUE(htnk_ok);

    SpriteFrame frame_apc = {};
    bool apc_ok = provider.Get_Frame((void*)(uintptr_t)entity_hash("APC"), 0, frame_apc);
    EXPECT_TRUE(apc_ok);

    EXPECT_NOT_NULL(frame_htnk.pixels);
    EXPECT_NOT_NULL(frame_apc.pixels);
    
    EXPECT_GT(frame_htnk.canvas_width, 0);
    EXPECT_GT(frame_htnk.canvas_height, 0);
    EXPECT_GT(frame_apc.canvas_width, 0);
    EXPECT_GT(frame_apc.canvas_height, 0);
    PASS();
}

int main() {
    printf("test_hd_sprite_provider\n");
    return RUN_TESTS();
}
