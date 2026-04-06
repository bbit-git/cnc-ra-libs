#include "hd_terrain_provider.h"
#include "meg_reader.h"
#include "dds_reader.h"
#include "meta_parser.h"

#include <unordered_map>
#include <string>
#include <list>
#include <cstring>
#include <ctype.h>
#include <cstdlib>

static uint32_t fnv1a_hash(const char* str) {
    uint32_t hash = 0x811C9DC5;
    while (*str) {
        hash ^= (unsigned char)toupper((unsigned char)*str++);
        hash *= 0x01000193;
    }
    return hash;
}

struct TerrainInfo {
    uint32_t name_hash;
    std::string name;
    std::string filename_pattern;
    int frame_count;
};

struct CachedTerrain {
    SpriteFrame frame;
    size_t size_bytes;
    std::list<uint64_t>::iterator lru_it;
};

struct HDTerrainProvider_Impl {
    MegReader meg;
    std::unordered_map<uint32_t, TerrainInfo> tiles;
    std::unordered_map<uint64_t, CachedTerrain> cache;
    std::list<uint64_t> lru_list;

    size_t current_cache_bytes = 0;
    const size_t MAX_CACHE_BYTES = 64 * 1024 * 1024; // 64 MB roughly ~1000 tiles
};

HDTerrainProvider::HDTerrainProvider() : impl_(new HDTerrainProvider_Impl()) {}
HDTerrainProvider::~HDTerrainProvider() {
    if (impl_) {
        for (auto& pair : impl_->cache) {
            free(const_cast<void*>(pair.second.frame.pixels));
        }
        delete impl_;
    }
}

bool HDTerrainProvider::Open(const char* meg_path) {
    return impl_->meg.Open(meg_path);
}

void HDTerrainProvider::Set_Theater(const char* theater) {
    if (!theater || theater[0] == '\0') return;

    impl_->tiles.clear();
    for (auto& pair : impl_->cache) {
        free(const_cast<void*>(pair.second.frame.pixels));
    }
    impl_->cache.clear();
    impl_->lru_list.clear();
    impl_->current_cache_bytes = 0;

    std::string th_name = theater;
    for (char& c : th_name) c = (char)toupper(c);

    std::string prefix = "DATA\\ART\\TEXTURES\\SRGB\\TIBERIAN_DAWN\\TERRAIN\\" + th_name + "\\";

    int count = impl_->meg.Entry_Count();
    for (int i = 0; i < count; ++i) {
        const MegEntry* entry = impl_->meg.Get_Entry(i);
        if (!entry) continue;
        const char* fname = impl_->meg.Get_Filename(entry->name_index);
        if (!fname) continue;

        std::string fname_s = fname;
        for (char& c : fname_s) { if (c == '/') c = '\\'; c = (char)toupper(c); }

        if (fname_s.find(prefix) == 0) {
            size_t sub_start = prefix.length();
            size_t slash = fname_s.find('\\', sub_start);
            if (slash == std::string::npos) continue;

            std::string folder = fname_s.substr(sub_start, slash - sub_start);
            size_t dot = folder.find('.');
            std::string name = (dot != std::string::npos) ? folder.substr(0, dot) : folder;
            uint32_t name_hash = fnv1a_hash(name.c_str());

            size_t dash = fname_s.find('-', slash);
            size_t dot_dds = fname_s.find(".DDS", slash);
            int frame = 0;
            if (dash != std::string::npos && dot_dds != std::string::npos && dash < dot_dds) {
                frame = atoi(fname_s.substr(dash + 1, dot_dds - dash - 1).c_str());
            }

            auto& ts = impl_->tiles[name_hash];
            if (ts.name.empty()) {
                ts.name_hash = name_hash;
                ts.name = name;
                ts.filename_pattern = fname_s.substr(0, dash);
                ts.frame_count = 0;
            }
            if (frame >= ts.frame_count) {
                ts.frame_count = frame + 1;
            }
        }
    }
}

bool HDTerrainProvider::Get_Tile(const char* name, int frame, SpriteFrame& out) {
    if (!name || frame < 0) return false;
    uint32_t name_hash = fnv1a_hash(name);
    uint64_t cache_key = ((uint64_t)name_hash << 32) | (uint32_t)frame;

    auto cache_it = impl_->cache.find(cache_key);
    if (cache_it != impl_->cache.end()) {
        impl_->lru_list.erase(cache_it->second.lru_it);
        impl_->lru_list.push_front(cache_key);
        cache_it->second.lru_it = impl_->lru_list.begin();
        out = cache_it->second.frame;
        return true;
    }

    auto tile_it = impl_->tiles.find(name_hash);
    if (tile_it == impl_->tiles.end()) return false;
    auto& ts = tile_it->second;
    if (frame >= ts.frame_count) return false;

    char dds_path[512];
    snprintf(dds_path, sizeof(dds_path), "%s-%04d.DDS", ts.filename_pattern.c_str(), frame);

    const MegEntry* dds_entry = impl_->meg.Find(dds_path);
    if (!dds_entry) return false;

    size_t dds_size = 0;
    void* dds_data = impl_->meg.Read_Alloc(dds_entry, &dds_size);
    if (!dds_data) return false;

    int w = 0, h = 0;
    uint8_t* pixels = DDS_Decode_RGBA(dds_data, dds_size, w, h);
    free(dds_data);
    if (!pixels) return false;

    SpriteMeta meta = {};
    meta.canvas_width = w;
    meta.canvas_height = h;

    char meta_path[512];
    snprintf(meta_path, sizeof(meta_path), "%s-%04d.META", ts.filename_pattern.c_str(), frame);
    const MegEntry* meta_entry = impl_->meg.Find(meta_path);
    if (meta_entry) {
        size_t meta_size = 0;
        void* meta_data = impl_->meg.Read_Alloc(meta_entry, &meta_size);
        if (meta_data) {
            Meta_Parse(meta_data, meta_size, meta);
            free(meta_data);
        }
    }

    out.pixels = pixels;
    out.width = w;
    out.height = h;
    out.origin_x = meta.crop_x;
    out.origin_y = meta.crop_y;
    out.canvas_width = meta.canvas_width;
    out.canvas_height = meta.canvas_height;
    out.pitch = w * 4;
    out.pixel_format = SpritePixelFormat::RGBA_32BIT;

    size_t alloc_size = w * h * 4;
    // Keep decoded tiles hot in memory until the LRU trims them.
    impl_->lru_list.push_front(cache_key);
    impl_->cache[cache_key] = { out, alloc_size, impl_->lru_list.begin() };
    impl_->current_cache_bytes += alloc_size;

    while (impl_->current_cache_bytes > impl_->MAX_CACHE_BYTES && !impl_->lru_list.empty()) {
        uint64_t evict_key = impl_->lru_list.back();
        impl_->lru_list.pop_back();
        auto evict_it = impl_->cache.find(evict_key);
        if (evict_it != impl_->cache.end()) {
            impl_->current_cache_bytes -= evict_it->second.size_bytes;
            free(const_cast<void*>(evict_it->second.frame.pixels));
            impl_->cache.erase(evict_it);
        }
    }

    return true;
}
