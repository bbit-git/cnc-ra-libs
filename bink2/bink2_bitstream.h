/*
 * bink2_bitstream.h - Little-endian bit reader helpers for BK2 packets.
 */

#ifndef BINK2_BITSTREAM_H
#define BINK2_BITSTREAM_H

#include <cstddef>
#include <cstdint>

class Bink2BitReader {
public:
    Bink2BitReader();

    bool Reset(const uint8_t* data, size_t size_bytes);

    size_t Bits_Read() const;
    size_t Bits_Left() const;

    bool Read_Bit(uint32_t& value);
    bool Read_Bits(uint32_t count, uint32_t& value);
    bool Read_Unary(uint32_t stop_bit, uint32_t max_value, uint32_t& value);
    bool Skip_Bits(size_t count);
    void Align_Byte();

private:
    const uint8_t* data_;
    size_t         size_bits_;
    size_t         bit_pos_;
};

#endif /* BINK2_BITSTREAM_H */
