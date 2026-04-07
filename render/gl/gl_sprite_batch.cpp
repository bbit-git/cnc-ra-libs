/**
 * GLSpriteBatch - Batched sprite rendering via GL.
 *
 * Collects SpriteBatchEntry quads per frame, sorts by atlas page,
 * and issues one draw call per atlas page for minimal state changes.
 *
 * Vertex format per quad: 4 vertices × (pos.xy + uv.xy + house_hue + flags)
 * Rendered as indexed triangles (6 indices per quad).
 */

#include "gl_sprite_batch.h"

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <cstdlib>
#include <cstring>
#include <vector>
#include <algorithm>

// Batch vertex: position + UV + house hue + effect + opacity
struct BatchVertex {
    float x, y;
    float u, v;
    float house_hue;
    float effect;
    float opacity;
};

// Maximum quads per flush
static constexpr int MAX_QUADS = 4096;
static constexpr int VERTS_PER_QUAD = 4;
static constexpr int INDICES_PER_QUAD = 6;

// Sprite batch shader (embedded)
static const char* batch_vert_src = R"(
    attribute vec2 a_pos;
    attribute vec2 a_uv;
    attribute float a_house_hue;
    attribute float a_effect;
    attribute float a_opacity;
    varying vec2 v_uv;
    varying float v_house_hue;
    varying float v_effect;
    varying float v_opacity;
    uniform vec2 u_viewport;
    void main() {
        vec2 p = a_pos / u_viewport * 2.0 - 1.0;
        p.y = -p.y;
        gl_Position = vec4(p, 0.0, 1.0);
        v_uv = a_uv;
        v_house_hue = a_house_hue;
        v_effect = a_effect;
        v_opacity = a_opacity;
    }
)";

static const char* batch_frag_src = R"(
    precision mediump float;
    varying vec2 v_uv;
    varying float v_house_hue;
    varying float v_effect;
    varying float v_opacity;
    uniform sampler2D u_atlas;

    vec3 rgb2hsv(vec3 c) {
        vec4 K = vec4(0.0, -1.0/3.0, 2.0/3.0, -1.0);
        vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
        vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
        float d = q.x - min(q.w, q.y);
        float e = 1.0e-10;
        return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
    }

    vec3 hsv2rgb(vec3 c) {
        vec4 K = vec4(1.0, 2.0/3.0, 1.0/3.0, 3.0);
        vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
        return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
    }

    void main() {
        vec4 color = texture2D(u_atlas, v_uv);
        if (color.a < 0.01) discard;
        if (v_effect < 0.5 && v_opacity <= 0.0) {
            color.a = 1.0;
        }

        if (v_house_hue >= 0.0) {
            float max_ch = max(color.r, max(color.g, color.b));
            float rb_avg = (color.r + color.b) * 0.5;
            float dominance = color.g / (rb_avg + 0.001);
            if (max_ch > 0.05 && dominance >= 2.0 && color.g > 0.2) {
                vec3 hsv = rgb2hsv(color.rgb);
                hsv.x = v_house_hue;
                color.rgb = hsv2rgb(hsv);
            }
        }

        if (v_effect > 5.5) {
            color.rgb = mix(color.rgb, vec3(0.0), 0.35);
            color.a *= v_opacity;
        } else if (v_effect > 4.5) {
            color.rgb = mix(color.rgb, vec3(1.0, 1.0, 0.85), 0.5);
            color.a *= v_opacity;
        } else if (v_effect > 3.5) {
            color.rgb = mix(color.rgb, vec3(0.75, 0.95, 1.0), 0.45);
            color.a *= v_opacity;
        } else if (v_effect > 2.5) {
            color.rgb = mix(color.rgb, vec3(1.0), 0.7);
            color.a *= v_opacity;
        } else if (v_effect > 1.5) {
            // Preserve edge antialiasing from the remastered alpha mask, but
            // keep shadows visibly softer than a pure black silhouette.
            color.rgb = mix(color.rgb, vec3(0.0), 0.7);
            color.a *= v_opacity * 0.75;
        } else if (v_effect > 0.5) {
            color.a *= v_opacity;
        } else if (v_opacity > 0.0) {
            color.rgb = mix(color.rgb, vec3(0.0), v_opacity);
        }

        gl_FragColor = color;
    }
)";

static GLuint Compile_Shader_Batch(GLenum type, const char* src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        glDeleteShader(s);
        return 0;
    }
    return s;
}

struct GLSpriteBatch::Impl {
    GLuint prog     = 0;
    GLint  a_pos    = -1;
    GLint  a_uv     = -1;
    GLint  a_house  = -1;
    GLint  a_effect = -1;
    GLint  a_opacity = -1;
    GLint  u_viewport = -1;
    GLint  u_atlas  = -1;

    GLuint vbo      = 0;
    GLuint ibo      = 0;

    std::vector<SpriteBatchEntry> entries;
    std::vector<GLuint> page_textures;
    int    draw_calls = 0;
    int    sprite_count = 0;
    int    viewport_w = 0;
    int    viewport_h = 0;
    bool   initialized = false;

    bool Init_GL() {
        if (initialized) return true;

        GLuint vs = Compile_Shader_Batch(GL_VERTEX_SHADER, batch_vert_src);
        GLuint fs = Compile_Shader_Batch(GL_FRAGMENT_SHADER, batch_frag_src);
        if (!vs || !fs) return false;

        prog = glCreateProgram();
        glAttachShader(prog, vs);
        glAttachShader(prog, fs);
        glLinkProgram(prog);
        glDeleteShader(vs);
        glDeleteShader(fs);

        GLint ok = 0;
        glGetProgramiv(prog, GL_LINK_STATUS, &ok);
        if (!ok) return false;

        a_pos   = glGetAttribLocation(prog, "a_pos");
        a_uv    = glGetAttribLocation(prog, "a_uv");
        a_house = glGetAttribLocation(prog, "a_house_hue");
        a_effect = glGetAttribLocation(prog, "a_effect");
        a_opacity = glGetAttribLocation(prog, "a_opacity");
        u_viewport = glGetUniformLocation(prog, "u_viewport");
        u_atlas = glGetUniformLocation(prog, "u_atlas");

        // Pre-allocate VBO
        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     MAX_QUADS * VERTS_PER_QUAD * sizeof(BatchVertex),
                     nullptr, GL_DYNAMIC_DRAW);

        // Pre-build index buffer (0,1,2, 2,1,3 pattern)
        glGenBuffers(1, &ibo);
        std::vector<uint16_t> indices(MAX_QUADS * INDICES_PER_QUAD);
        for (int i = 0; i < MAX_QUADS; i++) {
            int base = i * 4;
            int idx  = i * 6;
            indices[idx + 0] = base + 0;
            indices[idx + 1] = base + 1;
            indices[idx + 2] = base + 2;
            indices[idx + 3] = base + 2;
            indices[idx + 4] = base + 1;
            indices[idx + 5] = base + 3;
        }
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     indices.size() * sizeof(uint16_t),
                     indices.data(), GL_STATIC_DRAW);

        initialized = true;
        return true;
    }
};

GLSpriteBatch::GLSpriteBatch()
    : impl_(new Impl)
{
}

GLSpriteBatch::~GLSpriteBatch()
{
    if (impl_->vbo) glDeleteBuffers(1, &impl_->vbo);
    if (impl_->ibo) glDeleteBuffers(1, &impl_->ibo);
    if (impl_->prog) glDeleteProgram(impl_->prog);
    delete impl_;
}

void GLSpriteBatch::Begin()
{
    impl_->entries.clear();
    impl_->draw_calls = 0;
    impl_->sprite_count = 0;
}

void GLSpriteBatch::Add(const SpriteBatchEntry& entry)
{
    impl_->entries.push_back(entry);
}

void GLSpriteBatch::Set_Page_Texture(uint16_t atlas_id, uint32_t texture_id)
{
    if (atlas_id >= impl_->page_textures.size()) {
        impl_->page_textures.resize(atlas_id + 1, 0);
    }
    impl_->page_textures[atlas_id] = texture_id;
}

void GLSpriteBatch::Clear_Page_Textures()
{
    impl_->page_textures.clear();
}

void GLSpriteBatch::Flush()
{
    impl_->draw_calls = 0;
    impl_->sprite_count = static_cast<int>(impl_->entries.size());
    if (impl_->entries.empty()) return;

    // DO NOT sort by atlas page — draw list order must be preserved for correct
    // overlapping (e.g., tank body drawn before turret). Instead, we flush on
    // page transitions, batching consecutive same-page sprites together.

    // Count draw calls: each contiguous run of same-page sprites is one draw,
    // split further if exceeding MAX_QUADS.
    {
        size_t idx = 0;
        while (idx < impl_->entries.size()) {
            uint16_t cur = impl_->entries[idx].region.atlas_id;
            size_t count = 0;
            while (idx < impl_->entries.size() &&
                   impl_->entries[idx].region.atlas_id == cur) {
                count++;
                idx++;
            }
            impl_->draw_calls += static_cast<int>((count + MAX_QUADS - 1) / MAX_QUADS);
        }
    }

    // GL rendering (skip if no GL context available)
    if (!impl_->Init_GL()) return;

    GLint vp[4];
    glGetIntegerv(GL_VIEWPORT, vp);
    impl_->viewport_w = vp[2];
    impl_->viewport_h = vp[3];

    glUseProgram(impl_->prog);
    glUniform2f(impl_->u_viewport,
                static_cast<float>(impl_->viewport_w),
                static_cast<float>(impl_->viewport_h));
    glUniform1i(impl_->u_atlas, 0);
    glActiveTexture(GL_TEXTURE0);

    GLboolean blend_enabled = glIsEnabled(GL_BLEND);
    GLint blend_src_rgb = GL_ONE;
    GLint blend_dst_rgb = GL_ZERO;
    GLint blend_src_alpha = GL_ONE;
    GLint blend_dst_alpha = GL_ZERO;
    glGetIntegerv(GL_BLEND_SRC_RGB, &blend_src_rgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &blend_dst_rgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &blend_src_alpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &blend_dst_alpha);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                        GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    glBindBuffer(GL_ARRAY_BUFFER, impl_->vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, impl_->ibo);

    glEnableVertexAttribArray(impl_->a_pos);
    glEnableVertexAttribArray(impl_->a_uv);
    if (impl_->a_house >= 0) glEnableVertexAttribArray(impl_->a_house);
    if (impl_->a_effect >= 0) glEnableVertexAttribArray(impl_->a_effect);
    if (impl_->a_opacity >= 0) glEnableVertexAttribArray(impl_->a_opacity);

    int stride = sizeof(BatchVertex);
    glVertexAttribPointer(impl_->a_pos, 2, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(0));
    glVertexAttribPointer(impl_->a_uv, 2, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(8));
    if (impl_->a_house >= 0)
        glVertexAttribPointer(impl_->a_house, 1, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<void*>(16));
    if (impl_->a_effect >= 0)
        glVertexAttribPointer(impl_->a_effect, 1, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<void*>(20));
    if (impl_->a_opacity >= 0)
        glVertexAttribPointer(impl_->a_opacity, 1, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<void*>(24));

    // Flush in batches per atlas page
    size_t i = 0;
    while (i < impl_->entries.size()) {
        uint16_t current_atlas = impl_->entries[i].region.atlas_id;

        // Build vertex data for this batch
        std::vector<BatchVertex> verts;
        verts.reserve(MAX_QUADS * VERTS_PER_QUAD);
        int quad_count = 0;

        while (i < impl_->entries.size() &&
               impl_->entries[i].region.atlas_id == current_atlas &&
               quad_count < MAX_QUADS) {
            const SpriteBatchEntry& e = impl_->entries[i];
            const AtlasRegion& r = e.region;

            float x0 = e.dst_x;
            float y0 = e.dst_y;
            float x1 = x0 + r.w * e.scale_x;
            float y1 = y0 + r.h * e.scale_y;

            float u0 = r.u0, v0 = r.v0;
            float u1 = r.u1, v1 = r.v1;

            // Handle horizontal/vertical flip
            if (e.flags & 0x01) { float t = u0; u0 = u1; u1 = t; }
            if (e.flags & 0x02) { float t = v0; v0 = v1; v1 = t; }

            float hue = e.house_hue;
            float effect = 0.0f;
            if (e.flags & 0x04) effect = 1.0f;
            if (e.flags & 0x08) effect = 2.0f;
            if (e.flags & 0x10) effect = 3.0f;
            if (e.flags & 0x20) effect = 4.0f;
            if (e.flags & 0x40) effect = 5.0f;
            if (e.flags & 0x80) effect = 6.0f;
            float opacity = e.fade / 255.0f;

            verts.push_back({x0, y0, u0, v0, hue, effect, opacity});
            verts.push_back({x1, y0, u1, v0, hue, effect, opacity});
            verts.push_back({x0, y1, u0, v1, hue, effect, opacity});
            verts.push_back({x1, y1, u1, v1, hue, effect, opacity});

            quad_count++;
            i++;
        }

        // Upload vertex data
        glBufferSubData(GL_ARRAY_BUFFER, 0,
                        verts.size() * sizeof(BatchVertex), verts.data());

        if (current_atlas < impl_->page_textures.size()) {
            GLuint texture = impl_->page_textures[current_atlas];
            glBindTexture(GL_TEXTURE_2D, texture);
        } else {
            glBindTexture(GL_TEXTURE_2D, 0);
        }

        glDrawElements(GL_TRIANGLES,
                       quad_count * INDICES_PER_QUAD,
                       GL_UNSIGNED_SHORT, nullptr);
    }

    glDisableVertexAttribArray(impl_->a_pos);
    glDisableVertexAttribArray(impl_->a_uv);
    if (impl_->a_house >= 0) glDisableVertexAttribArray(impl_->a_house);
    if (impl_->a_effect >= 0) glDisableVertexAttribArray(impl_->a_effect);
    if (impl_->a_opacity >= 0) glDisableVertexAttribArray(impl_->a_opacity);
    glBlendFuncSeparate(blend_src_rgb, blend_dst_rgb, blend_src_alpha, blend_dst_alpha);
    if (!blend_enabled) {
        glDisable(GL_BLEND);
    }
}

int GLSpriteBatch::Draw_Call_Count() const
{
    return impl_->draw_calls;
}

int GLSpriteBatch::Sprite_Count() const
{
    return impl_->sprite_count;
}
