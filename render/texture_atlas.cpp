/**
 * TextureAtlas - Bin-packs sprite frames into power-of-2 atlas pages.
 *
 * Uses a shelf-based packing algorithm (simple, fast, good enough for
 * sprite atlases where frames have similar heights).
 *
 * Flow: Init() → Add_Frame() * N → Finalize() → Get_Region() / Page_Pixels()
 */

#include "texture_atlas.h"
#include <cstdlib>
#include <cstring>
#include <vector>

/// A horizontal shelf within an atlas page.
struct Shelf {
    int y;        // Top Y of this shelf in the page
    int height;   // Shelf height (tallest frame placed on it)
    int cursor_x; // Next free X position
};

/// An atlas page: page_size x page_size RGBA pixels.
struct AtlasPage {
    uint8_t*            pixels;
    int                 page_size;
    std::vector<Shelf>  shelves;

    AtlasPage(int size) : page_size(size) {
        pixels = static_cast<uint8_t*>(calloc(size * size, 4));
        // Start with one empty shelf at top
        shelves.push_back({0, 0, 0});
    }

    ~AtlasPage() {
        free(pixels);
    }

    /// Try to place a frame. Returns true and fills x_out/y_out on success.
    bool Try_Place(int w, int h, int& x_out, int& y_out) {
        // 1-pixel padding between frames
        int pw = w + 1;
        int ph = h + 1;

        // Try existing shelves (best-fit: smallest shelf that fits height)
        int best_shelf = -1;
        int best_waste = page_size;
        for (int i = 0; i < static_cast<int>(shelves.size()); i++) {
            Shelf& s = shelves[i];
            int shelf_h = s.height ? s.height : ph; // empty shelf takes frame height
            if (s.cursor_x + pw <= page_size && shelf_h >= ph) {
                int waste = shelf_h - ph;
                if (waste < best_waste) {
                    best_waste = waste;
                    best_shelf = i;
                }
            }
        }

        if (best_shelf >= 0) {
            Shelf& s = shelves[best_shelf];
            if (s.height == 0) s.height = ph; // first frame sets shelf height
            x_out = s.cursor_x;
            y_out = s.y;
            s.cursor_x += pw;
            return true;
        }

        // No existing shelf fits — start a new shelf
        int next_y = 0;
        if (!shelves.empty()) {
            const Shelf& last = shelves.back();
            next_y = last.y + last.height;
        }
        if (next_y + ph > page_size) return false; // page full

        shelves.push_back({next_y, ph, pw});
        x_out = 0;
        y_out = next_y;
        return true;
    }

    /// Blit RGBA pixels into the page at (dx, dy).
    void Blit(const void* src, int w, int h, int dx, int dy) {
        const uint8_t* sp = static_cast<const uint8_t*>(src);
        int dst_pitch = page_size * 4;
        for (int row = 0; row < h; row++) {
            memcpy(pixels + (dy + row) * dst_pitch + dx * 4,
                   sp + row * w * 4,
                   w * 4);
        }
    }
};

struct FrameRecord {
    uint16_t atlas_id;
    uint16_t x, y, w, h;
    int16_t  origin_x;
    int16_t  origin_y;
    uint16_t canvas_w;
    uint16_t canvas_h;
    float    native_scale;
};

struct TextureAtlas::Impl {
    int                       page_size = 2048;
    std::vector<AtlasPage*>   pages;
    std::vector<FrameRecord>  frames;
    bool                      finalized = false;

    ~Impl() {
        for (auto* p : pages) delete p;
    }
};

TextureAtlas::TextureAtlas()
    : impl_(new Impl)
{
}

TextureAtlas::~TextureAtlas()
{
    delete impl_;
}

void TextureAtlas::Init(int page_size)
{
    impl_->page_size = page_size;
}

AtlasFrameID TextureAtlas::Add_Frame(const void* pixels, int width, int height,
                                     int origin_x, int origin_y,
                                     int canvas_w, int canvas_h,
                                     float native_scale)
{
    if (impl_->finalized) return static_cast<uint32_t>(-1);
    if (!pixels || width <= 0 || height <= 0) return static_cast<uint32_t>(-1);
    if (width > impl_->page_size || height > impl_->page_size) return static_cast<uint32_t>(-1);

    // Try to place in an existing page
    int px = 0, py = 0;
    int page_idx = -1;

    for (int i = 0; i < static_cast<int>(impl_->pages.size()); i++) {
        if (impl_->pages[i]->Try_Place(width, height, px, py)) {
            page_idx = i;
            break;
        }
    }

    // Allocate new page if needed
    if (page_idx < 0) {
        auto* page = new AtlasPage(impl_->page_size);
        impl_->pages.push_back(page);
        page_idx = static_cast<int>(impl_->pages.size()) - 1;
        if (!page->Try_Place(width, height, px, py)) {
            return static_cast<uint32_t>(-1); // frame too large
        }
    }

    // Blit pixels
    impl_->pages[page_idx]->Blit(pixels, width, height, px, py);

    // Record
    FrameRecord rec;
    rec.atlas_id = static_cast<uint16_t>(page_idx);
    rec.x = static_cast<uint16_t>(px);
    rec.y = static_cast<uint16_t>(py);
    rec.w = static_cast<uint16_t>(width);
    rec.h = static_cast<uint16_t>(height);
    rec.origin_x = static_cast<int16_t>(origin_x);
    rec.origin_y = static_cast<int16_t>(origin_y);
    rec.canvas_w = static_cast<uint16_t>(canvas_w > 0 ? canvas_w : width);
    rec.canvas_h = static_cast<uint16_t>(canvas_h > 0 ? canvas_h : height);
    rec.native_scale = native_scale > 0.0f ? native_scale : 1.0f;
    impl_->frames.push_back(rec);

    return static_cast<AtlasFrameID>(impl_->frames.size() - 1);
}

void TextureAtlas::Finalize()
{
    impl_->finalized = true;
}

bool TextureAtlas::Get_Region(AtlasFrameID id, AtlasRegion& region_out) const
{
    if (id >= impl_->frames.size()) return false;

    const FrameRecord& rec = impl_->frames[id];
    float ps = static_cast<float>(impl_->page_size);

    region_out.atlas_id = rec.atlas_id;
    region_out.x = rec.x;
    region_out.y = rec.y;
    region_out.w = rec.w;
    region_out.h = rec.h;
    region_out.origin_x = rec.origin_x;
    region_out.origin_y = rec.origin_y;
    region_out.canvas_w = rec.canvas_w;
    region_out.canvas_h = rec.canvas_h;
    region_out.native_scale = rec.native_scale;
    region_out.u0 = rec.x / ps;
    region_out.v0 = rec.y / ps;
    region_out.u1 = (rec.x + rec.w) / ps;
    region_out.v1 = (rec.y + rec.h) / ps;
    return true;
}

int TextureAtlas::Page_Count() const
{
    return static_cast<int>(impl_->pages.size());
}

const void* TextureAtlas::Page_Pixels(int page_index) const
{
    if (page_index < 0 || page_index >= static_cast<int>(impl_->pages.size()))
        return nullptr;
    return impl_->pages[page_index]->pixels;
}

int TextureAtlas::Page_Size() const
{
    return impl_->page_size;
}
