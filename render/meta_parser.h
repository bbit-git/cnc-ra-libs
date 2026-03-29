/**
 * MetaParser - Parse .META companion files for HD TGA sprites.
 *
 * META format (JSON):
 *   {"size":[canvas_w, canvas_h], "crop":[x, y, w, h]}
 *
 * - size: original canvas dimensions (for positioning the sprite)
 * - crop: bounding box of non-transparent pixels within the canvas
 *         The TGA file dimensions match crop w/h.
 */

#ifndef RENDER_META_PARSER_H
#define RENDER_META_PARSER_H

#include <cstddef>

struct SpriteMeta {
    int canvas_width;
    int canvas_height;
    int crop_x;
    int crop_y;
    int crop_width;
    int crop_height;
};

/**
 * Parse a .META JSON buffer.
 * @param data      Raw META file contents (not null-terminated required)
 * @param size      Size of data in bytes
 * @param meta_out  Filled on success
 * @return true if parsed successfully
 */
bool Meta_Parse(const void* data, size_t size, SpriteMeta& meta_out);

#endif // RENDER_META_PARSER_H
