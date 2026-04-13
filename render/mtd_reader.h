/**
 * mtd_reader.h — MTD (Mega Texture Dictionary) sprite catalog parser.
 *
 * Parses MT_COMMANDBAR_COMMON.MTD files from the C&C Remastered Collection.
 * Each MTD entry maps a sprite name to a rectangular region in the companion
 * TGA atlas texture.
 *
 * Format spec: docs/format-mtd.md
 */

#ifndef RENDER_MTD_READER_H
#define RENDER_MTD_READER_H

#include <cstdint>
#include <cstddef>

/// A single sprite entry in the MTD atlas.
struct MTDEntry {
    const char* name;           // Null-terminated sprite name (e.g. "BUILDICON_TD_E1.TGA")
    uint32_t    atlas_x;        // X offset in atlas (pixels)
    uint32_t    atlas_y;        // Y offset in atlas (pixels)
    uint32_t    width;          // Sprite width (pixels)
    uint32_t    height;         // Sprite height (pixels)
    uint32_t    origin_x;       // Draw origin X (always 0 in command bar atlas)
    uint32_t    origin_y;       // Draw origin Y (always 0 in command bar atlas)
    uint32_t    canvas_width;   // Logical canvas width (equals width in this atlas)
    uint32_t    canvas_height;  // Logical canvas height (equals height in this atlas)
    uint8_t     flag;           // 0 = build icon, 1 = UI element
};

/// Parsed MTD sprite catalog. Owns a copy of the raw data.
class MTDReader {
public:
    MTDReader();
    ~MTDReader();

    /**
     * Parse an MTD file from a memory buffer.
     * The reader makes an internal copy of the data.
     * @param data  Pointer to MTD file contents
     * @param size  Size in bytes
     * @return true on success
     */
    bool Open(const void* data, size_t size);

    void Close();

    /// Number of sprite entries.
    int Count() const;

    /// Bits per pixel of the companion TGA atlas.
    int BPP() const;

    /**
     * Get entry by index (0 to Count()-1).
     * @return Pointer to entry, or nullptr if out of range
     */
    const MTDEntry* Get_Entry(int index) const;

    /**
     * Find a sprite by name (case-insensitive).
     * @param name  Sprite name (e.g. "BUILDICON_TD_E1.TGA")
     * @return Pointer to entry, or nullptr if not found
     */
    const MTDEntry* Find(const char* name) const;

private:
    struct Impl;
    Impl* impl_;
};

#endif // RENDER_MTD_READER_H
