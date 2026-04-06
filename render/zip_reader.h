#ifndef EA_ZIP_READER_H
#define EA_ZIP_READER_H

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

class ZipReader {
public:
    ZipReader();
    ~ZipReader();

    /// Open a ZIP archive from a memory buffer.
    /// The buffer must remain valid for the lifetime of the reader.
    bool Open(const void* data, size_t size);

    /// Close and release internal state.
    void Close();

    /// Number of files in the archive.
    int File_Count() const;

    /// Get filename by index.
    const char* Get_Filename(int index) const;

    /// Find a file by name (case-insensitive).
    /// Returns file index, or -1 if not found.
    int Find(const char* filename) const;

    /// Get uncompressed size of a file by index.
    size_t File_Size(int index) const;

    /// Extract a file to a malloc'd buffer. Caller must free().
    /// Returns nullptr on failure.
    void* Extract(int index, size_t* size_out) const;

    /// Extract a file by name. Convenience wrapper.
    void* Extract(const char* filename, size_t* size_out) const;

private:
    void* impl_;  // miniz mz_zip_archive*
    mutable std::vector<std::string> filenames_;
};

#endif
