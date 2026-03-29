/*
 * mixread.h — Standalone MIX file reading API.
 *
 * Provides access to C&C/RA MIX archives including encrypted (Blowfish/RSA)
 * files, without requiring the full game engine.
 *
 * Usage:
 *   MixReader reader;
 *   reader.add_search_path("/path/to/data/");
 *   reader.open_mix("REDALERT.MIX");   // opens and indexes
 *   reader.open_mix("MAIN.MIX");       // finds inside parent or on disk
 *
 *   auto entries = reader.list_entries("MOVIES1.MIX"); // CRC, offset, size
 *   bool found = reader.has_file("AAGUN.VQA");
 *   auto info = reader.find_file("AAGUN.VQA");  // which MIX, offset, size
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <set>

struct MixReadEntry {
    int32_t     crc;
    int32_t     offset;
    int32_t     size;
};

struct MixReadFileInfo {
    std::string mix_path;        /* which MIX file contains it */
    int32_t     crc;
    int32_t     offset;          /* absolute offset in the MIX file on disk */
    int32_t     size;
    bool        found;
};

struct MixReadIndex {
    std::string               path;      /* filesystem path or "parent>child" */
    std::vector<MixReadEntry> entries;
    int32_t                   data_start; /* offset of data section */
    bool                      encrypted;
};

class MixReader {
public:
    MixReader();
    ~MixReader();

    /* Initialize RA public key (call before opening encrypted MIX files).
     * Pass the embedded key INI string from const.cpp. */
    void init_ra_key(const char* key_ini_data, int key_ini_len);

    /* Add a directory to search for MIX files on disk. */
    void add_search_path(const std::string& path);

    /* Open and index a MIX file. Searches disk paths and already-opened
     * parent MIX files. Returns true if found and indexed. */
    bool open_mix(const char* filename);

    /* Check if a file (e.g. "AAGUN.VQA") exists in any loaded MIX. */
    bool has_file(const char* filename) const;

    /* Find a file — returns info about which MIX contains it. */
    MixReadFileInfo find_file(const char* filename) const;

    /* Get all loaded MIX indices (for map/dump). */
    const std::vector<MixReadIndex>& get_indices() const { return indices_; }

    /* Compute the Westwood CRC for a filename (uppercase). */
    static int32_t filename_crc(const char* filename);

private:
    struct Impl;
    Impl* impl_;

    std::vector<std::string>   search_paths_;
    std::vector<MixReadIndex>  indices_;
    std::set<std::string>      loaded_;  /* dedup */

    bool load_from_disk(const char* filename);
    bool load_from_parent(const char* filename);
    bool parse_mix(FILE* f, long base_offset, const std::string& label, MixReadIndex& out);
};
