/**
 * Tests for GLSpriteBatch — batched quad renderer.
 *
 * Tests cover:
 *   - Empty batch flush (no crash, zero draw calls)
 *   - Single sprite batch
 *   - Multiple sprites batched into single draw call (same atlas page)
 *   - Multiple atlas pages = multiple draw calls
 *   - Sprite count and draw call count reporting
 *   - Begin/Flush cycle can be repeated
 *
 * Build: g++ -std=c++17 -o test_gl_sprite_batch test_gl_sprite_batch.cpp \
 *        ../gl/gl_sprite_batch.cpp ../gl/gl_renderer.cpp -lSDL3 -lGLESv2
 */

#include "test_framework.h"
#include "../gl/gl_sprite_batch.h"

/* ─── Mocks for engine symbols ─────────────────────────────── */
#include <SDL3/SDL.h>
SDL_Window* g_window = nullptr;

/* ─── Offline tests (no GL context) ─────────────────────────── */

TEST(batch_construct_destroy) {
    GLSpriteBatch batch;
    PASS();
}

TEST(batch_begin_flush_empty) {
    GLSpriteBatch batch;
    batch.Begin();
    batch.Flush();
    EXPECT_EQ(batch.Draw_Call_Count(), 0);
    EXPECT_EQ(batch.Sprite_Count(), 0);
    PASS();
}

TEST(batch_add_single) {
    GLSpriteBatch batch;
    batch.Begin();

    SpriteBatchEntry entry = {};
    entry.region.atlas_id = 0;
    entry.region.x = 0; entry.region.y = 0;
    entry.region.w = 32; entry.region.h = 32;
    entry.region.u0 = 0; entry.region.v0 = 0;
    entry.region.u1 = 0.125f; entry.region.v1 = 0.125f;
    entry.dst_x = 100; entry.dst_y = 200;
    entry.scale_x = 1.0f; entry.scale_y = 1.0f;
    entry.house_hue = -1.0f;
    entry.flags = 0;
    entry.fade = 0;

    batch.Add(entry);
    batch.Flush();

    EXPECT_EQ(batch.Sprite_Count(), 1);
    /* Single atlas page = single draw call */
    EXPECT_EQ(batch.Draw_Call_Count(), 1);
    PASS();
}

TEST(batch_multiple_same_atlas) {
    GLSpriteBatch batch;
    batch.Begin();

    for (int i = 0; i < 50; i++) {
        SpriteBatchEntry entry = {};
        entry.region.atlas_id = 0; /* all same page */
        entry.region.w = 16; entry.region.h = 16;
        entry.dst_x = (float)(i * 20);
        entry.dst_y = 0;
        entry.scale_x = 1.0f; entry.scale_y = 1.0f;
        entry.house_hue = -1.0f;
        batch.Add(entry);
    }

    batch.Flush();
    EXPECT_EQ(batch.Sprite_Count(), 50);
    /* Same atlas page — should be 1 draw call */
    EXPECT_EQ(batch.Draw_Call_Count(), 1);
    PASS();
}

TEST(batch_multiple_atlas_pages) {
    GLSpriteBatch batch;
    batch.Begin();

    /* 3 sprites on 3 different atlas pages */
    for (int i = 0; i < 3; i++) {
        SpriteBatchEntry entry = {};
        entry.region.atlas_id = (uint16_t)i;
        entry.region.w = 32; entry.region.h = 32;
        entry.dst_x = (float)(i * 40);
        entry.dst_y = 0;
        entry.scale_x = 1.0f; entry.scale_y = 1.0f;
        entry.house_hue = -1.0f;
        batch.Add(entry);
    }

    batch.Flush();
    EXPECT_EQ(batch.Sprite_Count(), 3);
    /* 3 atlas pages = 3 draw calls */
    EXPECT_EQ(batch.Draw_Call_Count(), 3);
    PASS();
}

TEST(batch_large_single_atlas_splits_draw_calls) {
    GLSpriteBatch batch;
    batch.Begin();

    for (int i = 0; i < 4097; i++) {
        SpriteBatchEntry entry = {};
        entry.region.atlas_id = 0;
        entry.region.w = 8; entry.region.h = 8;
        entry.dst_x = (float)(i % 256);
        entry.dst_y = (float)(i / 256);
        entry.scale_x = 1.0f; entry.scale_y = 1.0f;
        entry.house_hue = -1.0f;
        batch.Add(entry);
    }

    batch.Flush();
    EXPECT_EQ(batch.Sprite_Count(), 4097);
    EXPECT_EQ(batch.Draw_Call_Count(), 2);
    PASS();
}

TEST(batch_repeated_begin_flush) {
    GLSpriteBatch batch;

    /* First cycle */
    batch.Begin();
    SpriteBatchEntry entry = {};
    entry.region.atlas_id = 0;
    entry.region.w = 16; entry.region.h = 16;
    entry.scale_x = 1.0f; entry.scale_y = 1.0f;
    entry.house_hue = -1.0f;
    batch.Add(entry);
    batch.Flush();
    EXPECT_EQ(batch.Sprite_Count(), 1);

    /* Second cycle — counts should reset */
    batch.Begin();
    batch.Flush();
    EXPECT_EQ(batch.Sprite_Count(), 0);
    EXPECT_EQ(batch.Draw_Call_Count(), 0);
    PASS();
}

TEST(batch_entry_struct_flags) {
    SpriteBatchEntry entry = {};
    entry.flags = 0x03; /* HORZ_REV | VERT_REV */
    entry.fade = 128;
    entry.house_hue = 0.6f;

    EXPECT_EQ(entry.flags, 0x03);
    EXPECT_EQ(entry.fade, 128);
    EXPECT_NEAR(entry.house_hue, 0.6f, 0.001f);
    PASS();
}

int main() {
    printf("test_gl_sprite_batch\n");
    return RUN_TESTS();
}
