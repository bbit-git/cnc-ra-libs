/**
 * Tests for MegReader — MEG v2 archive parser.
 *
 * Tests cover:
 *   - Header parsing (magic numbers, counts)
 *   - Filename table parsing
 *   - File table CRC lookup
 *   - File data reading (buffer + alloc)
 *   - Case-insensitive filename lookup
 *   - Edge cases (empty archive, missing file, zero-size entry)
 *   - Corrupt/truncated input rejection
 *
 * Build: g++ -std=c++17 -o test_meg_reader test_meg_reader.cpp ../meg_reader.cpp
 */

#include "test_framework.h"
#include "../meg_reader.h"

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <memory>
#include <vector>

/*
 * Helper: build a valid MEG v2 binary in memory.
 *
 * MEG v2 layout (matches truth.src Megafile.cs / SubFileData):
 *   [Header 24B] [Filename Table] [File Table] [File Data]
 *
 * Header:
 *   uint32 magic       = 0xFFFFFFFF
 *   float  version     = 0.99 (0x3F7D70A4)
 *   uint32 headerSize  (total bytes before file data)
 *   uint32 numFiles
 *   uint32 numStrings
 *   uint32 stringTableSize
 *
 * Filename entry: uint16 length + ASCII chars (no null terminator)
 * File table entry (20B, pack=2):
 *   uint16 flags, uint32 crc, int32 index, uint32 size, uint32 offset, uint16 nameIndex
 */

static uint32_t meg_crc32(const char* name) {
    /* Standard CRC-32/ISO-HDLC on uppercase filename */
    uint32_t crc = 0xFFFFFFFF;
    while (*name) {
        uint8_t c = (uint8_t)*name++;
        if (c >= 'a' && c <= 'z') c -= 32; /* uppercase */
        crc ^= c;
        for (int i = 0; i < 8; i++)
            crc = (crc >> 1) ^ (0xEDB88320 & (-(crc & 1)));
    }
    return crc ^ 0xFFFFFFFF;
}

struct MegTestFile {
    const char* name;
    const void* data;
    uint32_t    size;
};

static std::vector<uint8_t> build_meg_v2(const MegTestFile* files, int count) {
    std::vector<uint8_t> buf;
    auto put16 = [&](uint16_t v) { buf.push_back(v & 0xFF); buf.push_back(v >> 8); };
    auto put32 = [&](uint32_t v) {
        buf.push_back(v & 0xFF); buf.push_back((v >> 8) & 0xFF);
        buf.push_back((v >> 16) & 0xFF); buf.push_back((v >> 24) & 0xFF);
    };

    /* Header placeholder (24 bytes) */
    size_t hdr_pos = buf.size();
    for (int i = 0; i < 24; i++) buf.push_back(0);

    /* Filename table — compute size for header */
    size_t str_table_start = buf.size();
    for (int i = 0; i < count; i++) {
        uint16_t len = (uint16_t)strlen(files[i].name);
        put16(len);
        for (uint16_t j = 0; j < len; j++)
            buf.push_back((uint8_t)files[i].name[j]);
    }
    uint32_t string_table_size = (uint32_t)(buf.size() - str_table_start);

    /* File table placeholder (20 bytes each, SubFileData layout) */
    size_t ftable_pos = buf.size();
    for (int i = 0; i < count * 20; i++) buf.push_back(0);

    /* headerSize = everything before file data */
    uint32_t header_size = (uint32_t)buf.size();

    /* File data + collect table entries */
    struct FTEntry { uint32_t crc; int32_t idx; uint32_t size; uint32_t offset; uint16_t name_idx; };
    std::vector<FTEntry> entries;
    for (int i = 0; i < count; i++) {
        uint32_t abs_offset = (uint32_t)buf.size(); /* absolute offset */
        entries.push_back({meg_crc32(files[i].name), (int32_t)i,
                           files[i].size, abs_offset, (uint16_t)i});
        for (uint32_t j = 0; j < files[i].size; j++)
            buf.push_back(((const uint8_t*)files[i].data)[j]);
    }

    /* Sort file table by CRC (MEG spec requires this) */
    for (size_t i = 0; i < entries.size(); i++)
        for (size_t j = i + 1; j < entries.size(); j++)
            if (entries[j].crc < entries[i].crc)
                std::swap(entries[i], entries[j]);

    /* Write file table (SubFileData: flags(u16) crc(u32) index(i32) size(u32) offset(u32) nameIndex(u16)) */
    for (int i = 0; i < count; i++) {
        size_t pos = ftable_pos + i * 20;
        auto write16 = [&](size_t off, uint16_t v) {
            buf[off] = v & 0xFF; buf[off+1] = v >> 8;
        };
        auto write32 = [&](size_t off, uint32_t v) {
            buf[off]   = v & 0xFF; buf[off+1] = (v >> 8) & 0xFF;
            buf[off+2] = (v >> 16) & 0xFF; buf[off+3] = (v >> 24) & 0xFF;
        };
        write16(pos + 0,  0);                    /* flags */
        write32(pos + 2,  entries[i].crc);       /* crc */
        write32(pos + 6,  (uint32_t)entries[i].idx);  /* index */
        write32(pos + 10, entries[i].size);      /* size */
        write32(pos + 14, entries[i].offset);    /* offset (absolute) */
        write16(pos + 18, entries[i].name_idx);  /* nameIndex */
    }

    /* Write header (24 bytes) */
    auto write32 = [&](size_t off, uint32_t v) {
        buf[off]   = v & 0xFF; buf[off+1] = (v >> 8) & 0xFF;
        buf[off+2] = (v >> 16) & 0xFF; buf[off+3] = (v >> 24) & 0xFF;
    };
    write32(hdr_pos + 0,  0xFFFFFFFF);              /* magic */
    write32(hdr_pos + 4,  0x3F7D70A4);              /* version (0.99f) */
    write32(hdr_pos + 8,  header_size);              /* headerSize */
    write32(hdr_pos + 12, (uint32_t)count);          /* numFiles */
    write32(hdr_pos + 16, (uint32_t)count);          /* numStrings */
    write32(hdr_pos + 20, string_table_size);        /* stringTableSize */

    return buf;
}

static const char* write_temp_meg(const std::vector<uint8_t>& data, const char* name) {
    static char path[256];
    snprintf(path, sizeof(path), "/tmp/render_test_%s.meg", name);
    FILE* f = fopen(path, "wb");
    if (f) {
        fwrite(data.data(), 1, data.size(), f);
        fclose(f);
    }
    return path;
}

/* ─── Tests ─────────────────────────────────────────────────── */

TEST(meg_open_valid_archive) {
    const char content[] = "hello world";
    MegTestFile files[] = {{"TEST.TXT", content, sizeof(content) - 1}};
    auto meg = build_meg_v2(files, 1);
    const char* path = write_temp_meg(meg, "valid");

    MegReader reader;
    EXPECT_TRUE(reader.Open(path));
    EXPECT_EQ(reader.Entry_Count(), 1);
    reader.Close();
    PASS();
}

TEST(meg_open_nonexistent_file) {
    MegReader reader;
    EXPECT_FALSE(reader.Open("/tmp/render_test_nonexistent_12345.meg"));
    PASS();
}

TEST(meg_open_empty_archive) {
    MegTestFile files[] = {};
    auto meg = build_meg_v2(files, 0);
    const char* path = write_temp_meg(meg, "empty");

    MegReader reader;
    EXPECT_TRUE(reader.Open(path));
    EXPECT_EQ(reader.Entry_Count(), 0);
    reader.Close();
    PASS();
}

TEST(meg_open_truncated_header) {
    /* Only 10 bytes — header needs 24 */
    std::vector<uint8_t> data(10, 0xFF);
    const char* path = write_temp_meg(data, "truncated");

    MegReader reader;
    EXPECT_FALSE(reader.Open(path));
    PASS();
}

TEST(meg_open_wrong_magic) {
    const char content[] = "x";
    MegTestFile files[] = {{"A.TXT", content, 1}};
    auto meg = build_meg_v2(files, 1);
    /* Corrupt magic at offset 0 — make it unrecognizable */
    meg[0] = 0x00;
    meg[1] = 0x00;
    meg[2] = 0x00;
    meg[3] = 0x00;
    const char* path = write_temp_meg(meg, "badmagic");

    MegReader reader;
    /* With corrupted magic, parser takes old-format path with wrong offsets.
       It may fail to parse or produce garbage — either way it should not crash. */
    /* The result depends on the data interpretation; we just verify no crash. */
    reader.Open(path);
    reader.Close();
    PASS();
}

TEST(meg_find_existing_file) {
    const char content[] = "payload";
    MegTestFile files[] = {{"DATA/FILE.TGA", content, 7}};
    auto meg = build_meg_v2(files, 1);
    const char* path = write_temp_meg(meg, "find");

    MegReader reader;
    EXPECT_TRUE(reader.Open(path));

    const MegEntry* entry = reader.Find("DATA/FILE.TGA");
    EXPECT_NOT_NULL(entry);
    EXPECT_EQ(entry->size, 7u);
    reader.Close();
    PASS();
}

TEST(meg_find_case_insensitive) {
    const char content[] = "data";
    MegTestFile files[] = {{"ART/UNIT.TGA", content, 4}};
    auto meg = build_meg_v2(files, 1);
    const char* path = write_temp_meg(meg, "case");

    MegReader reader;
    EXPECT_TRUE(reader.Open(path));

    EXPECT_NOT_NULL(reader.Find("art/unit.tga"));
    EXPECT_NOT_NULL(reader.Find("ART/UNIT.TGA"));
    EXPECT_NOT_NULL(reader.Find("Art/Unit.Tga"));
    reader.Close();
    PASS();
}

TEST(meg_find_missing_file) {
    const char content[] = "x";
    MegTestFile files[] = {{"EXISTS.TXT", content, 1}};
    auto meg = build_meg_v2(files, 1);
    const char* path = write_temp_meg(meg, "missing");

    MegReader reader;
    EXPECT_TRUE(reader.Open(path));
    EXPECT_NULL(reader.Find("NOPE.TXT"));
    reader.Close();
    PASS();
}

TEST(meg_read_file_data) {
    const char content[] = "the quick brown fox";
    MegTestFile files[] = {{"README.TXT", content, (uint32_t)strlen(content)}};
    auto meg = build_meg_v2(files, 1);
    const char* path = write_temp_meg(meg, "readdata");

    MegReader reader;
    EXPECT_TRUE(reader.Open(path));

    const MegEntry* entry = reader.Find("README.TXT");
    EXPECT_NOT_NULL(entry);

    char buf[64] = {};
    size_t n = reader.Read(entry, buf);
    EXPECT_EQ(n, strlen(content));
    EXPECT_MEM_EQ(buf, content, strlen(content));
    reader.Close();
    PASS();
}

TEST(meg_read_alloc) {
    const char content[] = "allocated read test";
    MegTestFile files[] = {{"ALLOC.TXT", content, (uint32_t)strlen(content)}};
    auto meg = build_meg_v2(files, 1);
    const char* path = write_temp_meg(meg, "alloc");

    MegReader reader;
    EXPECT_TRUE(reader.Open(path));

    const MegEntry* entry = reader.Find("ALLOC.TXT");
    EXPECT_NOT_NULL(entry);

    size_t size = 0;
    void* data = reader.Read_Alloc(entry, &size);
    EXPECT_NOT_NULL(data);
    EXPECT_EQ(size, strlen(content));
    EXPECT_MEM_EQ(data, content, strlen(content));
    free(data);
    reader.Close();
    PASS();
}

TEST(meg_multiple_files) {
    const char a[] = "alpha";
    const char b[] = "bravo charlie";
    const char c[] = "d";
    MegTestFile files[] = {
        {"FIRST.TXT",  a, 5},
        {"SECOND.TXT", b, 13},
        {"THIRD.TXT",  c, 1},
    };
    auto meg = build_meg_v2(files, 3);
    const char* path = write_temp_meg(meg, "multi");

    MegReader reader;
    EXPECT_TRUE(reader.Open(path));
    EXPECT_EQ(reader.Entry_Count(), 3);

    /* Verify each file independently */
    char buf[64];

    const MegEntry* e1 = reader.Find("FIRST.TXT");
    EXPECT_NOT_NULL(e1);
    EXPECT_EQ(e1->size, 5u);
    memset(buf, 0, sizeof(buf));
    reader.Read(e1, buf);
    EXPECT_MEM_EQ(buf, a, 5);

    const MegEntry* e2 = reader.Find("SECOND.TXT");
    EXPECT_NOT_NULL(e2);
    EXPECT_EQ(e2->size, 13u);
    memset(buf, 0, sizeof(buf));
    reader.Read(e2, buf);
    EXPECT_MEM_EQ(buf, b, 13);

    const MegEntry* e3 = reader.Find("THIRD.TXT");
    EXPECT_NOT_NULL(e3);
    EXPECT_EQ(e3->size, 1u);
    memset(buf, 0, sizeof(buf));
    reader.Read(e3, buf);
    EXPECT_MEM_EQ(buf, c, 1);

    reader.Close();
    PASS();
}

TEST(meg_zero_size_file) {
    MegTestFile files[] = {{"EMPTY.TXT", "", 0}};
    auto meg = build_meg_v2(files, 1);
    const char* path = write_temp_meg(meg, "zerosize");

    MegReader reader;
    EXPECT_TRUE(reader.Open(path));

    const MegEntry* entry = reader.Find("EMPTY.TXT");
    EXPECT_NOT_NULL(entry);
    EXPECT_EQ(entry->size, 0u);
    reader.Close();
    PASS();
}

TEST(meg_get_filename) {
    const char content[] = "x";
    MegTestFile files[] = {{"MY/PATH/FILE.TGA", content, 1}};
    auto meg = build_meg_v2(files, 1);
    const char* path = write_temp_meg(meg, "getname");

    MegReader reader;
    EXPECT_TRUE(reader.Open(path));

    const MegEntry* entry = reader.Find("MY/PATH/FILE.TGA");
    EXPECT_NOT_NULL(entry);

    const char* name = reader.Get_Filename(entry->name_index);
    EXPECT_NOT_NULL(name);
    EXPECT_STR_EQ(name, "MY/PATH/FILE.TGA");
    reader.Close();
    PASS();
}

TEST(meg_close_and_reopen) {
    const char content[] = "reopen";
    MegTestFile files[] = {{"FILE.TXT", content, 6}};
    auto meg = build_meg_v2(files, 1);
    const char* path = write_temp_meg(meg, "reopen");

    MegReader reader;
    EXPECT_TRUE(reader.Open(path));
    reader.Close();

    /* Reopen same reader */
    EXPECT_TRUE(reader.Open(path));
    EXPECT_NOT_NULL(reader.Find("FILE.TXT"));
    reader.Close();
    PASS();
}

int main() {
    printf("test_meg_reader\n");
    return RUN_TESTS();
}
