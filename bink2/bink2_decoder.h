/*
 * bink2_decoder.h - Early in-tree Bink 2 container / packet parser.
 *
 * This is the first step toward replacing the external BinkPlayer shim
 * with a real decoder. The current implementation is intentionally narrow:
 * it fully supports silent KB2j files for frame packet extraction and
 * packet-preamble parsing, and it exposes enough state to build scalar
 * macroblock decode on top.
 */

#ifndef BINK2_DECODER_H
#define BINK2_DECODER_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

struct Bink2Header {
    char     signature[4] = {};
    uint32_t file_size_minus_8 = 0;
    uint32_t frame_count = 0;
    uint32_t max_frame_size = 0;
    uint32_t frame_count_again = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t fps_num = 0;
    uint32_t fps_den = 0;
    uint32_t video_flags = 0;
    uint32_t audio_track_count = 0;
    uint32_t secondary_flags = 0;

    bool     Is_Valid() const;
    char     Revision() const;
    uint32_t Declared_File_Size() const;
    bool     Has_Alpha() const;
};

struct Bink2FrameIndexEntry {
    uint32_t raw_value = 0;
    uint32_t file_offset = 0;
    bool     keyframe = false;
};

struct Bink2PacketHeader {
    uint32_t frame_flags = 0;
    uint32_t num_slices = 0;
    std::array<uint32_t, 8> slice_offsets = {};
    std::array<uint32_t, 8> slice_heights = {};

    bool Has_Row_Block_Flags() const;
    bool Has_Column_Block_Flags() const;
};

class Bink2Decoder {
public:
    Bink2Decoder();
    ~Bink2Decoder();

    bool Open(const char* path);
    void Close();

    bool Is_Open() const;
    bool Supports_Video_Only() const;

    const Bink2Header& Header() const;
    size_t Frame_Index_Count() const;
    const Bink2FrameIndexEntry* Frame_Entry(size_t index) const;

    bool Read_Video_Packet(size_t frame_index, std::vector<uint8_t>& packet) const;
    bool Parse_Packet_Header(const std::vector<uint8_t>& packet, Bink2PacketHeader& header) const;

private:
    bool Parse_Header();
    bool Parse_Video_Only_Index();
    bool Read_At(uint32_t offset, void* buffer, size_t size) const;
    uint32_t Frame_End_Offset(size_t frame_index) const;

    static uint32_t Read_LE32(const uint8_t* data);
    static uint32_t Align32(uint32_t value);

private:
    std::FILE*                    fp_;
    Bink2Header                   header_;
    std::vector<Bink2FrameIndexEntry> frame_index_;
};

#endif /* BINK2_DECODER_H */
