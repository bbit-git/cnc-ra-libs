#define MINIZ_HEADER_FILE_ONLY
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "miniz.h"

#include "bui_reader.h"
#include "meg_reader.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <unordered_set>

static constexpr size_t BUI_HEADER_SIZE = 0x24;

struct OrderedBUIStrings {
    std::vector<std::string> values;
    std::unordered_set<std::string> seen;

    bool Add(const std::string& value)
    {
        const std::string key = BUI_Upper_Ascii(value);
        if (seen.find(key) != seen.end()) return false;
        seen.insert(key);
        values.push_back(value);
        return true;
    }
};

std::string BUI_Upper_Ascii(const std::string& s)
{
    std::string out = s;
    for (char& c : out) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return out;
}

static std::string trim_ascii(const std::string& s)
{
    size_t begin = 0;
    while (begin < s.size() && std::isspace(static_cast<unsigned char>(s[begin]))) {
        ++begin;
    }
    size_t end = s.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    return s.substr(begin, end - begin);
}

static bool starts_with_ci(const std::string& s, const char* prefix)
{
    const size_t n = std::strlen(prefix);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; ++i) {
        if (std::toupper(static_cast<unsigned char>(s[i])) !=
            std::toupper(static_cast<unsigned char>(prefix[i]))) {
            return false;
        }
    }
    return true;
}

static bool ends_with_ci(const std::string& s, const char* suffix)
{
    const size_t n = std::strlen(suffix);
    if (s.size() < n) return false;
    const size_t off = s.size() - n;
    for (size_t i = 0; i < n; ++i) {
        if (std::toupper(static_cast<unsigned char>(s[off + i])) !=
            std::toupper(static_cast<unsigned char>(suffix[i]))) {
            return false;
        }
    }
    return true;
}

static bool contains_ci(const std::string& s, const char* needle)
{
    const std::string a = BUI_Upper_Ascii(s);
    const std::string b = BUI_Upper_Ascii(needle);
    return a.find(b) != std::string::npos;
}

bool BUI_Equal_Case_Insensitive(const std::string& a, const std::string& b)
{
    return BUI_Upper_Ascii(a) == BUI_Upper_Ascii(b);
}

bool BUI_Less_Case_Insensitive(const std::string& a, const std::string& b)
{
    const std::string au = BUI_Upper_Ascii(a);
    const std::string bu = BUI_Upper_Ascii(b);
    if (au == bu) return a < b;
    return au < bu;
}

static uint32_t read_u32_le(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0])
        | (static_cast<uint32_t>(p[1]) << 8)
        | (static_cast<uint32_t>(p[2]) << 16)
        | (static_cast<uint32_t>(p[3]) << 24);
}

static uint16_t read_u16_le(const uint8_t* p)
{
    return static_cast<uint16_t>(p[0])
        | static_cast<uint16_t>(p[1] << 8);
}

static int32_t read_i32_le(const uint8_t* p)
{
    return static_cast<int32_t>(read_u32_le(p));
}

static float read_f32_le(const uint8_t* p)
{
    const uint32_t bits = read_u32_le(p);
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

static bool is_printable_ascii(uint8_t c)
{
    return c >= 0x20 && c <= 0x7e;
}

static bool has_alpha(const std::string& s)
{
    for (unsigned char c : s) {
        if (std::isalpha(c)) return true;
    }
    return false;
}

static bool is_all_upper_word(const std::string& s)
{
    bool saw_alpha = false;
    for (unsigned char c : s) {
        if (std::isalpha(c)) {
            saw_alpha = true;
            if (std::islower(c)) return false;
        }
    }
    return saw_alpha;
}

static bool is_number_like(const std::string& s)
{
    if (s.empty()) return false;
    bool saw_digit = false;
    for (size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (std::isdigit(c)) {
            saw_digit = true;
            continue;
        }
        if ((c == '-' || c == '+') && i == 0) continue;
        if (c == '.') continue;
        return false;
    }
    return saw_digit;
}

static bool is_identifierish(const std::string& s)
{
    if (s.empty() || s.size() > 160 || !has_alpha(s)) return false;
    for (unsigned char c : s) {
        if (std::isalnum(c)) continue;
        switch (c) {
            case '_':
            case '-':
            case '.':
            case '\\':
            case '/':
            case ':':
            case '&':
                continue;
            default:
                return false;
        }
    }
    return true;
}

static bool is_child_bui_ref(const std::string& s)
{
    return ends_with_ci(s, ".BUI")
        && (contains_ci(s, "ART\\GUI\\")
            || contains_ci(s, "ART/GUI/")
            || contains_ci(s, "GUI\\")
            || contains_ci(s, "GUI/"));
}

static bool is_animation_channel(const std::string& s)
{
    static const std::unordered_set<std::string> names = {
        "ALPHA", "BRIGHTNESS", "COLOR", "HEIGHT", "HIDDEN",
        "LINE_WIDTH", "OFFSETV", "OPACITY", "POSX", "POSY",
        "POSITION", "PROG_BAR_TINT", "RENDER_MODE", "REPEATV",
        "ROTATION", "SCALE", "SCALEX", "SCALEY", "SIZE", "SIZEX",
        "SIZEY", "TELETYPE", "TELETYPE_TOGGLE", "TEXSIZEV",
        "TEXTURE", "TINT", "VISIBLE", "VISIBILITY", "WIDTH",
        "X", "Y", "Z"
    };
    return names.find(BUI_Upper_Ascii(s)) != names.end();
}

static bool is_font_name(const std::string& s)
{
    if (!is_identifierish(s)) return false;
    const std::string u = BUI_Upper_Ascii(s);
    return u.find("FONT") != std::string::npos
        || u.find("RUSSELL") != std::string::npos
        || starts_with_ci(s, "Point");
}

static bool is_ignored_control_word(const std::string& s)
{
    static const std::unordered_set<std::string> ignored = {
        "BOOL", "BUTTON", "COLOR", "CONTROL", "DEFAULT", "FALSE",
        "FLOAT", "GROUP", "IMAGE", "INT", "LABEL", "NONE", "NULL",
        "QUAD", "SCENE", "STRING", "TEXT", "TRUE"
    };
    return ignored.find(BUI_Upper_Ascii(s)) != ignored.end();
}

static std::string strip_texture_extension(std::string token)
{
    if (ends_with_ci(token, ".DDS") || ends_with_ci(token, ".TGA")) {
        token.resize(token.size() - 4);
    }
    return token;
}

static bool is_likely_texture_token(const std::string& s, const std::string& scene)
{
    if (BUI_Equal_Case_Insensitive(s, scene)) return false;
    if (!is_identifierish(s)) return false;
    if (is_child_bui_ref(s) || starts_with_ci(s, "TEXT_")) return false;
    if (s.find('\\') != std::string::npos || s.find('/') != std::string::npos) return false;

    const std::string u = BUI_Upper_Ascii(strip_texture_extension(s));
    const bool has_ext = ends_with_ci(s, ".DDS") || ends_with_ci(s, ".TGA");
    if (has_ext) return true;

    if (starts_with_ci(u, "ANIM_")
        || starts_with_ci(u, "BUILDICON_")
        || starts_with_ci(u, "BTN_")
        || starts_with_ci(u, "ICON_")
        || starts_with_ci(u, "MT_")) {
        return true;
    }

    if (starts_with_ci(u, "UI_")) return true;

    return false;
}

static bool is_likely_scene_candidate(const std::string& s)
{
    if (!is_identifierish(s)) return false;
    if (is_number_like(s)) return false;
    if (is_child_bui_ref(s)) return false;
    if (starts_with_ci(s, "TEXT_")) return false;
    if (ends_with_ci(s, ".DDS") || ends_with_ci(s, ".TGA")) return false;
    if (s.find('\\') != std::string::npos || s.find('/') != std::string::npos) return false;
    if (is_animation_channel(s)) return false;
    return true;
}

static bool is_likely_control_name(const std::string& s, const std::string& scene)
{
    if (BUI_Equal_Case_Insensitive(s, scene)) return false;
    if (!is_identifierish(s)) return false;
    if (is_number_like(s)) return false;
    if (!s.empty() && std::isdigit(static_cast<unsigned char>(s[0]))) return false;
    if (s.size() <= 3
        && s.find('_') == std::string::npos
        && (is_all_upper_word(s) || s.find_first_of("0123456789&") != std::string::npos)) {
        return false;
    }
    if (is_ignored_control_word(s)) return false;
    if (is_child_bui_ref(s)) return false;
    if (starts_with_ci(s, "TEXT_")) return false;
    if (is_animation_channel(s)) return false;
    if (is_font_name(s)) return false;
    if (s.find('\\') != std::string::npos || s.find('/') != std::string::npos) return false;
    if (ends_with_ci(s, ".DDS") || ends_with_ci(s, ".TGA")) return false;
    return true;
}

std::string BUI_Normalize_Entry_Path(const std::string& path)
{
    std::string out = trim_ascii(path);
    for (char& c : out) {
        if (c == '/') c = '\\';
    }

    while (!out.empty() && out.front() == '\\') {
        out.erase(out.begin());
    }

    const std::string u = BUI_Upper_Ascii(out);
    if (starts_with_ci(u, "DATA\\")) {
        return u;
    }
    if (starts_with_ci(u, "ART\\GUI\\")) {
        return "DATA\\" + u;
    }

    const size_t art_pos = u.find("ART\\GUI\\");
    if (art_pos != std::string::npos) {
        return "DATA\\" + u.substr(art_pos);
    }

    return u;
}

static bool inflate_zlib(const uint8_t* data, size_t size,
                         size_t max_inflated_bytes,
                         std::vector<uint8_t>& out, std::string& error)
{
    mz_stream stream;
    std::memset(&stream, 0, sizeof(stream));
    stream.next_in = data;
    stream.avail_in = static_cast<unsigned int>(size);

    int status = mz_inflateInit(&stream);
    if (status != MZ_OK) {
        error = "zlib inflate init failed";
        return false;
    }

    const size_t chunk = 64 * 1024;
    out.clear();

    for (;;) {
        const size_t old_size = out.size();
        out.resize(old_size + chunk);
        stream.next_out = out.data() + old_size;
        stream.avail_out = static_cast<unsigned int>(chunk);

        const mz_ulong before_in = stream.total_in;
        const mz_ulong before_out = stream.total_out;
        status = mz_inflate(&stream, MZ_NO_FLUSH);

        const size_t produced = chunk - stream.avail_out;
        out.resize(old_size + produced);

        if (out.size() > max_inflated_bytes) {
            error = "inflated payload exceeded safety limit";
            mz_inflateEnd(&stream);
            return false;
        }
        if (status == MZ_STREAM_END) {
            mz_inflateEnd(&stream);
            return true;
        }
        if (status != MZ_OK) {
            std::ostringstream os;
            os << "zlib inflate failed with status " << status;
            error = os.str();
            mz_inflateEnd(&stream);
            return false;
        }
        if (stream.total_in == before_in && stream.total_out == before_out) {
            error = "zlib inflate made no progress";
            mz_inflateEnd(&stream);
            return false;
        }
    }
}

static std::string clean_extracted_string(std::string s)
{
    s = trim_ascii(s);
    while (s.size() > 1 && s.back() == '+') {
        s.pop_back();
    }
    return s;
}

static std::vector<BUIString> extract_printable_strings(const std::vector<uint8_t>& data)
{
    std::vector<BUIString> out;
    const size_t min_len = 3;

    for (size_t i = 2; i < data.size(); ++i) {
        const size_t len = read_u16_le(data.data() + i - 2);
        if (len < min_len || len > 160 || i + len > data.size()) {
            continue;
        }

        bool printable = true;
        for (size_t j = 0; j < len; ++j) {
            if (!is_printable_ascii(data[i + j])) {
                printable = false;
                break;
            }
        }
        if (!printable) continue;

        std::string text(reinterpret_cast<const char*>(data.data() + i), len);
        text = clean_extracted_string(std::move(text));
        if (text.empty() || !is_identifierish(text)) continue;

        // If a printable character continues immediately after the declared
        // range, this is probably a coincidental length value inside a longer
        // byte run. A trailing '+' is a real BUI marker after some string refs.
        if (i + len < data.size()
            && is_printable_ascii(data[i + len])
            && data[i + len] != '+') {
            continue;
        }

        BUIString entry;
        entry.record_offset = i;
        entry.offset = i;
        entry.declared_length = static_cast<uint16_t>(len);
        entry.trailing_plus = (i + len < data.size() && data[i + len] == '+');
        entry.text = std::move(text);

        const size_t post_name = i + len + (entry.trailing_plus ? 1u : 0u);
        if (post_name + 8 <= data.size()) {
            entry.post_name_kind_valid = true;
            entry.post_name_kind = read_u32_le(data.data() + post_name);
            entry.post_name_value = read_u32_le(data.data() + post_name + 4);
        }

        if (i >= 6) {
            const uint32_t size_prefix = read_u32_le(data.data() + i - 6);
            if (size_prefix == len + 2) {
                entry.record_offset = i - 6;
                entry.size_prefix = size_prefix;
                entry.encoding = BUIString::Encoding::SizePrefixed;
            }
        }

        // Tagged: require a small nonzero tag (single byte) and that the
        // post-name kind/value bytes exist. A wider tag check would let any
        // stray scalar two bytes upstream flip the encoding and skew block
        // anchoring; in practice real tags are small type IDs.
        if (entry.encoding == BUIString::Encoding::Unknown && i >= 4) {
            const uint16_t tag = read_u16_le(data.data() + i - 4);
            if (tag != 0 && tag <= 0xff && entry.post_name_kind_valid) {
                entry.record_offset = i - 4;
                entry.tag = tag;
                entry.encoding = BUIString::Encoding::Tagged;
            }
        }

        out.push_back(std::move(entry));
    }

    if (!out.empty()) {
        std::sort(out.begin(), out.end(),
            [](const BUIString& a, const BUIString& b) {
                if (a.offset == b.offset) return a.text < b.text;
                return a.offset < b.offset;
            });
        return out;
    }

    size_t i = 0;
    while (i < data.size()) {
        if (!is_printable_ascii(data[i])) {
            ++i;
            continue;
        }

        const size_t begin = i;
        while (i < data.size() && is_printable_ascii(data[i])) {
            ++i;
        }

        const size_t len = i - begin;
        if (len >= min_len) {
            std::string text(reinterpret_cast<const char*>(data.data() + begin), len);
            text = clean_extracted_string(std::move(text));
            if (!text.empty()) {
                BUIString entry;
                entry.record_offset = begin;
                entry.offset = begin;
                entry.declared_length = static_cast<uint16_t>(
                    std::min<size_t>(text.size(), 0xffffu));
                entry.text = std::move(text);
                out.push_back(std::move(entry));
            }
        }
    }

    return out;
}

// A size-prefixed string whose post-name record is kind=3 value=0x33 is a
// style/default texture slot ("Empty", "UI_Sidebar_PowerSegment_Empty", ...),
// not an instantiated control. Treat these as a distinct classification so
// they do not inflate the control list.
static bool is_style_default_record(const BUIString& s)
{
    return s.encoding == BUIString::Encoding::SizePrefixed
        && s.post_name_kind_valid
        && s.post_name_kind == 3u
        && s.post_name_value == 0x33u;
}

static void classify_strings(BUIDocument& doc, const BUIReadOptions& options)
{
    OrderedBUIStrings children;
    OrderedBUIStrings normalized_children;
    OrderedBUIStrings textures;
    OrderedBUIStrings text_ids;
    OrderedBUIStrings fonts;
    OrderedBUIStrings animations;
    OrderedBUIStrings controls;
    OrderedBUIStrings styles;

    for (const BUIString& s : doc.strings) {
        if (doc.scene.empty() && is_likely_scene_candidate(s.text)) {
            doc.scene = s.text;
        }
    }

    for (const BUIString& s : doc.strings) {
        if (s.text.empty()) continue;

        if (is_child_bui_ref(s.text)) {
            children.Add(s.text);
            normalized_children.Add(BUI_Normalize_Entry_Path(s.text));
        } else if (starts_with_ci(s.text, "TEXT_")) {
            text_ids.Add(s.text);
        } else if (is_animation_channel(s.text)) {
            animations.Add(BUI_Upper_Ascii(s.text));
        } else if (is_font_name(s.text)) {
            fonts.Add(s.text);
        } else if (!BUI_Equal_Case_Insensitive(s.text, doc.scene)
                   && options.texture_probe
                   && options.texture_probe(s.text)) {
            textures.Add(s.text);
        } else if (is_likely_texture_token(s.text, doc.scene)) {
            textures.Add(s.text);
        } else if (is_style_default_record(s)) {
            styles.Add(s.text);
        } else if (is_likely_control_name(s.text, doc.scene)) {
            controls.Add(s.text);
        }
    }

    doc.children = std::move(children.values);
    doc.normalized_children = std::move(normalized_children.values);
    doc.textures = std::move(textures.values);
    doc.text_ids = std::move(text_ids.values);
    doc.fonts = std::move(fonts.values);
    doc.animations = std::move(animations.values);
    doc.controls = std::move(controls.values);
    doc.styles = std::move(styles.values);
    if (doc.scene.empty()) {
        doc.scene = "(unknown)";
    }
}

// Terminal layout-record field block, in order, immediately past the string
// bytes of a control name. Field tags are the leading byte (or first u32 for
// the i32 fields). Scalar-type byte (`2` = u16, `8` = vec2 f32) is encoded
// inline. See docs/tasks/remastered-bui-layout-inspector.md for the trace
// data this was derived from.
namespace bui_layout_field {
    constexpr size_t FIELD_X_TAG     = 0;   // u32 == 4
    constexpr size_t FIELD_X_VALUE   = 4;   // i32 x
    constexpr size_t FIELD_Y_TAG     = 8;   // u32 == 5
    constexpr size_t FIELD_Y_VALUE   = 12;  // i32 y
    constexpr size_t FIELD_W_TAG     = 16;  // u8  == 6
    constexpr size_t FIELD_W_TYPE    = 17;  // u8  == 2 (u16 scalar)
    constexpr size_t FIELD_W_VALUE   = 18;  // u16 width
    constexpr size_t FIELD_H_TAG     = 20;  // u8  == 7
    constexpr size_t FIELD_H_TYPE    = 21;  // u8  == 2
    constexpr size_t FIELD_H_VALUE   = 22;  // u16 height
    constexpr size_t FIELD_PIVOT_TAG  = 24; // u8  == 8
    constexpr size_t FIELD_PIVOT_TYPE = 25; // u8  == 8 (vec2 f32)
    constexpr size_t FIELD_PIVOT_X   = 26;  // f32 pivot.x
    constexpr size_t FIELD_PIVOT_Y   = 30;  // f32 pivot.y
    constexpr size_t BLOCK_SIZE      = 34;

    constexpr uint32_t TAG_X = 4;
    constexpr uint32_t TAG_Y = 5;
    constexpr uint8_t  TAG_W = 6;
    constexpr uint8_t  TAG_H = 7;
    constexpr uint8_t  TAG_PIVOT = 8;
    constexpr uint8_t  TYPE_U16  = 2;
    constexpr uint8_t  TYPE_VEC2 = 8;

    // Sanity bound for width/height. Real BUI fields are sub-2K screen units;
    // anything past this is almost certainly a misclassified control name
    // landing on garbage bytes.
    constexpr int32_t MAX_DIM = 16384;
    // Pivot is normalised in [0,1] for almost every record we have observed;
    // allow slack for negative anchors and overshoots without admitting NaN-
    // adjacent garbage.
    constexpr float PIVOT_LIMIT = 8.0f;
}

static std::vector<BUILayoutRecord> extract_layout_records(const std::vector<uint8_t>& data,
                                                           const BUIDocument& doc)
{
    using namespace bui_layout_field;
    std::vector<BUILayoutRecord> out;

    for (size_t i = 0; i < doc.strings.size(); ++i) {
        const BUIString& s = doc.strings[i];
        if (s.encoding != BUIString::Encoding::SizePrefixed) continue;
        if (!is_likely_control_name(s.text, doc.scene)) continue;

        // `fields_block` is the byte offset of the field block that immediately
        // follows the string text (NOT a header-relative length — `s.offset` is
        // the absolute position of the text inside the inflated payload, and
        // `s.declared_length` is the text byte count).
        const size_t fields_block = s.offset + s.declared_length;
        if (fields_block + BLOCK_SIZE > data.size()) continue;

        const uint8_t* f = data.data() + fields_block;

        if (read_u32_le(f + FIELD_X_TAG) != TAG_X) continue;
        if (read_u32_le(f + FIELD_Y_TAG) != TAG_Y) continue;
        if (f[FIELD_W_TAG] != TAG_W || f[FIELD_W_TYPE] != TYPE_U16) continue;
        if (f[FIELD_H_TAG] != TAG_H || f[FIELD_H_TYPE] != TYPE_U16) continue;
        if (f[FIELD_PIVOT_TAG] != TAG_PIVOT || f[FIELD_PIVOT_TYPE] != TYPE_VEC2) continue;

        const int32_t width  = static_cast<int32_t>(read_u16_le(f + FIELD_W_VALUE));
        const int32_t height = static_cast<int32_t>(read_u16_le(f + FIELD_H_VALUE));
        if (width <= 0 || height <= 0) continue;
        if (width > MAX_DIM || height > MAX_DIM) continue;

        const float pivot_x = read_f32_le(f + FIELD_PIVOT_X);
        const float pivot_y = read_f32_le(f + FIELD_PIVOT_Y);
        if (!std::isfinite(pivot_x) || !std::isfinite(pivot_y)) continue;
        if (pivot_x < -PIVOT_LIMIT || pivot_x > PIVOT_LIMIT
            || pivot_y < -PIVOT_LIMIT || pivot_y > PIVOT_LIMIT) {
            continue;
        }

        BUILayoutRecord rec;
        rec.string_index = i;
        rec.record_offset = s.record_offset;
        rec.fields_offset = fields_block;
        rec.name = s.text;
        rec.x = read_i32_le(f + FIELD_X_VALUE);
        rec.y = read_i32_le(f + FIELD_Y_VALUE);
        rec.width = width;
        rec.height = height;
        rec.pivot_x = pivot_x;
        rec.pivot_y = pivot_y;
        out.push_back(std::move(rec));
    }

    return out;
}

static void classify_block_strings(BUIBlock& block, const BUIDocument& doc,
                                   const BUIReadOptions& options)
{
    OrderedBUIStrings children;
    OrderedBUIStrings normalized_children;
    OrderedBUIStrings textures;
    OrderedBUIStrings text_ids;
    OrderedBUIStrings fonts;
    OrderedBUIStrings animations;
    OrderedBUIStrings controls;
    OrderedBUIStrings styles;

    for (size_t string_index : block.string_indices) {
        if (string_index >= doc.strings.size()) continue;

        const BUIString& s = doc.strings[string_index];
        if (s.text.empty()) continue;

        if (is_child_bui_ref(s.text)) {
            children.Add(s.text);
            normalized_children.Add(BUI_Normalize_Entry_Path(s.text));
        } else if (starts_with_ci(s.text, "TEXT_")) {
            text_ids.Add(s.text);
        } else if (is_animation_channel(s.text)) {
            animations.Add(BUI_Upper_Ascii(s.text));
        } else if (is_font_name(s.text)) {
            fonts.Add(s.text);
        } else if (!BUI_Equal_Case_Insensitive(s.text, doc.scene)
                   && options.texture_probe
                   && options.texture_probe(s.text)) {
            textures.Add(s.text);
        } else if (is_likely_texture_token(s.text, doc.scene)) {
            textures.Add(s.text);
        } else if (is_style_default_record(s)) {
            styles.Add(s.text);
        } else if (is_likely_control_name(s.text, doc.scene)) {
            controls.Add(s.text);
        }
    }

    block.children = std::move(children.values);
    block.normalized_children = std::move(normalized_children.values);
    block.textures = std::move(textures.values);
    block.text_ids = std::move(text_ids.values);
    block.fonts = std::move(fonts.values);
    block.animations = std::move(animations.values);
    block.controls = std::move(controls.values);
    block.styles = std::move(styles.values);
}

static void build_blocks(BUIDocument& doc, const BUIReadOptions& options)
{
    doc.blocks.clear();
    if (doc.strings.empty()) return;

    struct Anchor {
        size_t string_index = 0;
        size_t start_offset = 0;
        std::string text;
        uint16_t tag = 0;
        bool tagged = false;
    };

    std::vector<Anchor> anchors;
    anchors.push_back({0, doc.strings.front().record_offset, doc.scene, 0, false});

    for (size_t i = 0; i < doc.strings.size(); ++i) {
        const BUIString& s = doc.strings[i];
        if (s.encoding != BUIString::Encoding::Tagged) continue;
        if (!anchors.empty() && anchors.back().start_offset == s.record_offset) continue;
        anchors.push_back({i, s.record_offset, s.text, s.tag, true});
    }

    std::sort(anchors.begin(), anchors.end(),
        [](const Anchor& a, const Anchor& b) {
            if (a.start_offset == b.start_offset && a.tagged != b.tagged) {
                return a.tagged;
            }
            if (a.start_offset == b.start_offset) return a.string_index < b.string_index;
            return a.start_offset < b.start_offset;
        });

    anchors.erase(std::unique(anchors.begin(), anchors.end(),
        [](const Anchor& a, const Anchor& b) {
            return a.start_offset == b.start_offset;
        }), anchors.end());

    for (size_t i = 0; i < anchors.size(); ++i) {
        BUIBlock block;
        block.index = i;
        block.start_offset = anchors[i].start_offset;
        block.end_offset = (i + 1 < anchors.size())
            ? anchors[i + 1].start_offset
            : doc.inflated_size;
        block.anchor = anchors[i].text.empty() ? "(unknown)" : anchors[i].text;
        block.tag = anchors[i].tag;
        block.tagged_anchor = anchors[i].tagged;

        for (size_t j = 0; j < doc.strings.size(); ++j) {
            const size_t offset = doc.strings[j].record_offset;
            if (offset >= block.start_offset && offset < block.end_offset) {
                block.string_indices.push_back(j);
            }
        }

        for (size_t j = 0; j < doc.layout_records.size(); ++j) {
            const size_t offset = doc.layout_records[j].record_offset;
            if (offset >= block.start_offset && offset < block.end_offset) {
                block.layout_indices.push_back(j);
            }
        }

        classify_block_strings(block, doc, options);
        doc.blocks.push_back(std::move(block));
    }
}

bool BUIReader::Read_Memory(const void* data, size_t size, const std::string& entry,
                            BUIDocument& out, std::string& error,
                            const BUIReadOptions& options) const
{
    out = BUIDocument();
    out.entry = BUI_Normalize_Entry_Path(entry);
    out.raw_size = size;

    if (!data || size < BUI_HEADER_SIZE) {
        error = "BUI is too small for CH header";
        return false;
    }

    const auto* raw = static_cast<const uint8_t*>(data);
    out.header.magic[0] = raw[0];
    out.header.magic[1] = raw[1];
    out.header.major_version = raw[2];
    out.header.minor_version = raw[3];
    out.header.flags = read_u32_le(raw + 0x04);
    out.header.content_hash = read_u32_le(raw + 0x08);
    out.header.unknown_0c = read_u32_le(raw + 0x0c);
    out.header.compressed_size = read_u32_le(raw + 0x10);

    if (raw[0] != 'C' || raw[1] != 'H') {
        error = "bad BUI magic: expected CH";
        return false;
    }
    if (raw[2] != 2 || raw[3] != 1) {
        std::ostringstream os;
        os << "unsupported BUI CH version: "
           << static_cast<unsigned>(raw[2]) << "."
           << static_cast<unsigned>(raw[3]);
        error = os.str();
        return false;
    }

    const size_t actual_compressed = size - BUI_HEADER_SIZE;
    // Allow trailing alignment padding past the declared compressed size;
    // only truncation (less data than the header claims) is fatal.
    if (actual_compressed < out.header.compressed_size) {
        std::ostringstream os;
        os << "compressed payload truncated: header says " << out.header.compressed_size
           << ", file has " << actual_compressed << " bytes after 0x24";
        error = os.str();
        return false;
    }

    std::vector<uint8_t> inflated;
    if (!inflate_zlib(raw + BUI_HEADER_SIZE, out.header.compressed_size,
                      options.max_inflated_bytes, inflated, error)) {
        return false;
    }

    out.inflated_size = inflated.size();
    out.strings = extract_printable_strings(inflated);
    if (options.keep_inflated_payload) {
        out.inflated_payload = inflated;
    }
    classify_strings(out, options);
    out.layout_records = extract_layout_records(inflated, out);
    build_blocks(out, options);
    return true;
}

bool BUIReader::Read_Meg(const MegReader& meg, const std::string& entry,
                         BUIDocument& out, std::string& error,
                         const BUIReadOptions& options) const
{
    const std::string normalized_entry = BUI_Normalize_Entry_Path(entry);
    const MegEntry* e = meg.Find(normalized_entry.c_str());
    if (!e) {
        error = "entry not found: " + normalized_entry;
        return false;
    }

    size_t raw_size = 0;
    void* raw = meg.Read_Alloc(e, &raw_size);
    if (!raw) {
        error = "failed to read entry: " + normalized_entry;
        return false;
    }

    const bool ok = Read_Memory(raw, raw_size, normalized_entry, out, error, options);
    std::free(raw);
    if (!ok) {
        error = normalized_entry + ": " + error;
    }
    return ok;
}
