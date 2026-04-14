/**
 * mtd_reader.cpp — MTD (Mega Texture Dictionary) sprite catalog parser.
 *
 * Parses the variable-length record format:
 *   [Header 12B] [Entry0] [Entry1] ... [EntryN-1]
 *
 * Each entry: padded name + 8×uint32 + 1×uint8 + next_name_size (omitted on last).
 * See docs/format-mtd.md for full spec.
 */

#include "mtd_reader.h"
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <vector>
#include <unordered_map>
#include <string>

static const uint32_t MTD_MAGIC = 0xFFFFFFFE;

static uint32_t read_u32(const uint8_t* p) {
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

/// In-place uppercase into a static buffer to avoid per-call heap allocation.
/// MTD sprite names are short (<64 chars), so a fixed buffer is safe.
static const char* to_upper(const char* s) {
    static char buf[128];
    int i = 0;
    for (; *s && i < 126; s++, i++)
        buf[i] = static_cast<char>(toupper(static_cast<unsigned char>(*s)));
    buf[i] = '\0';
    return buf;
}

struct MTDReader::Impl {
    uint8_t*            data_copy;
    size_t              data_size;
    uint32_t            bpp;
    std::vector<MTDEntry> entries;
    std::unordered_map<std::string, int> name_to_index;  // uppercased name → index
};

MTDReader::MTDReader() : impl_(nullptr) {}

MTDReader::~MTDReader() {
    Close();
}

bool MTDReader::Open(const void* data, size_t size) {
    Close();

    if (!data || size < 12)
        return false;

    const uint8_t* raw = static_cast<const uint8_t*>(data);

    // Parse header
    uint32_t magic = read_u32(raw);
    if (magic != MTD_MAGIC)
        return false;

    uint32_t entry_count = read_u32(raw + 4);
    uint32_t bpp         = read_u32(raw + 8);

    if (entry_count == 0)
        return false;

    // Make a mutable copy so name pointers remain valid
    auto* imp = new Impl();
    imp->data_copy = static_cast<uint8_t*>(malloc(size));
    if (!imp->data_copy) {
        delete imp;
        return false;
    }
    memcpy(imp->data_copy, data, size);
    imp->data_size = size;
    imp->bpp = bpp;
    imp->entries.reserve(entry_count);

    const uint8_t* ptr = imp->data_copy + 12;
    const uint8_t* end = imp->data_copy + size;

    for (uint32_t i = 0; i < entry_count; i++) {
        if (ptr >= end)
            goto fail;

        // Read null-terminated name
        const char* name = reinterpret_cast<const char*>(ptr);
        const uint8_t* name_start = ptr;

        // Scan for null terminator
        while (ptr < end && *ptr != 0)
            ptr++;
        if (ptr >= end)
            goto fail;

        // Skip null terminator
        ptr++;

        // Pad to 4-byte alignment
        size_t name_raw_len = static_cast<size_t>(ptr - name_start);
        size_t padded = (name_raw_len + 3) & ~(size_t)3;
        ptr = name_start + padded;

        // Read 8 uint32 fields + 1 uint8 flag = 33 bytes
        if (ptr + 33 > end)
            goto fail;

        MTDEntry entry;
        entry.name          = name;
        entry.atlas_x       = read_u32(ptr);      ptr += 4;
        entry.atlas_y       = read_u32(ptr);      ptr += 4;
        entry.width         = read_u32(ptr);      ptr += 4;
        entry.height        = read_u32(ptr);      ptr += 4;
        entry.origin_x      = read_u32(ptr);      ptr += 4;
        entry.origin_y      = read_u32(ptr);      ptr += 4;
        entry.canvas_width  = read_u32(ptr);      ptr += 4;
        entry.canvas_height = read_u32(ptr);      ptr += 4;
        entry.flag           = *ptr;               ptr += 1;

        // Read next_name_size (skip it — we scan for null instead)
        if (i < entry_count - 1) {
            if (ptr + 4 > end)
                goto fail;
            ptr += 4;  // skip next_name_size
        }

        int idx = static_cast<int>(imp->entries.size());
        imp->entries.push_back(entry);
        imp->name_to_index[to_upper(entry.name)] = idx;
    }

    impl_ = imp;
    return true;

fail:
    free(imp->data_copy);
    delete imp;
    return false;
}

void MTDReader::Close() {
    if (impl_) {
        free(impl_->data_copy);
        delete impl_;
        impl_ = nullptr;
    }
}

int MTDReader::Count() const {
    return impl_ ? static_cast<int>(impl_->entries.size()) : 0;
}

int MTDReader::BPP() const {
    return impl_ ? static_cast<int>(impl_->bpp) : 0;
}

const MTDEntry* MTDReader::Get_Entry(int index) const {
    if (!impl_ || index < 0 || index >= static_cast<int>(impl_->entries.size()))
        return nullptr;
    return &impl_->entries[index];
}

int MTDReader::Find_Index(const char* name) const {
    if (!impl_ || !name)
        return -1;
    auto it = impl_->name_to_index.find(to_upper(name));
    if (it == impl_->name_to_index.end())
        return -1;
    return it->second;
}

const MTDEntry* MTDReader::Find(const char* name) const {
    int idx = Find_Index(name);
    if (idx < 0) return nullptr;
    return &impl_->entries[idx];
}
