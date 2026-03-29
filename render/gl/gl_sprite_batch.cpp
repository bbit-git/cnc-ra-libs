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

// Batch vertex: position + UV + house hue + fade
struct BatchVertex {
    float x, y;
    float u, v;
    float house_hue;
    float fade;
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
    attribute float a_fade;
    varying vec2 v_uv;
    varying float v_house_hue;
    varying float v_fade;
    uniform vec2 u_viewport;
    void main() {
        vec2 p = a_pos / u_viewport * 2.0 - 1.0;
        p.y = -p.y;
        gl_Position = vec4(p, 0.0, 1.0);
        v_uv = a_uv;
        v_house_hue = a_house_hue;
        v_fade = a_fade;
    }
)";

static const char* batch_frag_src = R"(
    precision mediump float;
    varying vec2 v_uv;
    varying float v_house_hue;
    varying float v_fade;
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

        if (v_house_hue >= 0.0) {
            float green_ratio = color.g / (max(color.r, max(color.g, color.b)) + 0.001);
            if (green_ratio > 0.6 && color.g > 0.3) {
                vec3 hsv = rgb2hsv(color.rgb);
                hsv.x = v_house_hue;
                color.rgb = hsv2rgb(hsv);
            }
        }

        if (v_fade > 0.0) {
            color.rgb = mix(color.rgb, vec3(0.0), v_fade);
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
    GLint  a_fade   = -1;
    GLint  u_viewport = -1;
    GLint  u_atlas  = -1;

    GLuint vbo      = 0;
    GLuint ibo      = 0;

    std::vector<SpriteBatchEntry> entries;
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
        a_fade  = glGetAttribLocation(prog, "a_fade");
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

        // Get viewport size
        GLint vp[4];
        glGetIntegerv(GL_VIEWPORT, vp);
        viewport_w = vp[2];
        viewport_h = vp[3];

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

void GLSpriteBatch::Flush()
{
    if (impl_->entries.empty()) return;

    // Sort by atlas page for batching
    std::sort(impl_->entries.begin(), impl_->entries.end(),
              [](const SpriteBatchEntry& a, const SpriteBatchEntry& b) {
                  return a.region.atlas_id < b.region.atlas_id;
              });

    // Count sprites and draw calls (works even without GL for testing)
    {
        size_t idx = 0;
        while (idx < impl_->entries.size()) {
            uint16_t cur = impl_->entries[idx].region.atlas_id;
            int count = 0;
            while (idx < impl_->entries.size() &&
                   impl_->entries[idx].region.atlas_id == cur) {
                count++;
                idx++;
            }
            impl_->sprite_count += count;
            impl_->draw_calls++;
        }
    }

    // GL rendering (skip if no GL context available)
    if (!impl_->Init_GL()) return;

    glUseProgram(impl_->prog);
    glUniform2f(impl_->u_viewport,
                static_cast<float>(impl_->viewport_w),
                static_cast<float>(impl_->viewport_h));
    glUniform1i(impl_->u_atlas, 0);
    glActiveTexture(GL_TEXTURE0);

    glBindBuffer(GL_ARRAY_BUFFER, impl_->vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, impl_->ibo);

    glEnableVertexAttribArray(impl_->a_pos);
    glEnableVertexAttribArray(impl_->a_uv);
    if (impl_->a_house >= 0) glEnableVertexAttribArray(impl_->a_house);
    if (impl_->a_fade >= 0)  glEnableVertexAttribArray(impl_->a_fade);

    int stride = sizeof(BatchVertex);
    glVertexAttribPointer(impl_->a_pos, 2, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(0));
    glVertexAttribPointer(impl_->a_uv, 2, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(8));
    if (impl_->a_house >= 0)
        glVertexAttribPointer(impl_->a_house, 1, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<void*>(16));
    if (impl_->a_fade >= 0)
        glVertexAttribPointer(impl_->a_fade, 1, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<void*>(20));

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
            float fade = e.fade / 255.0f;

            verts.push_back({x0, y0, u0, v0, hue, fade});
            verts.push_back({x1, y0, u1, v0, hue, fade});
            verts.push_back({x0, y1, u0, v1, hue, fade});
            verts.push_back({x1, y1, u1, v1, hue, fade});

            quad_count++;
            i++;
        }

        // Upload vertex data
        glBufferSubData(GL_ARRAY_BUFFER, 0,
                        verts.size() * sizeof(BatchVertex), verts.data());

        // Bind atlas page texture (caller manages texture IDs externally)
        // For now we assume atlas page textures are bound sequentially
        // The actual binding is done by the integration layer
        glDrawElements(GL_TRIANGLES,
                       quad_count * INDICES_PER_QUAD,
                       GL_UNSIGNED_SHORT, nullptr);
    }

    glDisableVertexAttribArray(impl_->a_pos);
    glDisableVertexAttribArray(impl_->a_uv);
    if (impl_->a_house >= 0) glDisableVertexAttribArray(impl_->a_house);
    if (impl_->a_fade >= 0)  glDisableVertexAttribArray(impl_->a_fade);
}

int GLSpriteBatch::Draw_Call_Count() const
{
    return impl_->draw_calls;
}

int GLSpriteBatch::Sprite_Count() const
{
    return impl_->sprite_count;
}
