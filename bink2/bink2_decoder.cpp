/*
 * bink2_decoder.cpp - Early in-tree Bink 2 container / packet parser.
 */

#include "bink2_decoder.h"

#include <algorithm>
#include <cstring>

namespace {

static const uint8_t kKb2hNumSlices[4] = { 2, 3, 4, 8 };
static const uint32_t kBink2HeaderSize = 48;

} // namespace

bool Bink2Header::Is_Valid() const
{
    return signature[0] == 'K' &&
           signature[1] == 'B' &&
           signature[2] == '2' &&
           Revision() >= 'a' &&
           frame_count > 0 &&
           frame_count == frame_count_again &&
           width > 0 &&
           height > 0 &&
           Declared_File_Size() >= kBink2HeaderSize;
}

char Bink2Header::Revision() const
{
    return signature[3];
}

uint32_t Bink2Header::Declared_File_Size() const
{
    return file_size_minus_8 + 8u;
}

bool Bink2PacketHeader::Has_Row_Block_Flags() const
{
    return (frame_flags & 0x8000u) != 0;
}

bool Bink2PacketHeader::Has_Column_Block_Flags() const
{
    return (frame_flags & 0x4000u) != 0;
}

Bink2Decoder::Bink2Decoder()
    : fp_(nullptr)
{
}

Bink2Decoder::~Bink2Decoder()
{
    Close();
}

bool Bink2Decoder::Open(const char* path)
{
    Close();

    if (!path || !path[0]) return false;

    fp_ = std::fopen(path, "rb");
    if (!fp_) return false;

    if (!Parse_Header()) {
        Close();
        return false;
    }

    if (header_.audio_track_count == 0 && !Parse_Video_Only_Index()) {
        Close();
        return false;
    }

    return true;
}

void Bink2Decoder::Close()
{
    if (fp_) {
        std::fclose(fp_);
        fp_ = nullptr;
    }
    header_ = {};
    frame_index_.clear();
}

bool Bink2Decoder::Is_Open() const
{
    return fp_ != nullptr;
}

bool Bink2Decoder::Supports_Video_Only() const
{
    return Is_Open() && header_.audio_track_count == 0 && frame_index_.size() == (size_t)header_.frame_count + 1;
}

const Bink2Header& Bink2Decoder::Header() const
{
    return header_;
}

size_t Bink2Decoder::Frame_Index_Count() const
{
    return frame_index_.size();
}

const Bink2FrameIndexEntry* Bink2Decoder::Frame_Entry(size_t index) const
{
    if (index >= frame_index_.size()) return nullptr;
    return &frame_index_[index];
}

bool Bink2Decoder::Read_Video_Packet(size_t frame_index, std::vector<uint8_t>& packet) const
{
    packet.clear();

    if (!Supports_Video_Only()) return false;
    if (frame_index >= (size_t)header_.frame_count) return false;

    uint32_t start = frame_index_[frame_index].file_offset;
    uint32_t end = Frame_End_Offset(frame_index);
    if (start >= end) return false;
    if (end > header_.Declared_File_Size()) return false;

    packet.resize(end - start);
    if (!Read_At(start, packet.data(), packet.size())) {
        packet.clear();
        return false;
    }

    return true;
}

bool Bink2Decoder::Parse_Packet_Header(const std::vector<uint8_t>& packet,
                                       Bink2PacketHeader& header) const
{
    header = {};

    if (!Is_Open()) return false;
    if (packet.size() < 4) return false;

    char revision = this->header_.Revision();
    uint32_t num_slices = 1;

    if (revision == 'f' || revision == 'g') {
        num_slices = (this->header_.video_flags & 1u) ? 2u : 1u;
    } else if (revision > 'g') {
        num_slices = kKb2hNumSlices[this->header_.video_flags & 3u];
    }

    if (num_slices == 0 || num_slices > header.slice_offsets.size()) return false;
    if (packet.size() < num_slices * sizeof(uint32_t)) return false;

    header.frame_flags = Read_LE32(packet.data());
    header.num_slices = num_slices;
    header.slice_offsets[0] = 0;

    uint32_t prev = 0;
    for (uint32_t i = 1; i < num_slices; ++i) {
        uint32_t off = Read_LE32(packet.data() + i * sizeof(uint32_t));
        if (off < prev || off >= packet.size()) return false;
        header.slice_offsets[i] = off;
        prev = off;
    }

    uint32_t aligned_height = Align32(this->header_.height);
    if (num_slices == 1) {
        header.slice_heights[0] = aligned_height;
        return true;
    }

    if (revision > 'g') {
        uint32_t start = 0;
        uint32_t end = aligned_height + 64u - 1u;
        for (uint32_t i = 0; i < num_slices - 1; ++i) {
            start += ((end - start) / (num_slices - i)) & ~31u;
            end -= 32u;
            header.slice_heights[i] = start;
        }
        header.slice_heights[num_slices - 1] = aligned_height;
    } else {
        for (uint32_t i = 0; i < num_slices; ++i) {
            header.slice_heights[i] = Align32(((i + 1) * aligned_height) / num_slices);
        }
    }

    return true;
}

bool Bink2Decoder::Parse_Header()
{
    uint8_t raw[kBink2HeaderSize];
    if (!Read_At(0, raw, sizeof(raw))) return false;

    std::memcpy(header_.signature, raw + 0, 4);
    header_.file_size_minus_8 = Read_LE32(raw + 4);
    header_.frame_count = Read_LE32(raw + 8);
    header_.max_frame_size = Read_LE32(raw + 12);
    header_.frame_count_again = Read_LE32(raw + 16);
    header_.width = Read_LE32(raw + 20);
    header_.height = Read_LE32(raw + 24);
    header_.fps_num = Read_LE32(raw + 28);
    header_.fps_den = Read_LE32(raw + 32);
    header_.video_flags = Read_LE32(raw + 36);
    header_.audio_track_count = Read_LE32(raw + 40);
    header_.secondary_flags = Read_LE32(raw + 44);

    return header_.Is_Valid();
}

bool Bink2Decoder::Parse_Video_Only_Index()
{
    frame_index_.clear();

    const size_t count = (size_t)header_.frame_count + 1;
    std::vector<uint8_t> raw(count * sizeof(uint32_t));
    if (!Read_At(kBink2HeaderSize, raw.data(), raw.size())) return false;

    frame_index_.resize(count);
    uint32_t prev = 0;
    for (size_t i = 0; i < count; ++i) {
        uint32_t value = Read_LE32(raw.data() + i * sizeof(uint32_t));
        uint32_t offset = value & ~1u;
        bool keyframe = (value & 1u) != 0;

        if (offset < kBink2HeaderSize + raw.size()) return false;
        if (offset > header_.Declared_File_Size()) return false;
        if (i > 0 && offset < prev) return false;

        frame_index_[i].raw_value = value;
        frame_index_[i].file_offset = offset;
        frame_index_[i].keyframe = keyframe;
        prev = offset;
    }

    return true;
}

bool Bink2Decoder::Read_At(uint32_t offset, void* buffer, size_t size) const
{
    if (!fp_ || !buffer) return false;
    if (std::fseek(fp_, (long)offset, SEEK_SET) != 0) return false;
    return std::fread(buffer, 1, size, fp_) == size;
}

uint32_t Bink2Decoder::Frame_End_Offset(size_t frame_index) const
{
    if (!Supports_Video_Only()) return 0;
    if (frame_index + 1 >= frame_index_.size()) return header_.Declared_File_Size();
    return frame_index_[frame_index + 1].file_offset;
}

uint32_t Bink2Decoder::Read_LE32(const uint8_t* data)
{
    return ((uint32_t)data[0]) |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

uint32_t Bink2Decoder::Align32(uint32_t value)
{
    return (value + 31u) & ~31u;
}
