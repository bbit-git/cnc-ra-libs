/**
 * RenderCompositor - Layer compositing for world + UI separation.
 *
 * CPU mode: each layer has its own RGBA pixel buffer. Composite()
 * blits dirty layers to the output buffer with zoom (world) or
 * fixed positioning (UI).
 *
 * GL mode deferred to gl/gl_renderer.cpp integration.
 */

#include "render_compositor.h"
#include <cstdlib>
#include <cstring>

struct LayerData {
    RenderLayerDesc desc;
    uint8_t*        buffer;  // RGBA pixel buffer, owned
    bool            configured;
};

struct RenderCompositor::Impl {
    int        output_width  = 0;
    int        output_height = 0;
    uint8_t*   output_buffer = nullptr;
    float      world_zoom    = 1.0f;
    LayerData  layers[static_cast<int>(RenderLayerID::COUNT)];

    Impl() {
        memset(layers, 0, sizeof(layers));
    }

    ~Impl() {
        for (auto& l : layers) free(l.buffer);
        free(output_buffer);
    }
};

RenderCompositor::RenderCompositor()
    : impl_(new Impl)
{
}

RenderCompositor::~RenderCompositor()
{
    delete impl_;
}

void RenderCompositor::Init(int output_width, int output_height)
{
    impl_->output_width  = output_width;
    impl_->output_height = output_height;
    free(impl_->output_buffer);
    impl_->output_buffer = static_cast<uint8_t*>(
        calloc(output_width * output_height, 4));
}

void RenderCompositor::Configure_Layer(const RenderLayerDesc& desc)
{
    int idx = static_cast<int>(desc.id);
    if (idx < 0 || idx >= static_cast<int>(RenderLayerID::COUNT)) return;

    LayerData& ld = impl_->layers[idx];
    free(ld.buffer);
    ld.desc = desc;
    ld.buffer = static_cast<uint8_t*>(calloc(desc.width * desc.height, 4));
    ld.configured = true;
}

// Global accessor for engine logic (scrolling, clamping)
static float g_render_world_zoom = 1.0f;
float Render_Get_World_Zoom()
{
    return g_render_world_zoom;
}

void RenderCompositor::Set_World_Zoom(float zoom)
{
    if (zoom <= 0.0f) return;
    impl_->world_zoom = zoom;
    g_render_world_zoom = zoom;

    // Mark all world layers dirty when zoom changes
    for (int i = 0; i <= static_cast<int>(RenderLayerID::WORLD_SHROUD); i++) {
        if (impl_->layers[i].configured) {
            impl_->layers[i].desc.dirty = true;
        }
    }
}

float RenderCompositor::Get_World_Zoom() const
{
    return impl_->world_zoom;
}

void RenderCompositor::Invalidate_Layer(RenderLayerID layer)
{
    int idx = static_cast<int>(layer);
    if (idx >= 0 && idx < static_cast<int>(RenderLayerID::COUNT)) {
        impl_->layers[idx].desc.dirty = true;
    }
}

/// Blit RGBA src to RGBA dst at (dx, dy) with nearest-neighbor scaling.
static void Blit_Scaled(const uint8_t* src, int sw, int sh,
                         uint8_t* dst, int dw, int dh, int dst_pitch,
                         int dx, int dy, float zoom)
{
    int scaled_w = static_cast<int>(sw * zoom);
    int scaled_h = static_cast<int>(sh * zoom);

    for (int row = 0; row < scaled_h; row++) {
        int out_y = dy + row;
        if (out_y < 0 || out_y >= dh) continue;

        int src_y = static_cast<int>(row / zoom);
        if (src_y >= sh) src_y = sh - 1;

        const uint8_t* src_row = src + src_y * sw * 4;
        uint8_t* dst_row = dst + out_y * dst_pitch;

        for (int col = 0; col < scaled_w; col++) {
            int out_x = dx + col;
            if (out_x < 0 || out_x >= dw) continue;

            int src_x = static_cast<int>(col / zoom);
            if (src_x >= sw) src_x = sw - 1;

            const uint8_t* sp = src_row + src_x * 4;
            uint8_t* dp = dst_row + out_x * 4;

            // Alpha blend (src over dst)
            uint8_t sa = sp[3];
            if (sa == 255) {
                memcpy(dp, sp, 4);
            } else if (sa > 0) {
                uint8_t da = 255 - sa;
                dp[0] = (sp[0] * sa + dp[0] * da) / 255;
                dp[1] = (sp[1] * sa + dp[1] * da) / 255;
                dp[2] = (sp[2] * sa + dp[2] * da) / 255;
                dp[3] = sa + (dp[3] * da) / 255;
            }
        }
    }
}

/// Blit RGBA src to RGBA dst at (dx, dy), no scaling.
static void Blit_Direct(const uint8_t* src, int sw, int sh,
                         uint8_t* dst, int dw, int dh, int dst_pitch,
                         int dx, int dy)
{
    for (int row = 0; row < sh; row++) {
        int out_y = dy + row;
        if (out_y < 0 || out_y >= dh) continue;

        const uint8_t* src_row = src + row * sw * 4;
        uint8_t* dst_row = dst + out_y * dst_pitch;

        int x_start = (dx < 0) ? -dx : 0;
        int x_end = (dx + sw > dw) ? (dw - dx) : sw;

        for (int col = x_start; col < x_end; col++) {
            const uint8_t* sp = src_row + col * 4;
            uint8_t* dp = dst_row + (dx + col) * 4;

            uint8_t sa = sp[3];
            if (sa == 255) {
                memcpy(dp, sp, 4);
            } else if (sa > 0) {
                uint8_t da = 255 - sa;
                dp[0] = (sp[0] * sa + dp[0] * da) / 255;
                dp[1] = (sp[1] * sa + dp[1] * da) / 255;
                dp[2] = (sp[2] * sa + dp[2] * da) / 255;
                dp[3] = sa + (dp[3] * da) / 255;
            }
        }
    }
}

void RenderCompositor::Composite()
{
    if (!impl_->output_buffer) return;

    int ow = impl_->output_width;
    int oh = impl_->output_height;
    int op = ow * 4;

    // Clear output
    memset(impl_->output_buffer, 0, ow * oh * 4);

    // Composite layers in order (bottom to top)
    for (int i = 0; i < static_cast<int>(RenderLayerID::COUNT); i++) {
        LayerData& ld = impl_->layers[i];
        if (!ld.configured || !ld.buffer) continue;

        bool is_world = (i <= static_cast<int>(RenderLayerID::WORLD_SHROUD));

        if (is_world && impl_->world_zoom != 1.0f) {
            int dx = ld.desc.offset_x;
            int dy = ld.desc.offset_y;

            // Center world layer if scaled size is smaller than output area
            int scaled_w = static_cast<int>(ld.desc.width * impl_->world_zoom);
            int scaled_h = static_cast<int>(ld.desc.height * impl_->world_zoom);
            int avail_w = ow - ld.desc.offset_x;
            int avail_h = oh - ld.desc.offset_y;
            if (scaled_w < avail_w) dx += (avail_w - scaled_w) / 2;
            if (scaled_h < avail_h) dy += (avail_h - scaled_h) / 2;

            Blit_Scaled(ld.buffer, ld.desc.width, ld.desc.height,
                        impl_->output_buffer, ow, oh, op,
                        dx, dy, impl_->world_zoom);
        } else {
            Blit_Direct(ld.buffer, ld.desc.width, ld.desc.height,
                        impl_->output_buffer, ow, oh, op,
                        ld.desc.offset_x, ld.desc.offset_y);
        }

        ld.desc.dirty = false;
    }
}

void* RenderCompositor::Get_Layer_Buffer(RenderLayerID layer)
{
    int idx = static_cast<int>(layer);
    if (idx < 0 || idx >= static_cast<int>(RenderLayerID::COUNT)) return nullptr;
    return impl_->layers[idx].buffer;
}

int RenderCompositor::Get_Layer_Width(RenderLayerID layer) const
{
    int idx = static_cast<int>(layer);
    if (idx < 0 || idx >= static_cast<int>(RenderLayerID::COUNT)) return 0;
    if (!impl_->layers[idx].configured) return 0;
    return impl_->layers[idx].desc.width;
}

int RenderCompositor::Get_Layer_Height(RenderLayerID layer) const
{
    int idx = static_cast<int>(layer);
    if (idx < 0 || idx >= static_cast<int>(RenderLayerID::COUNT)) return 0;
    if (!impl_->layers[idx].configured) return 0;
    return impl_->layers[idx].desc.height;
}

void* RenderCompositor::Get_Output()
{
    return impl_->output_buffer;
}

const void* RenderCompositor::Get_Output() const
{
    return impl_->output_buffer;
}

int RenderCompositor::Get_Output_Width() const
{
    return impl_->output_width;
}

int RenderCompositor::Get_Output_Height() const
{
    return impl_->output_height;
}
