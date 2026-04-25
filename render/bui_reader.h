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

// Decoded `kind=0x0c` repeated-instance record. Each instance places one copy
// of `child_entry` (a child BUI reference like
// `Art\GUI\UI_SideBar_ConstructionEntry.bui`) at a normalized {x, y, w, h}
// rectangle in its parent's coordinate space. The four floats are encoded
// directly after a fixed `0d 10` marker that follows the kind/value u64; the
// instance is then anchored by an embedded `kind=0x13 val=0x10` sub-header
// (validated by the decoder, not surfaced).
//
// On `TACTICAL_UI.BUI` this surfaces all 10 construction-entry slot positions
// — the cleanest numeric oracle the recon dump (B1) found. See
// `docs/tasks/bui-non-terminal-decoder.md`.
struct BUIInstance {
    size_t string_index = 0;
    size_t record_offset = 0;
    size_t fields_offset = 0;
    std::string child_entry;
    // Sibling slot label observed immediately before the instance record.
    // Real BUIs emit a `kind=0x0b val=0x2a` size-prefixed string right ahead
    // of every `kind=0x0c` instance whose value is the placement slot name
    // (e.g. `Column1_0`, `UnitButton_3`). Optional — left empty if none was
    // found within the conservative search window.
    std::string slot_name;
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;

    // Resolved parent: the closest terminal layout record (BUILayoutRecord)
    // whose `record_offset < instance.record_offset`. The instance's xywh is
    // normalized to that record's pixel bounding box. `parent_layout_index`
    // is `(size_t)-1` if no preceding layout record exists in the document.
    size_t parent_layout_index = static_cast<size_t>(-1);
    std::string parent_name;
    int32_t pixel_x = 0;
    int32_t pixel_y = 0;
    int32_t pixel_width = 0;
    int32_t pixel_height = 0;
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

// Decoded prefix of a `kind=0x13` named control record. Every kind=0x13 on
// the BUIs we've inspected starts with a fixed 32-byte header
// (vec3-zeros + f32 1.0 + a few u32 magic words), followed by tag/type/value
// triples. The first three triples are stable enough to decode:
//
//   tag 0x01, type 0x04 (u32): unique control ID (probably FNV hash of name)
//   tag 0x02, type 0x10 (vec4 f32):
//       leaves   — normalized (x, y, w, h) bbox in parent space
//       grid containers — (0, 0, child_step_x, child_step_y)
//   tag 0x03, type 0x10 (vec4 f32): scale/tint, always (1,1,1,1) so far
//
// `tag2_kind` tells callers which interpretation to apply. `Bbox` for leaves
// (Cost_Text → (0.0254, 0.0568, 0.2203, 0.2841)), `GridStep` for containers
// whose first two components are zero (Units_Group → (0, 0, 0.5, 0.1429)).
//
// The "container's outer extent" is not encoded here; that lives in some
// record kind we haven't isolated. See
// `docs/tasks/bui-non-terminal-decoder.md`.
struct BUIControlHeader {
    enum class Tag2Kind {
        Unknown,
        Bbox,
        GridStep
    };

    size_t string_index = 0;
    size_t header_offset = 0;
    uint32_t uid = 0;
    float tag2[4] = {0, 0, 0, 0};
    float tag3[4] = {0, 0, 0, 0};
    Tag2Kind tag2_kind = Tag2Kind::Unknown;
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
    std::vector<BUIInstance> instances;
    std::vector<BUIControlHeader> control_headers;
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
