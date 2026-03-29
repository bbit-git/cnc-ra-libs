/**
 * GLHouseColor - GLSL shader source for house color hue remapping.
 *
 * Green chroma key detection: identifies pixels where green channel
 * dominates (green_ratio = G / max(R,G,B) > threshold).
 * Hue shift: converts green to target faction color via RGB→HSV→RGB.
 *
 * This function provides the GLSL source that gets composed into
 * the sprite rendering fragment shader. It can also be used
 * standalone for testing.
 */

#include "gl_house_color.h"

static const char* house_color_glsl = R"(
// House color hue-shift function.
// Call this from the fragment shader after sampling the texture color.
//
// Inputs:
//   color     - sampled RGBA pixel
//   hue       - target hue (0.0-1.0, maps to 0°-360°)
//   sat_mult  - saturation multiplier (1.0 = unchanged)
//   val_mult  - brightness multiplier (1.0 = unchanged)
//   threshold - green detection threshold (ratio of G to max channel; 2.0 default)
//
// Returns: modified color with green regions shifted to target hue

vec3 house_rgb2hsv(vec3 c) {
    vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

vec3 house_hsv2rgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

vec4 apply_house_color(vec4 color, float hue, float sat_mult,
                       float val_mult, float threshold) {
    // Green channel dominance check
    float max_ch = max(color.r, max(color.g, color.b));
    if (max_ch < 0.05) return color; // too dark to remap

    float green_ratio = color.g / (max_ch + 0.001);

    // Detect green chroma key: green must be dominant and bright enough
    // The threshold is expressed as a ratio — default 2.0 means G must be
    // at least twice the average of R and B
    float rb_avg = (color.r + color.b) * 0.5;
    float dominance = color.g / (rb_avg + 0.001);

    if (dominance >= threshold && color.g > 0.2) {
        vec3 hsv = house_rgb2hsv(color.rgb);

        // Preserve saturation and value relative to the green key,
        // but shift hue to faction color
        hsv.x = hue;
        hsv.y *= sat_mult;
        hsv.z *= val_mult;

        // Smooth blending at edges: transition between original and
        // remapped color based on how strongly green dominates.
        // This preserves anti-aliased edges.
        float blend = smoothstep(threshold * 0.8, threshold * 1.2, dominance);
        vec3 remapped = house_hsv2rgb(hsv);
        color.rgb = mix(color.rgb, remapped, blend);
    }

    return color;
}
)";

const char* GL_House_Color_Shader_Source()
{
    return house_color_glsl;
}
