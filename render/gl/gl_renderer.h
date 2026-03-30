/**
 * GLRenderer - OpenGL ES rendering backend.
 *
 * Manages GL context, shaders, and texture uploads.
 * Replaces the SDL_Renderer + SDL_UpdateTexture presentation path
 * with direct GL ES calls for compositing and sprite rendering.
 */

#ifndef RENDER_GL_RENDERER_H
#define RENDER_GL_RENDERER_H

#include <cstdint>

class GLRenderer {
public:
    GLRenderer();
    ~GLRenderer();

    /**
     * Initialize GL ES context and compile shaders.
     * @param width   Output width
     * @param height  Output height
     * @return true on success
     */
    bool Init(int width, int height);

    /**
     * Release GL resources and destroy the context.
     */
    void Shutdown();

    /**
     * Upload an 8-bit indexed buffer as a GL_R8 texture.
     * Used for legacy SHP world rendering (palette lookup in shader).
     * @return GL texture ID
     */
    uint32_t Upload_Indexed(const void* pixels, int width, int height);

    /**
     * Upload a 256-entry RGB palette as a 256x1 GL_RGB texture.
     * @return GL texture ID
     */
    uint32_t Upload_Palette(const void* palette_rgb, int num_entries = 256);

    /**
     * Upload an RGBA buffer as a GL_RGBA texture.
     * Used for HD sprites and atlas pages.
     * @return GL texture ID
     */
    uint32_t Upload_RGBA(const void* pixels, int width, int height);

    /**
     * Update an existing texture (sub-image upload).
     */
    void Update_Indexed(uint32_t tex_id, const void* pixels, int width, int height);

    /**
     * Update an existing RGBA texture (sub-image upload).
     */
    void Update_RGBA(uint32_t tex_id, const void* pixels, int width, int height);

    /**
     * Draw a textured quad using the palette lookup shader.
     * Renders indexed_tex through palette_tex to produce RGB output.
     */
    void Draw_Palette_Quad(uint32_t indexed_tex, uint32_t palette_tex,
                           float x, float y, float w, float h,
                           float zoom = 1.0f);

    /**
     * Draw a textured quad using the RGBA shader.
     * Supports optional house color hue shifting.
     */
    void Draw_RGBA_Quad(uint32_t rgba_tex,
                        float src_x, float src_y, float src_w, float src_h,
                        float dst_x, float dst_y, float dst_w, float dst_h,
                        float house_hue = -1.0f);

    /**
     * Present the frame (calls eglSwapBuffers or equivalent).
     */
    void Present();

    /**
     * Delete a texture.
     */
    void Delete_Texture(uint32_t tex_id);

private:
    struct Impl;
    Impl* impl_;
};

#endif // RENDER_GL_RENDERER_H
