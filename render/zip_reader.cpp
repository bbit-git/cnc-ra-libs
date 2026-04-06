#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#define MINIZ_NO_ARCHIVE_WRITING_APIS
#include "miniz.h"

#include "zip_reader.h"
#include <cstring>
#include <cctype>

ZipReader::ZipReader() : impl_(nullptr) {}

ZipReader::~ZipReader() { Close(); }

bool ZipReader::Open(const void* data, size_t size) {
    Close();
    auto* zip = new mz_zip_archive();
    memset(zip, 0, sizeof(*zip));
    if (!mz_zip_reader_init_mem(zip, data, size, 0)) {
        delete zip;
        return false;
    }
    impl_ = zip;
    filenames_.reserve(File_Count());
    filenames_.clear();
    return true;
}

void ZipReader::Close() {
    if (impl_) {
        mz_zip_reader_end(static_cast<mz_zip_archive*>(impl_));
        delete static_cast<mz_zip_archive*>(impl_);
        impl_ = nullptr;
    }
    filenames_.clear();
}

int ZipReader::File_Count() const {
    if (!impl_) return 0;
    return (int)mz_zip_reader_get_num_files(static_cast<mz_zip_archive*>(impl_));
}

const char* ZipReader::Get_Filename(int index) const {
    if (!impl_) return nullptr;
    mz_zip_archive_file_stat stat;
    if (!mz_zip_reader_file_stat(static_cast<mz_zip_archive*>(impl_), index, &stat))
        return nullptr;
    if (index < 0) return nullptr;
    if (static_cast<size_t>(index) >= filenames_.size()) {
        filenames_.resize(static_cast<size_t>(index) + 1);
    }
    filenames_[static_cast<size_t>(index)] = stat.m_filename;
    return filenames_[static_cast<size_t>(index)].c_str();
}

int ZipReader::Find(const char* filename) const {
    if (!impl_) return -1;
    return mz_zip_reader_locate_file(static_cast<mz_zip_archive*>(impl_), filename, nullptr, 0);
}

size_t ZipReader::File_Size(int index) const {
    if (!impl_) return 0;
    mz_zip_archive_file_stat stat;
    if (!mz_zip_reader_file_stat(static_cast<mz_zip_archive*>(impl_), index, &stat))
        return 0;
    return stat.m_uncomp_size;
}

void* ZipReader::Extract(int index, size_t* size_out) const {
    if (!impl_) return nullptr;
    return mz_zip_reader_extract_to_heap(
        static_cast<mz_zip_archive*>(impl_), index, size_out, 0);
}

void* ZipReader::Extract(const char* filename, size_t* size_out) const {
    int idx = Find(filename);
    if (idx < 0) return nullptr;
    return Extract(idx, size_out);
}
