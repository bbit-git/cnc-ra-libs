/*
 * bink2_bitstream.cpp - Little-endian bit reader helpers for BK2 packets.
 */

#include "bink2_bitstream.h"

Bink2BitReader::Bink2BitReader()
    : data_(nullptr), size_bits_(0), bit_pos_(0)
{
}

bool Bink2BitReader::Reset(const uint8_t* data, size_t size_bytes)
{
    if (!data && size_bytes != 0) return false;

    data_ = data;
    size_bits_ = size_bytes * 8u;
    bit_pos_ = 0;
    return true;
}

size_t Bink2BitReader::Bits_Read() const
{
    return bit_pos_;
}

size_t Bink2BitReader::Bits_Left() const
{
    return (bit_pos_ <= size_bits_) ? (size_bits_ - bit_pos_) : 0u;
}

bool Bink2BitReader::Read_Bit(uint32_t& value)
{
    return Read_Bits(1u, value);
}

bool Bink2BitReader::Read_Bits(uint32_t count, uint32_t& value)
{
    value = 0;
    if (count > 32u) return false;
    if (Bits_Left() < count) return false;

    for (uint32_t i = 0; i < count; ++i) {
        size_t byte_index = (bit_pos_ + i) >> 3;
        uint32_t bit_index = (uint32_t)((bit_pos_ + i) & 7u);
        uint32_t bit = (uint32_t)((data_[byte_index] >> bit_index) & 1u);
        value |= bit << i;
    }

    bit_pos_ += count;
    return true;
}

bool Bink2BitReader::Read_Unary(uint32_t stop_bit, uint32_t max_value, uint32_t& value)
{
    value = 0;
    if (stop_bit > 1u) return false;

    while (value < max_value) {
        uint32_t bit = 0;
        if (!Read_Bit(bit)) return false;
        if (bit == stop_bit) return true;
        ++value;
    }

    return true;
}

bool Bink2BitReader::Skip_Bits(size_t count)
{
    if (Bits_Left() < count) return false;
    bit_pos_ += count;
    return true;
}

void Bink2BitReader::Align_Byte()
{
    bit_pos_ = (bit_pos_ + 7u) & ~size_t(7u);
    if (bit_pos_ > size_bits_) bit_pos_ = size_bits_;
}
