/**
 * RenderLayer - Named render target for compositing.
 *
 * Separates world rendering from UI rendering. Each layer has its
 * own viewport and can be independently transformed (e.g. zoom on world).
 */

#ifndef RENDER_RENDER_LAYER_H
#define RENDER_RENDER_LAYER_H

#include <cstdint>

enum class RenderLayerID : uint8_t {
    WORLD_TERRAIN,    // Ground tiles, templates, overlays
    WORLD_OBJECTS,    // Units, buildings, infantry (LAYER_GROUND)
    WORLD_EFFECTS,    // Explosions, flames (LAYER_AIR)
    WORLD_AIR,        // Aircraft, bullets (LAYER_TOP)
    WORLD_SHROUD,     // Fog of war overlay
    UI_SIDEBAR,       // Build queue sidebar
    UI_RADAR,         // Minimap
    UI_POWER,         // Power bar
    UI_TAB,           // Credits / EVA tab
    UI_OVERLAY,       // Messages, action menu, tooltips
    UI_OPTIONS,       // In-game options dialog (topmost UI)
    COUNT
};

/**
 * Properties for a render layer.
 */
struct RenderLayerDesc {
    RenderLayerID id;
    int           width;       // Layer buffer width (pixels)
    int           height;      // Layer buffer height (pixels)
    float         zoom;        // Zoom factor (1.0 = default, world layers only)
    int           offset_x;    // Compositing position on final output
    int           offset_y;
    bool          dirty;       // Needs re-render this frame
};

#endif // RENDER_RENDER_LAYER_H
