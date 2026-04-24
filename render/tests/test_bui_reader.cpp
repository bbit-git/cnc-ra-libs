#include "test_framework.h"
#include "../bui_reader.h"

#define MINIZ_HEADER_FILE_ONLY
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "../miniz.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

static void append_u32(std::vector<uint8_t>& out, uint32_t v)
{
    out.push_back(static_cast<uint8_t>(v & 0xff));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xff));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xff));
}

static void append_u16(std::vector<uint8_t>& out, uint16_t v)
{
    out.push_back(static_cast<uint8_t>(v & 0xff));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
}

// Synthesizes a size-prefixed string record: u32 size_prefix = len+2, u16 len,
// then len bytes. Matches the encoding observed in real BUI payloads.
static void append_size_prefixed_string(std::vector<uint8_t>& out, const std::string& s)
{
    append_u32(out, static_cast<uint32_t>(s.size()) + 2u);
    append_u16(out, static_cast<uint16_t>(s.size()));
    out.insert(out.end(), s.begin(), s.end());
}

static std::vector<uint8_t> wrap_as_bui(const std::vector<uint8_t>& inflated)
{
    std::vector<uint8_t> compressed(mz_compressBound(inflated.size()));
    mz_ulong dst_len = compressed.size();
    const int rc = mz_compress(compressed.data(), &dst_len,
                               inflated.data(), inflated.size());
    compressed.resize(dst_len);
    (void)rc;

    std::vector<uint8_t> raw(0x24, 0);
    raw[0] = 'C';
    raw[1] = 'H';
    raw[2] = 2;
    raw[3] = 1;
    const uint32_t csize = static_cast<uint32_t>(compressed.size());
    raw[0x10] = static_cast<uint8_t>(csize & 0xff);
    raw[0x11] = static_cast<uint8_t>((csize >> 8) & 0xff);
    raw[0x12] = static_cast<uint8_t>((csize >> 16) & 0xff);
    raw[0x13] = static_cast<uint8_t>((csize >> 24) & 0xff);
    raw.insert(raw.end(), compressed.begin(), compressed.end());
    return raw;
}

static std::vector<uint8_t> build_inflated_with_powerbar_layout()
{
    // 0x95-byte header padding so the scene string lands at the same offset real
    // BUI payloads use. Contents don't matter — the reader scans forward from 0.
    std::vector<uint8_t> data(0x95, 0);
    append_size_prefixed_string(data, "SyntheticScene");
    append_size_prefixed_string(data, "PowerBar");

    // Terminal layout record trailer: field 4 i32 x, field 5 i32 y,
    // field 6 u16 w, field 7 u16 h, field 8 vec2 pivot.
    append_u32(data, 4u);          // field 4
    append_u32(data, 0u);          // x
    append_u32(data, 5u);          // field 5
    append_u32(data, 39u);         // y
    data.push_back(6);             // field 6
    data.push_back(2);             // scalar type u16
    append_u16(data, 41u);         // width
    data.push_back(7);             // field 7
    data.push_back(2);             // scalar type u16
    append_u16(data, 606u);        // height
    data.push_back(8);             // field 8
    data.push_back(8);             // vec2 f32
    const uint32_t half = 0x3f000000u;
    append_u32(data, half);        // pivot.x = 0.5
    append_u32(data, half);        // pivot.y = 0.5

    // Tail padding so the scene string isn't the file's last thing.
    data.resize(data.size() + 16, 0);
    return data;
}

static std::vector<uint8_t> build_inflated_with_style_default()
{
    // Scene, then a size-prefixed "Empty" whose post-name bytes match the
    // observed style/default record signature (kind=3, value=0x33).
    std::vector<uint8_t> data(0x95, 0);
    append_size_prefixed_string(data, "SyntheticStyleScene");
    append_size_prefixed_string(data, "Empty");
    // Style-default record trailer: u32 kind=3, u32 value=0x33, then opaque
    // bytes observed in real data (`04 04 02 00 00 00 05 10`).
    append_u32(data, 3u);
    append_u32(data, 0x33u);
    const uint8_t trailer[] = {0x04, 0x04, 0x02, 0x00, 0x00, 0x00, 0x05, 0x10};
    data.insert(data.end(), trailer, trailer + sizeof(trailer));
    data.resize(data.size() + 8, 0);
    return data;
}

TEST(decode_terminal_layout_record)
{
    const std::vector<uint8_t> raw = wrap_as_bui(build_inflated_with_powerbar_layout());

    BUIReader reader;
    BUIDocument doc;
    std::string error;
    EXPECT_TRUE(reader.Read_Memory(raw.data(), raw.size(),
                                   "DATA\\ART\\GUI\\SYNTHETIC.BUI",
                                   doc, error));

    EXPECT_STR_EQ(doc.scene.c_str(), "SyntheticScene");
    EXPECT_EQ(doc.layout_records.size(), static_cast<size_t>(1));

    const BUILayoutRecord& layout = doc.layout_records[0];
    EXPECT_STR_EQ(layout.name.c_str(), "PowerBar");
    EXPECT_EQ(layout.x, 0);
    EXPECT_EQ(layout.y, 39);
    EXPECT_EQ(layout.width, 41);
    EXPECT_EQ(layout.height, 606);
    EXPECT_NEAR(layout.pivot_x, 0.5f, 0.0001f);
    EXPECT_NEAR(layout.pivot_y, 0.5f, 0.0001f);
    EXPECT_EQ(doc.blocks.size(), static_cast<size_t>(1));
    EXPECT_EQ(doc.blocks[0].layout_indices.size(), static_cast<size_t>(1));

    PASS();
}

TEST(classify_style_default_record)
{
    const std::vector<uint8_t> raw = wrap_as_bui(build_inflated_with_style_default());

    BUIReader reader;
    BUIDocument doc;
    std::string error;
    EXPECT_TRUE(reader.Read_Memory(raw.data(), raw.size(),
                                   "DATA\\ART\\GUI\\SYNTHETIC.BUI",
                                   doc, error));

    EXPECT_STR_EQ(doc.scene.c_str(), "SyntheticStyleScene");
    // "Empty" with kind=3 value=0x33 must route to styles, not controls.
    EXPECT_EQ(doc.styles.size(), static_cast<size_t>(1));
    EXPECT_STR_EQ(doc.styles[0].c_str(), "Empty");
    for (const std::string& c : doc.controls) {
        EXPECT_TRUE(c != "Empty");
    }

    bool saw_kind = false;
    for (const BUIString& s : doc.strings) {
        if (s.text == "Empty") {
            EXPECT_TRUE(s.post_name_kind_valid);
            EXPECT_EQ(s.post_name_kind, 3u);
            EXPECT_EQ(s.post_name_value, 0x33u);
            saw_kind = true;
        }
    }
    EXPECT_TRUE(saw_kind);

    PASS();
}

int main()
{
    return RUN_TESTS();
}
