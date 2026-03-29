/**
 * GLRenderer - OpenGL ES 2.0 rendering backend.
 *
 * Manages SDL3 GL context, compiles shaders for palette lookup and
 * RGBA textured quad rendering, handles texture upload/update.
 *
 * Shader programs:
 *   - Palette shader: GL_R8 indexed texture + 256x1 palette → RGB
 *   - RGBA shader: direct textured quad with optional house color hue shift
 */

#include "gl_renderer.h"

#include <SDL3/SDL.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <cstdlib>
#include <cstring>
#include <cstdio>

// SDL globals
extern SDL_Window* g_window;

// Vertex: position (x,y) + texcoord (u,v)
struct Vertex {
    float x, y, u, v;
};

// Shader sources
static const char* palette_vert_src = R"(
    attribute vec2 a_pos;
    attribute vec2 a_uv;
    varying vec2 v_uv;
    uniform vec2 u_scale;
    uniform vec2 u_offset;
    void main() {
        vec2 p = a_pos * u_scale + u_offset;
        gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
        v_uv = a_uv;
    }
)";

static const char* palette_frag_src = R"(
    precision mediump float;
    varying vec2 v_uv;
    uniform sampler2D u_indexed;
    uniform sampler2D u_palette;
    void main() {
        float idx = texture2D(u_indexed, v_uv).r;
        vec4 color = texture2D(u_palette, vec2(idx, 0.5));
        gl_FragColor = color;
    }
)";

static const char* rgba_vert_src = R"(
    attribute vec2 a_pos;
    attribute vec2 a_uv;
    varying vec2 v_uv;
    uniform vec4 u_src_rect;
    uniform vec4 u_dst_rect;
    uniform vec2 u_viewport;
    void main() {
        v_uv = u_src_rect.xy + a_uv * u_src_rect.zw;
        vec2 p = u_dst_rect.xy + a_pos * u_dst_rect.zw;
        p = p / u_viewport * 2.0 - 1.0;
        p.y = -p.y;
        gl_Position = vec4(p, 0.0, 1.0);
    }
)";

static const char* rgba_frag_src = R"(
    precision mediump float;
    varying vec2 v_uv;
    uniform sampler2D u_texture;
    uniform float u_house_hue;

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
        vec4 color = texture2D(u_texture, v_uv);
        if (u_house_hue >= 0.0) {
            float green_ratio = color.g / (max(color.r, max(color.g, color.b)) + 0.001);
            if (green_ratio > 0.6 && color.g > 0.3) {
                vec3 hsv = rgb2hsv(color.rgb);
                hsv.x = u_house_hue;
                color.rgb = hsv2rgb(hsv);
            }
        }
        gl_FragColor = color;
    }
)";

static GLuint Compile_Shader(GLenum type, const char* src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);

    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        fprintf(stderr, "Shader compile error: %s\n", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint Link_Program(GLuint vert, GLuint frag)
{
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vert);
    glAttachShader(prog, frag);
    glLinkProgram(prog);

    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        fprintf(stderr, "Program link error: %s\n", log);
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

struct GLRenderer::Impl {
    SDL_GLContext gl_ctx = nullptr;
    int width  = 0;
    int height = 0;

    // Palette shader
    GLuint pal_prog   = 0;
    GLint  pal_a_pos  = -1;
    GLint  pal_a_uv   = -1;
    GLint  pal_u_scale  = -1;
    GLint  pal_u_offset = -1;
    GLint  pal_u_indexed = -1;
    GLint  pal_u_palette = -1;

    // RGBA shader
    GLuint rgba_prog     = 0;
    GLint  rgba_a_pos    = -1;
    GLint  rgba_a_uv     = -1;
    GLint  rgba_u_src    = -1;
    GLint  rgba_u_dst    = -1;
    GLint  rgba_u_viewport = -1;
    GLint  rgba_u_texture  = -1;
    GLint  rgba_u_house    = -1;

    // Quad VBO
    GLuint quad_vbo = 0;
};

GLRenderer::GLRenderer()
    : impl_(new Impl)
{
}

GLRenderer::~GLRenderer()
{
    Shutdown();
    delete impl_;
}

bool GLRenderer::Init(int width, int height)
{
    impl_->width  = width;
    impl_->height = height;

    // Create GL context via SDL3
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

    impl_->gl_ctx = SDL_GL_CreateContext(g_window);
    if (!impl_->gl_ctx) return false;

    SDL_GL_MakeCurrent(g_window, impl_->gl_ctx);

    // Compile palette shader
    GLuint pv = Compile_Shader(GL_VERTEX_SHADER, palette_vert_src);
    GLuint pf = Compile_Shader(GL_FRAGMENT_SHADER, palette_frag_src);
    if (!pv || !pf) return false;
    impl_->pal_prog = Link_Program(pv, pf);
    glDeleteShader(pv);
    glDeleteShader(pf);
    if (!impl_->pal_prog) return false;

    impl_->pal_a_pos     = glGetAttribLocation(impl_->pal_prog, "a_pos");
    impl_->pal_a_uv      = glGetAttribLocation(impl_->pal_prog, "a_uv");
    impl_->pal_u_scale   = glGetUniformLocation(impl_->pal_prog, "u_scale");
    impl_->pal_u_offset  = glGetUniformLocation(impl_->pal_prog, "u_offset");
    impl_->pal_u_indexed = glGetUniformLocation(impl_->pal_prog, "u_indexed");
    impl_->pal_u_palette = glGetUniformLocation(impl_->pal_prog, "u_palette");

    // Compile RGBA shader
    GLuint rv = Compile_Shader(GL_VERTEX_SHADER, rgba_vert_src);
    GLuint rf = Compile_Shader(GL_FRAGMENT_SHADER, rgba_frag_src);
    if (!rv || !rf) return false;
    impl_->rgba_prog = Link_Program(rv, rf);
    glDeleteShader(rv);
    glDeleteShader(rf);
    if (!impl_->rgba_prog) return false;

    impl_->rgba_a_pos      = glGetAttribLocation(impl_->rgba_prog, "a_pos");
    impl_->rgba_a_uv       = glGetAttribLocation(impl_->rgba_prog, "a_uv");
    impl_->rgba_u_src      = glGetUniformLocation(impl_->rgba_prog, "u_src_rect");
    impl_->rgba_u_dst      = glGetUniformLocation(impl_->rgba_prog, "u_dst_rect");
    impl_->rgba_u_viewport = glGetUniformLocation(impl_->rgba_prog, "u_viewport");
    impl_->rgba_u_texture  = glGetUniformLocation(impl_->rgba_prog, "u_texture");
    impl_->rgba_u_house    = glGetUniformLocation(impl_->rgba_prog, "u_house_hue");

    // Quad VBO: unit quad (0,0)-(1,1) with UVs
    static const Vertex quad_verts[] = {
        {0.0f, 0.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 1.0f, 0.0f},
        {0.0f, 1.0f, 0.0f, 1.0f},
        {1.0f, 1.0f, 1.0f, 1.0f},
    };
    glGenBuffers(1, &impl_->quad_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, impl_->quad_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad_verts), quad_verts, GL_STATIC_DRAW);

    glViewport(0, 0, width, height);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    return true;
}

void GLRenderer::Shutdown()
{
    if (impl_->quad_vbo) { glDeleteBuffers(1, &impl_->quad_vbo); impl_->quad_vbo = 0; }
    if (impl_->pal_prog) { glDeleteProgram(impl_->pal_prog); impl_->pal_prog = 0; }
    if (impl_->rgba_prog) { glDeleteProgram(impl_->rgba_prog); impl_->rgba_prog = 0; }
    if (impl_->gl_ctx) {
        SDL_GL_DestroyContext(impl_->gl_ctx);
        impl_->gl_ctx = nullptr;
    }
}

uint32_t GLRenderer::Upload_Indexed(const void* pixels, int width, int height)
{
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, width, height, 0,
                 GL_LUMINANCE, GL_UNSIGNED_BYTE, pixels);
    return tex;
}

uint32_t GLRenderer::Upload_Palette(const void* palette_rgb, int num_entries)
{
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, num_entries, 1, 0,
                 GL_RGB, GL_UNSIGNED_BYTE, palette_rgb);
    return tex;
}

uint32_t GLRenderer::Upload_RGBA(const void* pixels, int width, int height)
{
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    return tex;
}

void GLRenderer::Update_Indexed(uint32_t tex_id, const void* pixels, int width, int height)
{
    glBindTexture(GL_TEXTURE_2D, tex_id);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height,
                    GL_LUMINANCE, GL_UNSIGNED_BYTE, pixels);
}

void GLRenderer::Update_RGBA(uint32_t tex_id, const void* pixels, int width, int height)
{
    glBindTexture(GL_TEXTURE_2D, tex_id);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height,
                    GL_RGBA, GL_UNSIGNED_BYTE, pixels);
}

static void Bind_Quad_VBO(GLuint vbo, GLint a_pos, GLint a_uv)
{
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glEnableVertexAttribArray(a_pos);
    glVertexAttribPointer(a_pos, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(a_uv);
    glVertexAttribPointer(a_uv, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(8));
}

void GLRenderer::Draw_Palette_Quad(uint32_t indexed_tex, uint32_t palette_tex,
                                    float x, float y, float w, float h,
                                    float zoom)
{
    glUseProgram(impl_->pal_prog);

    float vw = static_cast<float>(impl_->width);
    float vh = static_cast<float>(impl_->height);

    glUniform2f(impl_->pal_u_scale, (w * zoom) / vw, (h * zoom) / vh);
    glUniform2f(impl_->pal_u_offset, x / vw, y / vh);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, indexed_tex);
    glUniform1i(impl_->pal_u_indexed, 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, palette_tex);
    glUniform1i(impl_->pal_u_palette, 1);

    Bind_Quad_VBO(impl_->quad_vbo, impl_->pal_a_pos, impl_->pal_a_uv);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void GLRenderer::Draw_RGBA_Quad(uint32_t rgba_tex,
                                 float src_x, float src_y, float src_w, float src_h,
                                 float dst_x, float dst_y, float dst_w, float dst_h,
                                 float house_hue)
{
    glUseProgram(impl_->rgba_prog);

    glUniform4f(impl_->rgba_u_src, src_x, src_y, src_w, src_h);
    glUniform4f(impl_->rgba_u_dst, dst_x, dst_y, dst_w, dst_h);
    glUniform2f(impl_->rgba_u_viewport,
                static_cast<float>(impl_->width),
                static_cast<float>(impl_->height));
    glUniform1f(impl_->rgba_u_house, house_hue);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, rgba_tex);
    glUniform1i(impl_->rgba_u_texture, 0);

    Bind_Quad_VBO(impl_->quad_vbo, impl_->rgba_a_pos, impl_->rgba_a_uv);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void GLRenderer::Present()
{
    SDL_GL_SwapWindow(g_window);
}

void GLRenderer::Delete_Texture(uint32_t tex_id)
{
    if (!tex_id || !impl_->gl_ctx) return;
    GLuint t = tex_id;
    glDeleteTextures(1, &t);
}
