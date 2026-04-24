/**
 * bui_reader.h - C&C Remastered binary UI layout dependency reader.
 *
 * BUI files are CH-wrapped zlib streams. The inflated payload is still a
 * binary scene graph, but its length-prefixed strings expose scene/control
 * names, child BUI references, text IDs, texture tokens, fonts, and animation
 * channel names.
 */

#ifndef RENDER_BUI_READER_H
#define RENDER_BUI_READER_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class MegReader;

struct BUIHeader {
    uint8_t magic[2] = {0, 0};
    uint8_t major_version = 0;
    uint8_t minor_version = 0;
    uint32_t flags = 0;
    uint32_t content_hash = 0;
    uint32_t unknown_0c = 0;
    uint32_t compressed_size = 0;
};

struct BUIString {
    enum class Encoding {
        Unknown,
        SizePrefixed,
        Tagged
    };

    // First/second u32 immediately after the string bytes. These act as a
    // per-record discriminator + payload in the inflated binary:
    //   kind=0x03 value=0x33  → style/default token (e.g. "Empty")
    //   kind=0x03 value=0x06  → texture-atom token
    //   kind=0x03 value=0x00  → animation-channel atom
    //   kind=0x04             → terminal layout record (field 4 = x)
    //   kind=0x13             → group/container (value is likely child count)
    //   kind=0x01, 0x02, 0x06 → scalar/reference records
    //   kind=0x0c             → repeated-instance record (construction entries)
    // `post_name_kind_valid` is false when the string sits too close to EOF
    // for us to sample either u32.
    size_t record_offset = 0;
    size_t offset = 0;
    uint16_t declared_length = 0;
    uint32_t size_prefix = 0;
    uint16_t tag = 0;
    bool trailing_plus = false;
    Encoding encoding = Encoding::Unknown;
    bool post_name_kind_valid = false;
    uint32_t post_name_kind = 0;
    uint32_t post_name_value = 0;
    std::string text;
};

struct BUIBlock {
    size_t index = 0;
    size_t start_offset = 0;
    size_t end_offset = 0;
    std::string anchor;
    uint16_t tag = 0;
    bool tagged_anchor = false;
    std::vector<size_t> string_indices;
    std::vector<std::string> children;
    std::vector<std::string> normalized_children;
    std::vector<std::string> textures;
    std::vector<std::string> text_ids;
    std::vector<std::string> fonts;
    std::vector<std::string> animations;
    std::vector<std::string> controls;
    std::vector<std::string> styles;
    std::vector<size_t> layout_indices;
};

struct BUILayoutRecord {
    size_t string_index = 0;
    size_t record_offset = 0;
    size_t fields_offset = 0;
    std::string name;
    int32_t x = 0;
    int32_t y = 0;
    int32_t width = 0;
    int32_t height = 0;
    float pivot_x = 0.0f;
    float pivot_y = 0.0f;
};

struct BUIDocument {
    std::string entry;
    BUIHeader header;
    size_t raw_size = 0;
    size_t inflated_size = 0;
    std::vector<uint8_t> inflated_payload;
    std::vector<BUIString> strings;
    std::vector<BUIBlock> blocks;
    std::vector<BUILayoutRecord> layout_records;
    std::string scene;
    std::vector<std::string> children;
    std::vector<std::string> normalized_children;
    std::vector<std::string> textures;
    std::vector<std::string> text_ids;
    std::vector<std::string> fonts;
    std::vector<std::string> animations;
    std::vector<std::string> controls;
    std::vector<std::string> styles;
};

struct BUIReadOptions {
    size_t max_inflated_bytes = 128u * 1024u * 1024u;
    bool keep_inflated_payload = false;

    // Optional callback for callers that can resolve atlas/direct texture
    // tokens more accurately than the reader's name heuristics.
    std::function<bool(const std::string&)> texture_probe;
};

std::string BUI_Normalize_Entry_Path(const std::string& path);
std::string BUI_Upper_Ascii(const std::string& s);
bool BUI_Equal_Case_Insensitive(const std::string& a, const std::string& b);
bool BUI_Less_Case_Insensitive(const std::string& a, const std::string& b);

class BUIReader {
public:
    bool Read_Memory(const void* data, size_t size, const std::string& entry,
                     BUIDocument& out, std::string& error,
                     const BUIReadOptions& options = BUIReadOptions()) const;

    bool Read_Meg(const MegReader& meg, const std::string& entry,
                  BUIDocument& out, std::string& error,
                  const BUIReadOptions& options = BUIReadOptions()) const;
};

#endif // RENDER_BUI_READER_H
