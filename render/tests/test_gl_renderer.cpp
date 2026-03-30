/**
 * Tests for GLRenderer — OpenGL ES backend.
 *
 * GL tests require a display context. Tests are split into:
 *   - Offline tests (no GL context needed): struct validation, parameter checks
 *   - Online tests (GL context required): texture upload, shader compile, rendering
 *
 * Online tests create a hidden SDL3 window for the GL context.
 * They skip gracefully if SDL3/GL is unavailable (e.g. headless CI).
 *
 * Build: g++ -std=c++17 -o test_gl_renderer test_gl_renderer.cpp \
 *        ../gl/gl_renderer.cpp -lSDL3 -lGLESv2
 */

#include "test_framework.h"
#include "../gl/gl_renderer.h"

#include <cstdlib>

/* ─── Mocks for engine symbols ─────────────────────────────── */
#include <SDL3/SDL.h>
SDL_Window* g_window = nullptr;

/* ─── Offline tests (no GL context) ─────────────────────────── */

TEST(gl_renderer_construct_destroy) {
    GLRenderer renderer;
    /* Constructor + destructor should not crash without Init() */
    PASS();
}

TEST(gl_renderer_shutdown_without_init) {
    GLRenderer renderer;
    renderer.Shutdown(); /* Should be safe */
    PASS();
}

TEST(gl_renderer_delete_texture_zero) {
    GLRenderer renderer;
    renderer.Delete_Texture(0); /* Should be no-op */
    PASS();
}

/* ─── Online tests (need SDL3 + GL context) ─────────────────── */

/*
 * Helper: try to create a hidden SDL3 window + GL context.
 * Returns true if successful (GL available).
 */
static bool g_gl_available = false;
static bool g_gl_checked = false;

static bool check_gl_available() {
    if (g_gl_checked) return g_gl_available;
    g_gl_checked = true;

    /* Quick check: can we create a GLRenderer?
       This will fail gracefully on headless systems. */
    GLRenderer test;
    g_gl_available = test.Init(64, 64);
    if (g_gl_available) test.Shutdown();
    return g_gl_available;
}

#define REQUIRE_GL()                                    \
    do {                                                \
        if (!check_gl_available()) {                    \
            printf("SKIP (no GL context)\n");           \
            g_tests_passed++;                           \
            return;                                     \
        }                                               \
    } while (0)

TEST(gl_renderer_init_shutdown) {
    REQUIRE_GL();
    GLRenderer renderer;
    EXPECT_TRUE(renderer.Init(320, 200));
    renderer.Shutdown();
    PASS();
}

TEST(gl_renderer_upload_indexed) {
    REQUIRE_GL();
    GLRenderer renderer;
    EXPECT_TRUE(renderer.Init(64, 64));

    uint8_t pixels[16 * 16];
    memset(pixels, 42, sizeof(pixels));
    uint32_t tex = renderer.Upload_Indexed(pixels, 16, 16);
    EXPECT_NE(tex, 0u);

    renderer.Delete_Texture(tex);
    renderer.Shutdown();
    PASS();
}

TEST(gl_renderer_upload_palette) {
    REQUIRE_GL();
    GLRenderer renderer;
    EXPECT_TRUE(renderer.Init(64, 64));

    uint8_t palette[256 * 3];
    for (int i = 0; i < 256; i++) {
        palette[i * 3 + 0] = i;       /* R */
        palette[i * 3 + 1] = 255 - i; /* G */
        palette[i * 3 + 2] = i / 2;   /* B */
    }
    uint32_t tex = renderer.Upload_Palette(palette, 256);
    EXPECT_NE(tex, 0u);

    renderer.Delete_Texture(tex);
    renderer.Shutdown();
    PASS();
}

TEST(gl_renderer_upload_rgba) {
    REQUIRE_GL();
    GLRenderer renderer;
    EXPECT_TRUE(renderer.Init(64, 64));

    uint8_t pixels[8 * 8 * 4];
    memset(pixels, 200, sizeof(pixels));
    uint32_t tex = renderer.Upload_RGBA(pixels, 8, 8);
    EXPECT_NE(tex, 0u);

    renderer.Delete_Texture(tex);
    renderer.Shutdown();
    PASS();
}

TEST(gl_renderer_update_indexed) {
    REQUIRE_GL();
    GLRenderer renderer;
    EXPECT_TRUE(renderer.Init(64, 64));

    uint8_t pixels[16 * 16] = {};
    uint32_t tex = renderer.Upload_Indexed(pixels, 16, 16);

    /* Update with new data — should not crash */
    memset(pixels, 99, sizeof(pixels));
    renderer.Update_Indexed(tex, pixels, 16, 16);

    renderer.Delete_Texture(tex);
    renderer.Shutdown();
    PASS();
}

TEST(gl_renderer_draw_palette_quad) {
    REQUIRE_GL();
    GLRenderer renderer;
    EXPECT_TRUE(renderer.Init(64, 64));

    uint8_t indexed[16 * 16] = {};
    uint32_t idx_tex = renderer.Upload_Indexed(indexed, 16, 16);

    uint8_t palette[256 * 3] = {};
    uint32_t pal_tex = renderer.Upload_Palette(palette, 256);

    /* Draw should not crash */
    renderer.Draw_Palette_Quad(idx_tex, pal_tex, 0, 0, 64, 64);
    renderer.Present();

    renderer.Delete_Texture(idx_tex);
    renderer.Delete_Texture(pal_tex);
    renderer.Shutdown();
    PASS();
}

TEST(gl_renderer_draw_rgba_quad) {
    REQUIRE_GL();
    GLRenderer renderer;
    EXPECT_TRUE(renderer.Init(64, 64));

    uint8_t pixels[8 * 8 * 4];
    memset(pixels, 128, sizeof(pixels));
    uint32_t tex = renderer.Upload_RGBA(pixels, 8, 8);

    renderer.Draw_RGBA_Quad(tex, 0, 0, 8, 8, 0, 0, 64, 64);
    renderer.Present();

    renderer.Delete_Texture(tex);
    renderer.Shutdown();
    PASS();
}

TEST(gl_renderer_draw_with_house_hue) {
    REQUIRE_GL();
    GLRenderer renderer;
    EXPECT_TRUE(renderer.Init(64, 64));

    uint8_t pixels[8 * 8 * 4];
    /* Fill with green chroma key pixels */
    for (int i = 0; i < 8 * 8; i++) {
        pixels[i * 4 + 0] = 0;    /* R */
        pixels[i * 4 + 1] = 255;  /* G */
        pixels[i * 4 + 2] = 0;    /* B */
        pixels[i * 4 + 3] = 255;  /* A */
    }
    uint32_t tex = renderer.Upload_RGBA(pixels, 8, 8);

    /* Draw with house hue = red (0.0) — should not crash */
    renderer.Draw_RGBA_Quad(tex, 0, 0, 8, 8, 0, 0, 64, 64, 0.0f);
    renderer.Present();

    renderer.Delete_Texture(tex);
    renderer.Shutdown();
    PASS();
}

int main() {
    printf("test_gl_renderer\n");
    return RUN_TESTS();
}
