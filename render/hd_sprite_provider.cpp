/**
 * HDSpriteProvider - ISpriteProvider for remastered MEG/DDS assets.
 *
 * Loads DDS frames from MEG archives, decodes with DDS_Decode_RGBA,
 * applies META crop offsets. Caches decoded frames for the session.
 *
 * Shape lookup flow:
 *   shape_id (entity name hash) → tileset map → ZIP filename pattern
 *   ZIP filename + frame → MEG lookup → ZIP bytes → ZipReader → DDS bytes → DDS_Decode_RGBA → RGBA
 *   Companion .META → crop/canvas offsets
 */

#include "hd_sprite_provider.h"
#include "dds_reader.h"
#include "meg_reader.h"
#include "meta_parser.h"
#include "zip_reader.h"
#include <memory>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cctype>

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
    float    native_scale;
};

/// Tileset entry: maps entity name hash to filename pattern.
struct TilesetEntry {
    uint32_t    name_hash;
    std::string name;              // e.g. "HTANK"
    std::string filename_pattern;  // e.g. "ART/TEXTURES/SRGB/TD/UNITS/HTANK"
    int         frame_count;
    std::string category;          // Derived from XML (e.g. "UNITS", "INFANTRY")
};

struct ZipCache {
    void*     zip_data;
    size_t    zip_size;
    ZipReader reader;

    ZipCache() : zip_data(nullptr), zip_size(0) {}
    ~ZipCache() { if (zip_data) free(zip_data); }
};

/// Simple FNV-1a hash for entity name lookup.
static uint32_t fnv1a_hash(const char* str)
{
    uint32_t h = 0x811c9dc5u;
    for (; *str; str++) {
        h ^= static_cast<uint8_t>(toupper(static_cast<unsigned char>(*str)));
        h *= 0x01000193u;
    }
    return h;
}

struct HDSpriteProvider_Impl {
    MegReader meg;

    // Tileset: name_hash → tileset entry
    std::unordered_map<uint32_t, TilesetEntry> tilesets;

    // Frame cache: key = entity name + frame number
    std::unordered_map<std::string, CachedFrame> cache;

    // Zip cache per entity name
    std::unordered_map<std::string, std::unique_ptr<ZipCache>> zip_cache;

    ~HDSpriteProvider_Impl() {
        for (auto& pair : cache) {
            free(pair.second.pixels);
        }
    }
};

HDSpriteProvider::HDSpriteProvider()
    : impl_(nullptr)
{
}

HDSpriteProvider::~HDSpriteProvider()
{
    delete impl_;
    impl_ = nullptr;
}

bool HDSpriteProvider::Open(const char* meg_path)
{
    auto* impl = new HDSpriteProvider_Impl;
    if (!impl->meg.Open(meg_path)) {
        delete impl;
        return false;
    }
    impl_ = impl;
    return true;
}

/// Minimal XML parser for <Tile><Key><Name>... and <Value><Frames><Frame>...
static bool Parse_Tileset_XML(const char* xml, size_t len,
                               std::unordered_map<uint32_t, TilesetEntry>& out)
{
    std::unordered_map<uint32_t, int> current_pass_frames;
    std::string s(xml, len);
    size_t pos = 0;
    while ((pos = s.find("<Tile>", pos)) != std::string::npos) {
        size_t end_pos = s.find("</Tile>", pos);
        if (end_pos == std::string::npos) break;

        std::string tile = s.substr(pos, end_pos - pos);
        
        size_t name_start = tile.find("<Name>");
        if (name_start != std::string::npos) {
            name_start += 6;
            size_t name_end = tile.find("</Name>", name_start);
            if (name_end != std::string::npos) {
                std::string name = tile.substr(name_start, name_end - name_start);
                
                int frames = 0;
                size_t fpos = tile.find("<Frames>");
                if (fpos != std::string::npos) {
                    size_t fend = tile.find("</Frames>", fpos);
                    while ((fpos = tile.find("<Frame>", fpos)) != std::string::npos && fpos < fend) {
                        frames++;
                        fpos += 7;
                    }
                }
                
                uint32_t name_hash = fnv1a_hash(name.c_str());
                current_pass_frames[name_hash] += frames;
                
                auto& entry = out[name_hash];
                if (entry.name.empty()) {
                    entry.name_hash = name_hash;
                    entry.name = name;
                    entry.filename_pattern = name;
                }
            }
        }
        pos = end_pos + 7;
    }

    for (const auto& pair : current_pass_frames) {
        out[pair.first].frame_count = pair.second;
    }

    return !current_pass_frames.empty();
}

static const char* extract_category(const char* xml_path) {
    const char* start = strrchr(xml_path, '\\');
    if (!start) start = strrchr(xml_path, '/');
    start = start ? start + 1 : xml_path;
    
    // Check for known categories based on filename substrings
    std::string name(start);
    for (char& c : name) c = (char)toupper(c);
    
    if (name.find("INFANTRY") != std::string::npos) return "INFANTRY";
    if (name.find("STRUCTURE") != std::string::npos) return "STRUCTURES";
    if (name.find("UNIT") != std::string::npos) return "UNITS";
    return nullptr; // Unrecognised
}

static bool Parse_Tileset_From_Reader(HDSpriteProvider_Impl* impl, MegReader& meg, const char* xml_path) {
    const MegEntry* entry = meg.Find(xml_path);
    if (!entry) return false;

    size_t xml_size = 0;
    void* xml_data = meg.Read_Alloc(entry, &xml_size);
    if (!xml_data) return false;

    const char* cat = extract_category(xml_path);
    if (!cat) {
        free(xml_data);
        return false;
    }

    bool ok = Parse_Tileset_XML(static_cast<const char*>(xml_data), xml_size,
                                 impl->tilesets);
    if (ok) {
        for (auto& pair : impl->tilesets) {
            if (pair.second.category.empty()) {
                pair.second.category = cat;
            }
        }
    }
    
    free(xml_data);
    return ok;
}

bool HDSpriteProvider::Load_Tileset(const char* xml_path)
{
    if (!impl_) return false;
    return Parse_Tileset_From_Reader(impl_, impl_->meg, xml_path);
}

bool HDSpriteProvider::Load_Tileset_From_Meg(const char* meg_path, const char* xml_path)
{
    if (!impl_) return false;

    MegReader temp_meg;
    if (!temp_meg.Open(meg_path)) return false;

    return Parse_Tileset_From_Reader(impl_, temp_meg, xml_path);
}

static std::string zip_path_for_entity(const std::string& entity, const std::string& category) {
    return "DATA\\ART\\TEXTURES\\SRGB\\TIBERIAN_DAWN\\" + category + "\\" + entity + ".ZIP";
}

static bool load_zip_for_entity(HDSpriteProvider_Impl* impl, const std::string& entity, const std::string& category) {
    if (category.empty()) return false;

    auto it = impl->zip_cache.find(entity);
    if (it != impl->zip_cache.end()) {
        return it->second != nullptr;
    }

    impl->zip_cache[entity] = nullptr;

    std::string path = zip_path_for_entity(entity, category);
    const MegEntry* entry = impl->meg.Find(path.c_str());
    if (entry) {
        auto zc = std::make_unique<ZipCache>();
        zc->zip_data = impl->meg.Read_Alloc(entry, &zc->zip_size);
        if (zc->zip_data) {
            if (zc->reader.Open(zc->zip_data, zc->zip_size)) {
                impl->zip_cache[entity] = std::move(zc);
                return true;
            }
        }
    }
    
    // Clear the null sentinel on failure so we can retry later if the category was corrected
    impl->zip_cache.erase(entity);
    return false;
}

static bool load_frame_from_zip(ZipCache& zc, const char* entity_name,
                                int frame, CachedFrame& out)
{
    std::string lower_entity = entity_name;
    for (char& c : lower_entity) c = (char)tolower(c);

    char dds_name[256];
    snprintf(dds_name, sizeof(dds_name), "%s-%04d.dds", lower_entity.c_str(), frame);

    int w = 0, h = 0;
    uint8_t* pixels = nullptr;
    size_t dds_size = 0;
    void* dds_data = zc.reader.Extract(dds_name, &dds_size);
    if (dds_data) {
        pixels = DDS_Decode_RGBA(dds_data, dds_size, w, h);
        free(dds_data);
    }

    if (!pixels) {
        char tga_name[256];
        snprintf(tga_name, sizeof(tga_name), "%s-%04d.tga", lower_entity.c_str(), frame);
        size_t tga_size = 0;
        void* tga_data = zc.reader.Extract(tga_name, &tga_size);
        if (!tga_data) return false;

        int channels = 0;
        pixels = stbi_load_from_memory(
            static_cast<const uint8_t*>(tga_data),
            static_cast<int>(tga_size),
            &w, &h, &channels, 4);
        free(tga_data);
        if (!pixels) return false;
    }

    char meta_name[256];
    snprintf(meta_name, sizeof(meta_name), "%s-%04d.meta", lower_entity.c_str(), frame);
    SpriteMeta meta = {};
    meta.canvas_width = w;
    meta.canvas_height = h;
    size_t meta_size = 0;
    void* meta_data = zc.reader.Extract(meta_name, &meta_size);
    if (meta_data) {
        Meta_Parse(meta_data, meta_size, meta);
        free(meta_data);
    }

    out.pixels = pixels;
    out.width = w;
    out.height = h;
    out.origin_x = meta.crop_x;
    out.origin_y = meta.crop_y;
    out.canvas_width = meta.canvas_width;
    out.canvas_height = meta.canvas_height;
    out.native_scale = 4.0f;
    return true;
}

bool HDSpriteProvider::Get_Frame(const void* shape_id, int frame, SpriteFrame& frame_out)
{
    auto* impl = impl_;
    if (!impl || !shape_id || frame < 0) return false;

    // shape_id is treated as an entity name hash (cast from uint32_t)
    uint32_t name_hash = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(shape_id));

    auto ts_it = impl->tilesets.find(name_hash);
    if (ts_it == impl->tilesets.end()) return false;

    TilesetEntry& ts = ts_it->second;
    std::string cache_key = ts.name + ":" + std::to_string(frame);
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
        frame_out.native_scale  = cf.native_scale > 0.0f ? cf.native_scale : 4.0f;
        frame_out.pixel_format  = SpritePixelFormat::RGBA_32BIT;
        return true;
    }

    bool loaded = false;
    CachedFrame cf;

    // 1. Try ZIP path first, with DDS inside the archive.
    if (load_zip_for_entity(impl, ts.name, ts.category)) {
        if (impl->zip_cache[ts.name]) {
            ZipCache& zc = *(impl->zip_cache[ts.name]);
            if (frame < ts.frame_count) {
                loaded = load_frame_from_zip(zc, ts.name.c_str(), frame, cf);
            }
        }
    }

    // Still out of bounds fallback check
    if (!loaded && frame >= ts.frame_count) return false;
    if (!loaded) {
        char dds_path[512];
        snprintf(dds_path, sizeof(dds_path), "%s-%04d.DDS", ts.filename_pattern.c_str(), frame);

        const MegEntry* dds_entry = impl->meg.Find(dds_path);
        if (dds_entry) {
            size_t dds_size = 0;
            void* dds_data = impl->meg.Read_Alloc(dds_entry, &dds_size);
            if (dds_data) {
                int w = 0, h = 0;
                uint8_t* pixels = DDS_Decode_RGBA(dds_data, dds_size, w, h);
                free(dds_data);

                if (pixels) {
                    char meta_path[512];
                    snprintf(meta_path, sizeof(meta_path), "%s-%04d.META", ts.filename_pattern.c_str(), frame);
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

                    cf.pixels = pixels;
                    cf.width = w;
                    cf.height = h;
                    cf.origin_x = meta.crop_x;
                    cf.origin_y = meta.crop_y;
                    cf.canvas_width = meta.canvas_width;
                    cf.canvas_height = meta.canvas_height;
                    cf.native_scale = 4.0f;
                    loaded = true;
                }
            }
        }
    }

    if (!loaded) return false;

    // Cache the decoded frame
    impl->cache[cache_key] = cf;

    // Fill output
    frame_out.pixels        = cf.pixels;
    frame_out.width         = cf.width;
    frame_out.height        = cf.height;
    frame_out.pitch         = cf.width * 4;
    frame_out.origin_x      = cf.origin_x;
    frame_out.origin_y      = cf.origin_y;
    frame_out.canvas_width  = cf.canvas_width;
    frame_out.canvas_height = cf.canvas_height;
    frame_out.native_scale  = cf.native_scale > 0.0f ? cf.native_scale : 4.0f;
    frame_out.pixel_format  = SpritePixelFormat::RGBA_32BIT;
    return true;
}

int HDSpriteProvider::Get_Frame_Count(const void* shape_id)
{
    auto* impl = impl_;
    if (!impl || !shape_id) return 0;

    uint32_t name_hash = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(shape_id));
    auto it = impl->tilesets.find(name_hash);
    if (it == impl->tilesets.end()) return 0;
    
    // Attempt to load ZIP first to cache it for later Get_Frame calls.
    load_zip_for_entity(impl, it->second.name, it->second.category);
    
    return it->second.frame_count;
}
