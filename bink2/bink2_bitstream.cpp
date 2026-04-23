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

    // Byte-wise LSB-first read. For count <= 24 this resolves to at most
    // one unaligned 32-bit load + shift + mask. count == 32 takes two
    // halves so the shift never overflows.
    const size_t start_bit  = bit_pos_;
    size_t       byte_index = start_bit >> 3;
    const uint32_t bit_index = (uint32_t)(start_bit & 7u);
    const size_t total_bytes = size_bits_ >> 3;

    if (count <= 24u) {
        uint32_t buf = 0;
        size_t bytes_avail = (total_bytes > byte_index) ? (total_bytes - byte_index) : 0;
        if (bytes_avail > 4) bytes_avail = 4;
        for (size_t i = 0; i < bytes_avail; ++i) {
            buf |= (uint32_t)data_[byte_index + i] << (i * 8u);
        }
        // count <= 24 here, so (1u << count) never overflows.
        const uint32_t mask = (1u << count) - 1u;
        value = (buf >> bit_index) & mask;
    } else {
        // count in (24, 32]. Read in two halves to keep intermediates in 32 bits.
        uint32_t lo = 0;
        uint32_t hi = 0;
        const uint32_t lo_count = 16u;
        const uint32_t hi_count = count - lo_count;

        // Low half: 16 bits starting at bit_pos_.
        {
            uint32_t buf = 0;
            size_t bytes_avail = (total_bytes > byte_index) ? (total_bytes - byte_index) : 0;
            if (bytes_avail > 4) bytes_avail = 4;
            for (size_t i = 0; i < bytes_avail; ++i) {
                buf |= (uint32_t)data_[byte_index + i] << (i * 8u);
            }
            lo = (buf >> bit_index) & 0xffffu;
        }

        // High half: hi_count bits starting at bit_pos_ + 16.
        const size_t hi_start_bit = start_bit + lo_count;
        size_t hi_byte_index = hi_start_bit >> 3;
        const uint32_t hi_bit_index = (uint32_t)(hi_start_bit & 7u);
        {
            uint32_t buf = 0;
            size_t bytes_avail = (total_bytes > hi_byte_index) ? (total_bytes - hi_byte_index) : 0;
            if (bytes_avail > 4) bytes_avail = 4;
            for (size_t i = 0; i < bytes_avail; ++i) {
                buf |= (uint32_t)data_[hi_byte_index + i] << (i * 8u);
            }
            // hi_count = count - 16, and count <= 32, so hi_count <= 16.
            const uint32_t mask = (1u << hi_count) - 1u;
            hi = (buf >> hi_bit_index) & mask;
        }

        value = lo | (hi << lo_count);
    }

    bit_pos_ += count;
    return true;
}

bool Bink2BitReader::Peek_Bits_LSB(uint32_t count, uint32_t& value) const
{
    value = 0;
    if (count > 24u) return false;
    if (Bits_Left() < count) return false;

    size_t byte_index = bit_pos_ >> 3;
    const uint32_t bit_index = (uint32_t)(bit_pos_ & 7u);
    const size_t total_bytes = size_bits_ >> 3;

    uint32_t buf = 0;
    size_t bytes_avail = (total_bytes > byte_index) ? (total_bytes - byte_index) : 0;
    if (bytes_avail > 4) bytes_avail = 4;
    for (size_t i = 0; i < bytes_avail; ++i) {
        buf |= (uint32_t)data_[byte_index + i] << (i * 8u);
    }
    const uint32_t mask = (1u << count) - 1u;
    value = (buf >> bit_index) & mask;
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
