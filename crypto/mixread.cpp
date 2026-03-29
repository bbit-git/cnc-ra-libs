/*
 * mixread.cpp — Standalone MIX file reader with Blowfish/RSA support.
 *
 * Reads both plain (CNC) and encrypted (RA) MIX archives.
 * Uses the RA engine's crypto primitives but doesn't depend on the game engine.
 */

#include "mixread.h"

/* RA engine crypto primitives (compiled as part of this library) */
#include "pk.h"
#include "pkstraw.h"
#include "rndstraw.h"
#include "xstraw.h"
#include "rawfile.h"
#include "ramfile.h"
#include "ini.h"

#include <cstdio>
#include <cstring>
#include <cctype>
#include <cstdlib>
#include <algorithm>

/* ======================================================================
 * Westwood CRC — matches the engine's Calculate_CRC
 * ====================================================================== */

static unsigned int crc_rotl(unsigned int v, int n) { return (v << n) | (v >> (32 - n)); }
static unsigned int crc_rotr(unsigned int v, int n) { return (v >> n) | (v << (32 - n)); }

int32_t MixReader::filename_crc(const char* filename) {
    char upper[256];
    int len = (int)strlen(filename);
    if (len >= (int)sizeof(upper)) len = (int)sizeof(upper) - 1;
    for (int i = 0; i < len; i++) upper[i] = toupper((unsigned char)filename[i]);
    upper[len] = 0;

    auto* b = (const unsigned char*)upper;
    unsigned int crc = 0;
    int off = 0;
    while (off + 4 <= len) {
        unsigned int chunk = b[off] | (b[off+1]<<8) | (b[off+2]<<16) | (b[off+3]<<24);
        crc = crc_rotl(crc, 1) + chunk;
        off += 4;
    }
    int rem = len - off;
    if (rem > 0) {
        unsigned int chunk = 0;
        for (int i = 0; i < rem; i++) {
            chunk = crc_rotr(chunk, 8);
            chunk |= (unsigned int)b[off+i] << 24;
        }
        chunk = crc_rotr(chunk, (unsigned int)((4-rem)*8));
        crc = crc_rotl(crc, 1) + chunk;
    }
    return (int32_t)crc;
}

/* Also check with the old CRC algorithm (CNC uses a different one) */
static uint32_t old_crc(const char* filename) {
    char upper[256];
    int len = (int)strlen(filename);
    if (len >= (int)sizeof(upper)) len = (int)sizeof(upper) - 1;
    for (int i = 0; i < len; i++) upper[i] = toupper((unsigned char)filename[i]);
    upper[len] = 0;

    auto* b = (const unsigned char*)upper;
    unsigned int crc = 0;
    for (int i = 0; i < len; i++)
        crc = ((crc << 1) | (crc >> 31)) + b[i];
    return crc;
}

/* ======================================================================
 * MIX file format structures
 * ====================================================================== */

#pragma pack(push, 1)
struct MixFileHeader {
    int16_t count;
    int32_t size;
};
struct MixSubBlock {
    int32_t crc;
    int32_t offset;
    int32_t size;
};
#pragma pack(pop)

/* Global RandomStraw used by PKStraw for decryption */
static RandomStraw g_crypt_random;

/* ======================================================================
 * MixReader implementation
 * ====================================================================== */

struct MixReader::Impl {
    PKey fast_key;
    bool key_initialized = false;
};

MixReader::MixReader() : impl_(new Impl) {}
MixReader::~MixReader() { delete impl_; }

void MixReader::init_ra_key(const char* key_ini_data, int key_ini_len) {
    RAMFileClass file((void*)key_ini_data, key_ini_len);
    INIClass ini;
    ini.Load(file);
    impl_->fast_key = ini.Get_PKey(true);
    impl_->key_initialized = true;
}

void MixReader::add_search_path(const std::string& path) {
    std::string p = path;
    if (!p.empty() && p.back() != '/') p += '/';
    search_paths_.push_back(p);
}

bool MixReader::parse_mix(FILE* f, long base_offset, const std::string& label,
                           MixReadIndex& out) {
    out.path = label;
    out.encrypted = false;

    fseek(f, base_offset, SEEK_SET);

    /* Read first 4 bytes — either a TD-style header or RA extended flags.
     * RA extended: First(2)=0x0000, Second(2)=flags (0x01=digest, 0x02=encrypted)
     * TD plain: First(2)=count (non-zero), next 4=size */
    struct { int16_t first; int16_t second; } alternate;
    if (fread(&alternate, 4, 1, f) != 1) return false;

    MixFileHeader hdr;

    if (alternate.first == 0) {
        /* RA extended format */
        bool is_digest    = (alternate.second & 0x01) != 0;
        bool is_encrypted = (alternate.second & 0x02) != 0;

        if (!is_encrypted) {
            /* RA plain (unencrypted) extended format */
            if (fread(&hdr, sizeof(hdr), 1, f) != 1) return false;
            if (hdr.count <= 0 || hdr.count > 10000) return false;

            out.entries.resize(hdr.count);
            if ((int)fread(out.entries.data(), sizeof(MixReadEntry), hdr.count, f) != hdr.count)
                return false;

            out.data_start = (int32_t)(ftell(f) - base_offset);
            return true;
        }

        /* Encrypted — fall through to crypto path */
    }

    if (alternate.first == 0 && (alternate.second & 0x02)) {
        /* Encrypted and/or digest — use RA engine crypto */
        out.encrypted = true;

        if (!impl_) return false;
        if (!impl_->key_initialized) return false;

        /* Re-open through RA file I/O + straw chain for decryption.
         * We need a RawFileClass positioned at the MIX file start. */
        fseek(f, base_offset, SEEK_SET);

        /* Read the full file into memory for straw-based processing */
        fseek(f, 0, SEEK_END);
        long file_size = ftell(f);
        long mix_size = file_size - base_offset;
        fseek(f, base_offset, SEEK_SET);

        unsigned char* buf = (unsigned char*)malloc(mix_size);
        if (!buf) return false;
        if ((long)fread(buf, 1, mix_size, f) != mix_size) {
            free(buf);
            return false;
        }

        /* Use BufferStraw to read from our buffer */
        BufferStraw bstraw(buf, mix_size);
        PKStraw pstraw(PKStraw::DECRYPT, g_crypt_random);
        Straw* straw = &bstraw;

        /* Skip the 4-byte flags (already read) — re-read from buffer */
        struct { short first; short second; } alt;
        straw->Get(&alt, sizeof(alt));

        bool is_encrypted = (alt.second & 0x02) != 0;

        if (is_encrypted) {
            pstraw.Key(&impl_->fast_key);
            pstraw.Get_From(&bstraw);
            straw = &pstraw;
        }

        MixFileHeader hdr;
        straw->Get(&hdr, sizeof(hdr));

        if (hdr.count <= 0 || hdr.count > 10000) {
            free(buf);
            return false;
        }

        out.entries.resize(hdr.count);

        /* Read SubBlock entries through the (possibly decrypting) straw */
        MixSubBlock* subs = (MixSubBlock*)malloc(hdr.count * sizeof(MixSubBlock));
        straw->Get(subs, hdr.count * sizeof(MixSubBlock));
        for (int i = 0; i < hdr.count; i++) {
            out.entries[i].crc = subs[i].crc;
            out.entries[i].offset = subs[i].offset;
            out.entries[i].size = subs[i].size;
        }
        ::free(subs);

        /* Calculate data start: flags(4) + encrypted_header_size
         * The encrypted header is padded to 8-byte blocks. The header contains:
         *   - 80 bytes RSA key block
         *   - FileHeader (6 bytes) + SubBlocks (count * 12 bytes) encrypted in Blowfish
         *   - Padding to 8-byte boundary
         * Total header = 4 (flags) + 80 (RSA) + ceil8(6 + count*12) */
        if (is_encrypted) {
            int raw_header = 6 + hdr.count * 12;
            int padded = (raw_header + 7) & ~7;  /* round up to 8 */
            out.data_start = 4 + 80 + padded;
        } else {
            out.data_start = 4 + 6 + hdr.count * 12;
        }

        free(buf);
        return true;
    }

    /* TD-style plain format: first two bytes are low word of count */
    fseek(f, base_offset, SEEK_SET);
    if (fread(&hdr, sizeof(hdr), 1, f) != 1) return false;
    if (hdr.count <= 0 || hdr.count > 10000) return false;

    out.entries.resize(hdr.count);
    if ((int)fread(out.entries.data(), sizeof(MixReadEntry), hdr.count, f) != hdr.count)
        return false;

    out.data_start = (int32_t)(ftell(f) - base_offset);
    return true;
}

bool MixReader::load_from_disk(const char* filename) {
    bool any = false;
    for (auto& dir : search_paths_) {
        std::string path = dir + filename;
        if (loaded_.count(path)) continue;  /* dedup by full path */
        FILE* f = fopen(path.c_str(), "rb");
        if (!f) continue;

        MixReadIndex idx;
        if (parse_mix(f, 0, path, idx)) {
            loaded_.insert(path);
            indices_.push_back(std::move(idx));
            any = true;
        }
        fclose(f);
    }
    return any;
}

bool MixReader::load_from_parent(const char* filename) {
    /* Compute CRC for the MIX filename */
    int32_t target_crc = filename_crc(filename);
    uint32_t target_old = old_crc(filename);

    for (auto& parent : indices_) {
        for (auto& entry : parent.entries) {
            if (entry.crc != target_crc && (uint32_t)entry.crc != target_old)
                continue;

            /* Found a sub-MIX entry — open the parent file and parse from offset */
            std::string disk_path = parent.path;
            /* If parent is itself a sub-MIX label like "path>child", extract disk path */
            auto gt = disk_path.find('>');
            if (gt != std::string::npos)
                disk_path = disk_path.substr(0, gt);

            FILE* f = fopen(disk_path.c_str(), "rb");
            if (!f) continue;

            long abs_offset = parent.data_start + entry.offset;
            /* For sub-MIX in a plain parent, data_start is relative to file start
             * if the parent is at offset 0. For nested, we'd need to track absolute
             * offsets, but for now handle the common case. */

            /* If the parent path contains '>', the base is not 0.
             * For simplicity, only handle top-level parents for sub-MIX. */
            if (gt != std::string::npos) {
                fclose(f);
                continue;
            }

            std::string label = parent.path + ">" + filename;
            MixReadIndex idx;
            if (parse_mix(f, abs_offset, label, idx)) {
                fclose(f);
                loaded_.insert(filename);
                indices_.push_back(std::move(idx));
                return true;
            }
            fclose(f);
        }
    }
    return false;
}

bool MixReader::open_mix(const char* filename) {
    char upper[256];
    int len = (int)strlen(filename);
    for (int i = 0; i < len && i < 255; i++) upper[i] = toupper((unsigned char)filename[i]);
    upper[len] = 0;

    /* Load from all search paths (same name in different dirs = different data) */
    bool found = load_from_disk(filename);
    if (!found) found = load_from_disk(upper);
    /* Also try inside already-loaded parent MIX files */
    if (load_from_parent(upper)) found = true;
    return found;
}

bool MixReader::has_file(const char* filename) const {
    int32_t crc = filename_crc(filename);
    uint32_t crc2 = old_crc(filename);

    for (auto& idx : indices_) {
        for (auto& e : idx.entries) {
            if (e.crc == crc || (uint32_t)e.crc == crc2)
                return true;
        }
    }
    return false;
}

MixReadFileInfo MixReader::find_file(const char* filename) const {
    int32_t crc = filename_crc(filename);
    uint32_t crc2 = old_crc(filename);

    for (auto& idx : indices_) {
        for (auto& e : idx.entries) {
            if (e.crc == crc || (uint32_t)e.crc == crc2) {
                MixReadFileInfo info;
                info.mix_path = idx.path;
                info.crc = e.crc;
                info.offset = idx.data_start + e.offset;
                info.size = e.size;
                info.found = true;
                return info;
            }
        }
    }

    MixReadFileInfo info;
    info.crc = crc;
    info.offset = 0;
    info.size = 0;
    info.found = false;
    return info;
}
