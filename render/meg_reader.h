/**
 * MegReader - MEG v2 archive reader (C&C Remastered Collection format).
 *
 * MEG v2 is unencrypted. Structure:
 *   [Header 20B] [Filename Table] [File Table] [File Data]
 *
 * Reference: truth.src/.../CnCTDRAMapEditor/Utility/Megafile.cs
 * Spec: modtools.petrolution.net/docs/MegFileFormat
 */

#ifndef RENDER_MEG_READER_H
#define RENDER_MEG_READER_H

#include <cstdint>
#include <cstddef>

struct MegEntry {
    uint32_t crc;
    uint32_t size;
    uint32_t offset;       // Absolute offset in file
    uint16_t name_index;   // Index into filename table
};

class MegReader {
public:
    MegReader();
    ~MegReader();

    /**
     * Open and parse a MEG v2 archive.
     * @param path  Filesystem path to .meg file
     * @return true on success
     */
    bool Open(const char* path);

    void Close();

    /**
     * Find a file entry by path (case-insensitive).
     * @param filename  Path within MEG (e.g. "ART/TEXTURES/SRGB/TD/UNITS/HTANK-0.TGA")
     * @return Pointer to entry, or nullptr if not found
     */
    const MegEntry* Find(const char* filename) const;

    /**
     * Read file data into caller-provided buffer.
     * @param entry   Entry from Find()
     * @param buffer  Output buffer (must be >= entry->size bytes)
     * @return Bytes read, or 0 on failure
     */
    size_t Read(const MegEntry* entry, void* buffer) const;

    /**
     * Read file data into newly allocated buffer (caller must free).
     * @param entry     Entry from Find()
     * @param size_out  Filled with file size
     * @return Allocated buffer, or nullptr on failure
     */
    void* Read_Alloc(const MegEntry* entry, size_t* size_out) const;

    int Entry_Count() const;
    
    /**
     * Get an entry by index (0 to Entry_Count() - 1).
     */
    const MegEntry* Get_Entry(int index) const;

    /**
     * Get filename for an entry by its name_index.
     */
    const char* Get_Filename(uint16_t name_index) const;

private:
    struct Impl;
    Impl* impl_;
};

#endif // RENDER_MEG_READER_H
