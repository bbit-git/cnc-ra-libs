/**
 * GLHouseColor - Shader-based house color remapping.
 *
 * Legacy SHP: palette index remapping via lookup table (existing system).
 * HD TGA: green chroma key detection + hue shift (this module).
 *
 * The remastered HD sprites use bright green pixels to mark house-colored
 * regions. This shader detects green chroma key and shifts the hue to
 * match the player's faction color, preserving anti-aliased edges.
 */

#ifndef RENDER_GL_HOUSE_COLOR_H
#define RENDER_GL_HOUSE_COLOR_H

#include <cstdint>

/**
 * House color parameters for the hue-shift shader.
 */
struct HouseColorParams {
    float hue;          // Target hue (0.0-1.0, maps to 0-360 degrees)
    float saturation;   // Saturation multiplier (1.0 = unchanged)
    float brightness;   // Brightness multiplier (1.0 = unchanged)
    float threshold;    // Green detection threshold (default 2.0)
};

/**
 * Predefined house colors matching original game factions.
 */
namespace HouseColors {
    // Tiberian Dawn
    constexpr HouseColorParams GDI_GOLD      = {0.12f, 1.0f, 1.0f, 2.0f};
    constexpr HouseColorParams NOD_RED       = {0.00f, 1.0f, 1.0f, 2.0f};

    // Red Alert
    constexpr HouseColorParams ALLIES_BLUE   = {0.60f, 1.0f, 1.0f, 2.0f};
    constexpr HouseColorParams SOVIET_RED    = {0.00f, 1.0f, 1.0f, 2.0f};

    // Multiplayer
    constexpr HouseColorParams MP_ORANGE     = {0.08f, 1.0f, 1.0f, 2.0f};
    constexpr HouseColorParams MP_TEAL       = {0.50f, 1.0f, 1.0f, 2.0f};
    constexpr HouseColorParams MP_PURPLE     = {0.78f, 1.0f, 1.0f, 2.0f};
    constexpr HouseColorParams MP_PINK       = {0.90f, 0.7f, 1.0f, 2.0f};
}

/**
 * Get GLSL fragment shader source for house color hue shifting.
 * Intended to be composed into the sprite rendering shader.
 */
const char* GL_House_Color_Shader_Source();

#endif // RENDER_GL_HOUSE_COLOR_H
