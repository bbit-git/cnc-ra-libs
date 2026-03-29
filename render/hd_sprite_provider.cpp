/**
 * HDSpriteProvider - ISpriteProvider for remastered MEG/TGA assets.
 *
 * Loads TGA frames from MEG archives, decodes with stb_image,
 * applies META crop offsets. Caches decoded frames for the session.
 *
 * Shape lookup flow:
 *   shape_id (entity name hash) → tileset map → TGA filename pattern
 *   TGA filename + frame → MEG lookup → TGA bytes → stb_image → RGBA
 *   Companion .META → crop/canvas offsets
 */

#include "hd_sprite_provider.h"
#include "meg_reader.h"
#include "meta_parser.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <unordered_map>
#include <string>

// stb_image: use the copy bundled with SDL3
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_TGA
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include "stb_image.h"

/// Cached decoded frame.
struct CachedFrame {
    uint8_t* pixels;       // RGBA, owned by this struct
    int      width;
    int      height;
    int      origin_x;     // Crop offset X
    int      origin_y;     // Crop offset Y
    int      canvas_width;
    int      canvas_height;
};

/// Tileset entry: maps entity name hash to filename pattern.
struct TilesetEntry {
    uint32_t    name_hash;
    std::string filename_pattern;  // e.g. "ART/TEXTURES/SRGB/TD/UNITS/HTANK"
    int         frame_count;
};

/// Simple FNV-1a hash for entity name lookup.
static uint32_t fnv1a_hash(const char* str)
{
    uint32_t h = 0x811c9dc5u;
    for (; *str; str++) {
        h ^= static_cast<uint8_t>(*str);
        h *= 0x01000193u;
    }
    return h;
}

struct HDSpriteProvider_Impl {
    MegReader meg;

    // Tileset: name_hash → tileset entry
    std::unordered_map<uint32_t, TilesetEntry> tilesets;

    // Frame cache: key = (name_hash << 16) | frame
    std::unordered_map<uint64_t, CachedFrame> cache;

    ~HDSpriteProvider_Impl() {
        for (auto& pair : cache) {
            stbi_image_free(pair.second.pixels);
        }
    }
};

HDSpriteProvider::HDSpriteProvider()
    : meg_(nullptr), atlas_(nullptr)
{
}

HDSpriteProvider::~HDSpriteProvider()
{
    // impl stored as meg_ pointer reinterpreted — clean up properly
    delete reinterpret_cast<HDSpriteProvider_Impl*>(meg_);
    meg_ = nullptr;
}

bool HDSpriteProvider::Open(const char* meg_path)
{
    auto* impl = new HDSpriteProvider_Impl;
    if (!impl->meg.Open(meg_path)) {
        delete impl;
        return false;
    }
    // Store impl in the meg_ pointer (reuse existing member)
    meg_ = reinterpret_cast<MegReader*>(impl);
    return true;
}

/// Minimal XML parser: extract <sequence name="..." filename="..." frames="..."/> entries.
static bool Parse_Tileset_XML(const char* xml, size_t len,
                               std::unordered_map<uint32_t, TilesetEntry>& out)
{
    const char* p = xml;
    const char* end = xml + len;

    while (p < end) {
        // Find next <sequence or <Sequence
        const char* tag = nullptr;
        for (const char* s = p; s < end - 9; s++) {
            if ((*s == '<') &&
                (s[1] == 's' || s[1] == 'S') &&
                (s[2] == 'e' || s[2] == 'E') &&
                (s[3] == 'q' || s[3] == 'Q')) {
                tag = s;
                break;
            }
        }
        if (!tag) break;
        p = tag + 1;

        // Find end of tag
        const char* tag_end = static_cast<const char*>(memchr(p, '>', end - p));
        if (!tag_end) break;

        // Extract attributes
        auto find_attr = [tag, tag_end](const char* attr_name) -> std::string {
            size_t alen = strlen(attr_name);
            const char* s = tag;
            while (s < tag_end - alen) {
                if (memcmp(s, attr_name, alen) == 0) {
                    s += alen;
                    // skip whitespace and =
                    while (s < tag_end && (*s == ' ' || *s == '=')) s++;
                    if (s < tag_end && *s == '"') {
                        s++;
                        const char* q = static_cast<const char*>(memchr(s, '"', tag_end - s));
                        if (q) return std::string(s, q - s);
                    }
                }
                s++;
            }
            return {};
        };

        std::string name = find_attr("name");
        std::string filename = find_attr("filename");
        std::string frames_str = find_attr("frames");

        if (!name.empty() && !filename.empty()) {
            TilesetEntry entry;
            entry.name_hash = fnv1a_hash(name.c_str());
            entry.filename_pattern = filename;
            entry.frame_count = frames_str.empty() ? 1 : atoi(frames_str.c_str());
            out[entry.name_hash] = std::move(entry);
        }

        p = tag_end + 1;
    }

    return !out.empty();
}

bool HDSpriteProvider::Load_Tileset(const char* xml_path)
{
    auto* impl = reinterpret_cast<HDSpriteProvider_Impl*>(meg_);
    if (!impl) return false;

    const MegEntry* entry = impl->meg.Find(xml_path);
    if (!entry) return false;

    size_t xml_size = 0;
    void* xml_data = impl->meg.Read_Alloc(entry, &xml_size);
    if (!xml_data) return false;

    bool ok = Parse_Tileset_XML(static_cast<const char*>(xml_data), xml_size,
                                 impl->tilesets);
    free(xml_data);
    return ok;
}

bool HDSpriteProvider::Get_Frame(const void* shape_id, int frame, SpriteFrame& frame_out)
{
    auto* impl = reinterpret_cast<HDSpriteProvider_Impl*>(meg_);
    if (!impl || !shape_id || frame < 0) return false;

    // shape_id is treated as an entity name hash (cast from uint32_t)
    uint32_t name_hash = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(shape_id));

    // Check cache
    uint64_t cache_key = (static_cast<uint64_t>(name_hash) << 16) | (frame & 0xFFFF);
    auto it = impl->cache.find(cache_key);
    if (it != impl->cache.end()) {
        const CachedFrame& cf = it->second;
        frame_out.pixels        = cf.pixels;
        frame_out.width         = cf.width;
        frame_out.height        = cf.height;
        frame_out.pitch         = cf.width * 4;
        frame_out.origin_x      = cf.origin_x;
        frame_out.origin_y      = cf.origin_y;
        frame_out.canvas_width  = cf.canvas_width;
        frame_out.canvas_height = cf.canvas_height;
        frame_out.pixel_format  = SpritePixelFormat::RGBA_32BIT;
        return true;
    }

    // Look up tileset entry
    auto ts_it = impl->tilesets.find(name_hash);
    if (ts_it == impl->tilesets.end()) return false;

    const TilesetEntry& ts = ts_it->second;
    if (frame >= ts.frame_count) return false;

    // Build TGA filename: pattern + "-" + frame + ".TGA"
    char tga_path[512];
    snprintf(tga_path, sizeof(tga_path), "%s-%d.TGA", ts.filename_pattern.c_str(), frame);

    // Load TGA from MEG
    const MegEntry* tga_entry = impl->meg.Find(tga_path);
    if (!tga_entry) return false;

    size_t tga_size = 0;
    void* tga_data = impl->meg.Read_Alloc(tga_entry, &tga_size);
    if (!tga_data) return false;

    int w = 0, h = 0, channels = 0;
    uint8_t* pixels = stbi_load_from_memory(
        static_cast<const uint8_t*>(tga_data),
        static_cast<int>(tga_size),
        &w, &h, &channels, 4 /* force RGBA */);
    free(tga_data);

    if (!pixels) return false;

    // Load companion .META
    char meta_path[512];
    snprintf(meta_path, sizeof(meta_path), "%s-%d.META", ts.filename_pattern.c_str(), frame);

    SpriteMeta meta = {};
    meta.canvas_width = w;
    meta.canvas_height = h;

    const MegEntry* meta_entry = impl->meg.Find(meta_path);
    if (meta_entry) {
        size_t meta_size = 0;
        void* meta_data = impl->meg.Read_Alloc(meta_entry, &meta_size);
        if (meta_data) {
            Meta_Parse(meta_data, meta_size, meta);
            free(meta_data);
        }
    }

    // Cache the decoded frame
    CachedFrame cf;
    cf.pixels        = pixels;
    cf.width         = w;
    cf.height        = h;
    cf.origin_x      = meta.crop_x;
    cf.origin_y      = meta.crop_y;
    cf.canvas_width  = meta.canvas_width;
    cf.canvas_height = meta.canvas_height;
    impl->cache[cache_key] = cf;

    // Fill output
    frame_out.pixels        = pixels;
    frame_out.width         = w;
    frame_out.height        = h;
    frame_out.pitch         = w * 4;
    frame_out.origin_x      = meta.crop_x;
    frame_out.origin_y      = meta.crop_y;
    frame_out.canvas_width  = meta.canvas_width;
    frame_out.canvas_height = meta.canvas_height;
    frame_out.pixel_format  = SpritePixelFormat::RGBA_32BIT;
    return true;
}

int HDSpriteProvider::Get_Frame_Count(const void* shape_id)
{
    auto* impl = reinterpret_cast<HDSpriteProvider_Impl*>(meg_);
    if (!impl || !shape_id) return 0;

    uint32_t name_hash = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(shape_id));
    auto it = impl->tilesets.find(name_hash);
    if (it == impl->tilesets.end()) return 0;
    return it->second.frame_count;
}
