/**
 * MegReader - MEG v2 archive reader implementation.
 *
 * MEG v2 layout (unencrypted):
 *   [0]  u32 magic       (0xFFFFFFFF or 0x8FFFFFFF)
 *   [4]  f32 version
 *   [8]  u32 header_size (total bytes before file data)
 *   [12] u32 num_files
 *   [16] u32 num_strings
 *   [20] u32 string_table_size
 *   [24] String table: for each string, u16 length + chars (not null-terminated)
 *   [..] File table: num_files * 20-byte SubFileData entries, sorted by CRC
 *   [header_size] File data begins
 *
 * Reference: truth.src/.../CnCTDRAMapEditor/Utility/Megafile.cs
 */

#include "meg_reader.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <algorithm>
#include <vector>
#include <string>

// Standard CRC-32 (IEEE 802.3, polynomial 0xEDB88320)
static uint32_t crc32_table[256];
static bool crc32_table_ready = false;

static void crc32_init()
{
    if (crc32_table_ready) return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t rem = i;
        for (int j = 0; j < 8; j++) {
            if (rem & 1) {
                rem = (rem >> 1) ^ 0xEDB88320u;
            } else {
                rem >>= 1;
            }
        }
        crc32_table[i] = rem;
    }
    crc32_table_ready = true;
}

static uint32_t crc32_calc(const void* data, size_t len)
{
    crc32_init();
    const uint8_t* p = static_cast<const uint8_t*>(data);
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc = (crc >> 8) ^ crc32_table[(crc ^ p[i]) & 0xFF];
    }
    return ~crc;
}

/// Compute CRC-32 of a filename after uppercasing (MEG convention).
static uint32_t meg_filename_crc(const char* filename)
{
    size_t len = strlen(filename);
    // Uppercase in a stack buffer for typical filenames
    char stack_buf[256];
    char* buf = (len < sizeof(stack_buf)) ? stack_buf : static_cast<char*>(malloc(len + 1));
    for (size_t i = 0; i < len; i++) {
        buf[i] = static_cast<char>(toupper(static_cast<unsigned char>(filename[i])));
    }
    buf[len] = '\0';
    uint32_t crc = crc32_calc(buf, len);
    if (buf != stack_buf) free(buf);
    return crc;
}

// On-disk SubFileData: 20 bytes, packed at 2-byte alignment
#pragma pack(push, 2)
struct SubFileData {
    uint16_t flags;
    uint32_t crc;
    int32_t  index;
    uint32_t size;
    uint32_t offset;
    uint16_t name_index;
};
#pragma pack(pop)

static_assert(sizeof(SubFileData) == 20, "SubFileData must be 20 bytes packed");

struct MegReader::Impl {
    FILE*                   fp = nullptr;
    std::vector<MegEntry>   entries;
    std::vector<std::string> filenames;
};

MegReader::MegReader()
    : impl_(new Impl)
{
}

MegReader::~MegReader()
{
    Close();
    delete impl_;
}

bool MegReader::Open(const char* path)
{
    Close();

    FILE* fp = fopen(path, "rb");
    if (!fp) return false;

    // Read magic
    uint32_t magic = 0;
    if (fread(&magic, 4, 1, fp) != 1) { fclose(fp); return false; }

    // Determine offset based on format version
    uint32_t read_offset = 0;
    if (magic == 0xFFFFFFFF || magic == 0x8FFFFFFF) {
        // v2: skip version(4) + header_size(4)
        read_offset = 8;
    }
    read_offset += 4; // skip past magic/header_size field

    // Read file count, string count, string table size
    fseek(fp, read_offset, SEEK_SET);
    uint32_t num_files = 0, num_strings = 0, string_table_size = 0;
    if (fread(&num_files, 4, 1, fp) != 1) { fclose(fp); return false; }
    if (fread(&num_strings, 4, 1, fp) != 1) { fclose(fp); return false; }
    if (fread(&string_table_size, 4, 1, fp) != 1) { fclose(fp); return false; }

    read_offset += 12;

    // Read string table
    std::vector<std::string> filenames(num_strings);
    for (uint32_t i = 0; i < num_strings; i++) {
        uint16_t str_len = 0;
        if (fread(&str_len, 2, 1, fp) != 1) { fclose(fp); return false; }
        filenames[i].resize(str_len);
        if (str_len > 0) {
            if (fread(&filenames[i][0], 1, str_len, fp) != static_cast<size_t>(str_len)) {
                fclose(fp);
                return false;
            }
        }
    }

    read_offset += string_table_size;

    // Read file table
    std::vector<MegEntry> entries(num_files);
    for (uint32_t i = 0; i < num_files; i++) {
        SubFileData sfd;
        if (fread(&sfd, sizeof(SubFileData), 1, fp) != 1) { fclose(fp); return false; }
        entries[i].crc        = sfd.crc;
        entries[i].size       = sfd.size;
        entries[i].offset     = sfd.offset;
        entries[i].name_index = sfd.name_index;
    }

    impl_->fp        = fp;
    impl_->entries   = std::move(entries);
    impl_->filenames = std::move(filenames);
    return true;
}

void MegReader::Close()
{
    if (impl_->fp) {
        fclose(impl_->fp);
        impl_->fp = nullptr;
    }
    impl_->entries.clear();
    impl_->filenames.clear();
}

const MegEntry* MegReader::Find(const char* filename) const
{
    if (!impl_->fp || impl_->entries.empty()) return nullptr;

    uint32_t target_crc = meg_filename_crc(filename);

    // Binary search — file table is sorted by CRC
    int lo = 0;
    int hi = static_cast<int>(impl_->entries.size()) - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        uint32_t mid_crc = impl_->entries[mid].crc;
        if (mid_crc == target_crc) {
            return &impl_->entries[mid];
        } else if (mid_crc < target_crc) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }

    return nullptr;
}

size_t MegReader::Read(const MegEntry* entry, void* buffer) const
{
    if (!impl_->fp || !entry || !buffer) return 0;

    if (fseek(impl_->fp, entry->offset, SEEK_SET) != 0) return 0;
    size_t read = fread(buffer, 1, entry->size, impl_->fp);
    return read;
}

void* MegReader::Read_Alloc(const MegEntry* entry, size_t* size_out) const
{
    if (!impl_->fp || !entry) return nullptr;

    void* buf = malloc(entry->size);
    if (!buf) return nullptr;

    size_t read = Read(entry, buf);
    if (read != entry->size) {
        free(buf);
        return nullptr;
    }

    if (size_out) *size_out = entry->size;
    return buf;
}

int MegReader::Entry_Count() const
{
    return static_cast<int>(impl_->entries.size());
}

const char* MegReader::Get_Filename(uint16_t name_index) const
{
    if (name_index >= impl_->filenames.size()) return nullptr;
    return impl_->filenames[name_index].c_str();
}
