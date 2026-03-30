/**
 * RenderCompositor - Composites render layers into the final frame.
 *
 * Replaces the current GScreenClass::Render() → Blit_Display() pipeline.
 * Draws world layers (with zoom) and UI layers (fixed) into the output.
 *
 * In CPU mode: blits to HidPage as today.
 * In GL mode: renders textured quads per layer with appropriate transforms.
 */

#ifndef RENDER_RENDER_COMPOSITOR_H
#define RENDER_RENDER_COMPOSITOR_H

#include "render_layer.h"

class RenderCompositor {
public:
    RenderCompositor();
    ~RenderCompositor();

    /**
     * Initialize the compositor with output dimensions.
     */
    void Init(int output_width, int output_height);

    /**
     * Set properties for a layer.
     */
    void Configure_Layer(const RenderLayerDesc& desc);

    /**
     * Set world zoom level (applies to all WORLD_* layers).
     * @param zoom  Zoom factor: 1.0 = normal, 2.0 = 2x zoom in, 0.5 = zoom out
     */
    void Set_World_Zoom(float zoom);

    /**
     * Get current world zoom.
     */
    float Get_World_Zoom() const;

    /**
     * Mark a layer as needing re-render this frame.
     */
    void Invalidate_Layer(RenderLayerID layer);

    /**
     * Composite all dirty layers into the final output.
     * Called once per frame after all layers have been rendered.
     */
    void Composite();

    /**
     * Get the render target buffer for a layer (CPU mode).
     * Layer rendering code writes pixels here.
     * @return Pointer to pixel buffer, or nullptr if layer not configured
     */
    void* Get_Layer_Buffer(RenderLayerID layer);

    int Get_Layer_Width(RenderLayerID layer) const;
    int Get_Layer_Height(RenderLayerID layer) const;

    /// Get the composited RGBA output buffer.
    /// Writable for direct passthrough (menus), read-only after Composite().
    void* Get_Output();
    const void* Get_Output() const;

    int Get_Output_Width() const;
    int Get_Output_Height() const;

private:
    struct Impl;
    Impl* impl_;
};

/**
 * Global accessor for engine logic (scrolling, clamping).
 * Returns the zoom factor from the last Set_World_Zoom call.
 */
float Render_Get_World_Zoom();

#endif // RENDER_RENDER_COMPOSITOR_H
