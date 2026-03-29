/**
 * Tests for GLHouseColor — house color definitions and shader source.
 *
 * Tests cover:
 *   - Predefined house color values are in valid ranges
 *   - All factions have distinct hue values
 *   - Shader source string is non-empty and contains expected keywords
 *   - Threshold values are positive
 *
 * Build: g++ -std=c++17 -o test_gl_house_color test_gl_house_color.cpp \
 *        ../gl/gl_house_color.cpp
 */

#include "test_framework.h"
#include "../gl/gl_house_color.h"

#include <cstring>
#include <cmath>

/* ─── Tests ─────────────────────────────────────────────────── */

TEST(house_color_gdi_valid_range) {
    EXPECT_GE(HouseColors::GDI_GOLD.hue, 0.0f);
    EXPECT_LT(HouseColors::GDI_GOLD.hue, 1.0f);
    EXPECT_GT(HouseColors::GDI_GOLD.saturation, 0.0f);
    EXPECT_GT(HouseColors::GDI_GOLD.brightness, 0.0f);
    EXPECT_GT(HouseColors::GDI_GOLD.threshold, 0.0f);
    PASS();
}

TEST(house_color_nod_valid_range) {
    EXPECT_GE(HouseColors::NOD_RED.hue, 0.0f);
    EXPECT_LT(HouseColors::NOD_RED.hue, 1.0f);
    PASS();
}

TEST(house_color_allies_valid_range) {
    EXPECT_GE(HouseColors::ALLIES_BLUE.hue, 0.0f);
    EXPECT_LT(HouseColors::ALLIES_BLUE.hue, 1.0f);
    PASS();
}

TEST(house_color_soviet_valid_range) {
    EXPECT_GE(HouseColors::SOVIET_RED.hue, 0.0f);
    EXPECT_LT(HouseColors::SOVIET_RED.hue, 1.0f);
    PASS();
}

TEST(house_colors_distinct_hues) {
    /* GDI and NOD should have different hues */
    EXPECT_TRUE(std::fabs(HouseColors::GDI_GOLD.hue - HouseColors::NOD_RED.hue) > 0.05f);

    /* Allies and Soviet should have different hues */
    EXPECT_TRUE(std::fabs(HouseColors::ALLIES_BLUE.hue - HouseColors::SOVIET_RED.hue) > 0.05f);

    /* All MP colors should be distinct from each other */
    float mp_hues[] = {
        HouseColors::MP_ORANGE.hue,
        HouseColors::MP_TEAL.hue,
        HouseColors::MP_PURPLE.hue,
        HouseColors::MP_PINK.hue,
    };
    for (int i = 0; i < 4; i++) {
        for (int j = i + 1; j < 4; j++) {
            EXPECT_TRUE(std::fabs(mp_hues[i] - mp_hues[j]) > 0.05f);
        }
    }
    PASS();
}

TEST(house_color_shader_source_exists) {
    const char* src = GL_House_Color_Shader_Source();
    EXPECT_NOT_NULL(src);
    EXPECT_GT(strlen(src), 50u); /* Non-trivial shader */
    PASS();
}

TEST(house_color_shader_has_keywords) {
    const char* src = GL_House_Color_Shader_Source();
    EXPECT_NOT_NULL(src);

    /* Should contain key GLSL keywords */
    EXPECT_NOT_NULL(strstr(src, "vec3"));
    EXPECT_NOT_NULL(strstr(src, "vec4"));
    /* Should reference hue somewhere */
    EXPECT_NOT_NULL(strstr(src, "hue"));
    PASS();
}

TEST(house_color_threshold_positive) {
    EXPECT_GT(HouseColors::GDI_GOLD.threshold, 0.0f);
    EXPECT_GT(HouseColors::NOD_RED.threshold, 0.0f);
    EXPECT_GT(HouseColors::ALLIES_BLUE.threshold, 0.0f);
    EXPECT_GT(HouseColors::SOVIET_RED.threshold, 0.0f);
    EXPECT_GT(HouseColors::MP_ORANGE.threshold, 0.0f);
    EXPECT_GT(HouseColors::MP_TEAL.threshold, 0.0f);
    EXPECT_GT(HouseColors::MP_PURPLE.threshold, 0.0f);
    EXPECT_GT(HouseColors::MP_PINK.threshold, 0.0f);
    PASS();
}

int main() {
    printf("test_gl_house_color\n");
    return RUN_TESTS();
}
