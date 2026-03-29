/**
 * MetaParser - Minimal JSON parser for .META companion files.
 *
 * META format: {"size":[W,H],"crop":[X,Y,W,H]}
 *
 * This is a hand-rolled parser — no JSON library dependency.
 * The format is rigid enough that we just scan for the arrays.
 */

#include "meta_parser.h"
#include <cstring>
#include <cstdlib>

/// Skip whitespace, return pointer to next non-whitespace or end.
static const char* skip_ws(const char* p, const char* end)
{
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
        p++;
    return p;
}

/// Parse an integer at p, advancing p past it. Returns false on failure.
static bool parse_int(const char*& p, const char* end, int& out)
{
    p = skip_ws(p, end);
    if (p >= end) return false;

    bool neg = false;
    if (*p == '-') { neg = true; p++; }
    if (p >= end || *p < '0' || *p > '9') return false;

    int val = 0;
    while (p < end && *p >= '0' && *p <= '9') {
        val = val * 10 + (*p - '0');
        p++;
    }
    out = neg ? -val : val;
    return true;
}

/// Find a key in the JSON and position p after the '[' of its array value.
static bool find_array(const char*& p, const char* end, const char* key)
{
    size_t key_len = strlen(key);
    while (p < end) {
        // Look for the key string
        const char* found = static_cast<const char*>(
            memmem(p, end - p, key, key_len));
        if (!found) return false;

        p = found + key_len;
        p = skip_ws(p, end);

        // Expect ':'
        if (p >= end || *p != ':') continue;
        p++;
        p = skip_ws(p, end);

        // Expect '['
        if (p >= end || *p != '[') continue;
        p++; // past '['
        return true;
    }
    return false;
}

/// Parse a comma-separated array of N ints (already positioned past '[').
static bool parse_int_array(const char*& p, const char* end, int* out, int count)
{
    for (int i = 0; i < count; i++) {
        if (!parse_int(p, end, out[i])) return false;
        p = skip_ws(p, end);
        if (i < count - 1) {
            if (p >= end || *p != ',') return false;
            p++; // skip comma
        }
    }
    // Expect ']'
    p = skip_ws(p, end);
    if (p >= end || *p != ']') return false;
    p++;
    return true;
}

bool Meta_Parse(const void* data, size_t size, SpriteMeta& meta_out)
{
    if (!data || size == 0) return false;

    const char* buf = static_cast<const char*>(data);
    const char* end = buf + size;

    memset(&meta_out, 0, sizeof(meta_out));

    bool got_size = false;
    bool got_crop = false;

    // Parse "size":[W,H]
    const char* p = buf;
    if (find_array(p, end, "\"size\"")) {
        int vals[2];
        if (parse_int_array(p, end, vals, 2)) {
            meta_out.canvas_width  = vals[0];
            meta_out.canvas_height = vals[1];
            got_size = true;
        }
    }

    // Parse "crop":[X,Y,W,H]
    p = buf;
    if (find_array(p, end, "\"crop\"")) {
        int vals[4];
        if (parse_int_array(p, end, vals, 4)) {
            meta_out.crop_x      = vals[0];
            meta_out.crop_y      = vals[1];
            meta_out.crop_width  = vals[2];
            meta_out.crop_height = vals[3];
            got_crop = true;
        }
    }

    return got_size && got_crop;
}
