/*
 * bink2_video.h - Early packet-to-frame planning helpers for BK2 video decode.
 */

#ifndef BINK2_VIDEO_H
#define BINK2_VIDEO_H

#include "bink2_decoder.h"

#include <cstddef>
#include <cstdint>
#include <array>
#include <vector>

struct Bink2FramePlan {
    Bink2PacketHeader     packet = {};
    uint32_t              aligned_width = 0;
    uint32_t              aligned_height = 0;
    uint32_t              row_flag_count = 0;
    uint32_t              col_flag_count = 0;
    bool                  has_global_block_flags = false;
    bool                  has_row_flag_stream = false;
    bool                  has_col_flag_stream = false;
    size_t                payload_bit_offset = 0;
    std::vector<uint8_t>  row_flags;
    std::vector<uint8_t>  col_flags;
};

struct Bink2PackedFlagRun {
    uint32_t bit_begin = 0;
    uint32_t bit_end = 0;
    bool     value = false;
};

struct Bink2PackedFlagGroup {
    uint32_t unit_begin = 0;
    uint32_t unit_end = 0;
    uint32_t pixel_begin = 0;
    uint32_t pixel_end = 0;
    uint32_t macroblock_begin = 0;
    uint32_t macroblock_end = 0;
};

struct Bink2MacroblockCoverageLine {
    uint32_t line_index = 0;
    uint32_t leading_decoded = 0;
    uint32_t total_decoded = 0;
};

struct Bink2PackedFlagGroupCoverage {
    Bink2PackedFlagGroup group = {};
    uint32_t             line_begin = 0;
    uint32_t             line_end = 0;
    uint32_t             decoded_total = 0;
    uint32_t             decoded_capacity = 0;
    uint32_t             min_leading_decoded = 0;
    uint32_t             max_leading_decoded = 0;
    uint32_t             full_lines = 0;
    uint32_t             partial_lines = 0;
    bool                 contains_stop_line = false;
};

struct Bink2IntraMacroblockProbe {
    uint32_t                  mb_x = 0;
    uint32_t                  mb_y = 0;
    uint32_t                  flags = 0;
    bool                      has_alpha = false;
    int32_t                   intra_q = 0;
    uint32_t                  y_cbp = 0;
    uint32_t                  u_cbp = 0;
    uint32_t                  v_cbp = 0;
    uint32_t                  a_cbp = 0;
    size_t                    bit_offset_before = 0;
    size_t                    bit_offset_after_q = 0;
    size_t                    bit_offset_after_y = 0;
    size_t                    bit_offset_after_u = 0;
    size_t                    bit_offset_after_v = 0;
    size_t                    bit_offset_after_a = 0;
    size_t                    bit_offset_after_coeffs = 0;
    std::array<int32_t, 16>   y_dc = {};
    std::array<int32_t, 4>    u_dc = {};
    std::array<int32_t, 4>    v_dc = {};
    std::array<int32_t, 16>   a_dc = {};
};

struct Bink2MacroblockPixels {
    bool                      has_alpha = false;
    std::array<uint8_t, 32 * 32> y = {};
    std::array<uint8_t, 16 * 16> u = {};
    std::array<uint8_t, 16 * 16> v = {};
    std::array<uint8_t, 32 * 32> a = {};
};

struct Bink2DecodedMacroblock {
    Bink2IntraMacroblockProbe                    probe = {};
    Bink2MacroblockPixels                        pixels = {};
    std::array<std::array<int16_t, 64>, 16>     y_blocks = {};
    std::array<std::array<int16_t, 64>, 4>      u_blocks = {};
    std::array<std::array<int16_t, 64>, 4>      v_blocks = {};
    std::array<std::array<int16_t, 64>, 16>     a_blocks = {};
};

struct Bink2DecodedKeyframeSlice {
    bool                 has_alpha = false;
    bool                 complete = false;
    uint32_t             slice_index = 0;
    uint32_t             slice_y = 0;
    uint32_t             luma_width = 0;
    uint32_t             luma_height = 0;
    uint32_t             chroma_width = 0;
    uint32_t             chroma_height = 0;
    uint32_t             luma_stride = 0;
    uint32_t             chroma_stride = 0;
    uint32_t             macroblock_cols = 0;
    uint32_t             macroblock_rows = 0;
    uint32_t             macroblock_stride = 0;
    uint32_t             macroblock_count = 0;
    uint32_t             nonzero_macroblock_count = 0;
    uint32_t             decode_mb_col = 0;
    uint32_t             decode_mb_row = 0;
    uint32_t             failure_stage = 0;
    size_t               bit_offset_begin = 0;
    size_t               bit_offset_end = 0;
    std::vector<uint8_t> decoded_macroblocks;
    std::vector<uint8_t> y;
    std::vector<uint8_t> u;
    std::vector<uint8_t> v;
    std::vector<uint8_t> a;
};

struct Bink2DecodedFrame {
    bool                 has_alpha = false;
    bool                 complete = false;
    uint32_t             width = 0;
    uint32_t             height = 0;
    uint32_t             aligned_width = 0;
    uint32_t             aligned_height = 0;
    uint32_t             luma_stride = 0;
    uint32_t             chroma_stride = 0;
    uint32_t             macroblock_cols = 0;
    uint32_t             macroblock_rows = 0;
    uint32_t             macroblock_stride = 0;
    uint32_t             decoded_slice_count = 0;
    uint32_t             macroblock_count = 0;
    uint32_t             nonzero_macroblock_count = 0;
    uint32_t             decode_mb_col = 0;
    uint32_t             decode_mb_row = 0;
    uint32_t             failure_stage = 0;
    size_t               bit_offset_begin = 0;
    size_t               bit_offset_end = 0;
    std::vector<uint8_t> decoded_macroblocks;
    std::vector<uint8_t> y;
    std::vector<uint8_t> u;
    std::vector<uint8_t> v;
    std::vector<uint8_t> a;
};

struct Bink2MacroblockBitBudget {
    uint32_t mb_col = 0;
    uint32_t mb_row = 0;
    uint32_t flags = 0;
    int32_t  intra_q = 0;
    bool     has_alpha = false;
    uint32_t y_cbp = 0;
    uint32_t u_cbp = 0;
    uint32_t v_cbp = 0;
    uint32_t a_cbp = 0;

    size_t   bit_offset_before = 0;
    size_t   bit_offset_after = 0;
    size_t   bits_total = 0;

    size_t   bits_dq = 0;
    size_t   bits_y_cbp = 0;
    size_t   bits_y_dc = 0;
    size_t   bits_u_cbp = 0;
    size_t   bits_u_dc = 0;
    size_t   bits_v_cbp = 0;
    size_t   bits_v_dc = 0;
    size_t   bits_a_cbp = 0;
    size_t   bits_a_dc = 0;

    std::array<size_t, 16> bits_y_ac = {};
    std::array<size_t, 4>  bits_u_ac = {};
    std::array<size_t, 4>  bits_v_ac = {};
    std::array<size_t, 16> bits_a_ac = {};
    size_t   bits_y_ac_total = 0;
    size_t   bits_u_ac_total = 0;
    size_t   bits_v_ac_total = 0;
    size_t   bits_a_ac_total = 0;
};

struct Bink2DcDeltaTraceEntry {
    uint32_t index = 0;
    size_t   bit_offset_start = 0;
    uint32_t unary = 0;
    size_t   bit_offset_after_unary = 0;
    bool     has_extra = false;
    uint32_t extra_bits = 0;
    uint32_t extra_value = 0;
    size_t   bit_offset_after_extra = 0;
    bool     has_sign = false;
    uint32_t sign_bit = 0;
    size_t   bit_offset_after_sign = 0;
    int32_t  delta = 0;
    int32_t  tdc = 0;
};

struct Bink2DcDeltaTrace {
    int32_t intra_q = 0;
    int32_t pat = 0;
    bool    has_delta_bit_present = false;
    uint32_t has_delta_value = 0;
    size_t  bit_offset_begin = 0;
    size_t  bit_offset_end = 0;
    std::vector<Bink2DcDeltaTraceEntry> entries;
};

struct Bink2KeyframeMacroblockTraceEntry {
    Bink2IntraMacroblockProbe probe = {};
    size_t                    bits_used = 0;
    bool                      has_nonzero_cbp = false;
};

struct Bink2KeyframeMacroblockTrace {
    bool                                      has_alpha = false;
    bool                                      complete = false;
    uint32_t                                  macroblock_cols = 0;
    uint32_t                                  macroblock_rows = 0;
    uint32_t                                  macroblock_count = 0;
    uint32_t                                  nonzero_macroblock_count = 0;
    uint32_t                                  decode_mb_col = 0;
    uint32_t                                  decode_mb_row = 0;
    uint32_t                                  failure_stage = 0;
    size_t                                    bit_offset_begin = 0;
    size_t                                    bit_offset_end = 0;
    std::vector<Bink2KeyframeMacroblockTraceEntry> entries;
};

bool Bink2PrepareFramePlan(const Bink2Header& header,
                           const Bink2PacketHeader& packet_header,
                           const std::vector<uint8_t>& packet,
                           Bink2FramePlan& plan);

bool Bink2DecodeFirstKeyframeSlicePrefix(const Bink2Header& header,
                                         const Bink2FramePlan& plan,
                                         const std::vector<uint8_t>& packet,
                                         Bink2DecodedKeyframeSlice& slice);

bool Bink2DecodeFirstKeyframeFramePrefix(const Bink2Header& header,
                                         const Bink2FramePlan& plan,
                                         const std::vector<uint8_t>& packet,
                                         Bink2DecodedFrame& frame);

// Walks an inter-frame packet decoding only SKIP blocks (copies from previous
// frame). Stops at the first non-SKIP block and reports how many SKIPs were
// decoded. Produces a partial frame buffer; non-decoded MBs are left neutral.
// Returns true if at least the bitstream positioning succeeded.
bool Bink2DecodeInterFrameSkipPrefix(const Bink2Header& header,
                                     const Bink2FramePlan& plan,
                                     const std::vector<uint8_t>& packet,
                                     const Bink2DecodedFrame& prev_frame,
                                     Bink2DecodedFrame& frame);

bool Bink2ProbeMacroblockBitBudget(const Bink2Header& header,
                                   const Bink2FramePlan& plan,
                                   const std::vector<uint8_t>& packet,
                                   uint32_t target_mb_col,
                                   uint32_t target_mb_row,
                                   Bink2MacroblockBitBudget& budget);

bool Bink2ProbeMacroblockBitBudgetInterleaved(const Bink2Header& header,
                                              const Bink2FramePlan& plan,
                                              const std::vector<uint8_t>& packet,
                                              uint32_t target_mb_col,
                                              uint32_t target_mb_row,
                                              Bink2MacroblockBitBudget& budget);

bool Bink2TraceDcDeltasForMacroblock(const Bink2Header& header,
                                     const Bink2FramePlan& plan,
                                     const std::vector<uint8_t>& packet,
                                     uint32_t target_mb_col,
                                     uint32_t target_mb_row,
                                     Bink2DcDeltaTrace& y_trace,
                                     Bink2DcDeltaTrace& u_trace,
                                     Bink2DcDeltaTrace& v_trace,
                                     Bink2DcDeltaTrace& a_trace);

bool Bink2TraceFirstKeyframeSlicePrefix(const Bink2Header& header,
                                        const Bink2FramePlan& plan,
                                        const std::vector<uint8_t>& packet,
                                        Bink2KeyframeMacroblockTrace& trace);

bool Bink2ProbeFirstKeyframeMacroblock(const Bink2Header& header,
                                       const Bink2FramePlan& plan,
                                       const std::vector<uint8_t>& packet,
                                       Bink2IntraMacroblockProbe& probe);

bool Bink2FindFirstNonZeroCbpKeyframeMacroblock(const Bink2Header& header,
                                                const Bink2FramePlan& plan,
                                                const std::vector<uint8_t>& packet,
                                                Bink2IntraMacroblockProbe& probe);

bool Bink2DecodeFirstNonZeroCbpKeyframeMacroblock(const Bink2Header& header,
                                                  const Bink2FramePlan& plan,
                                                  const std::vector<uint8_t>& packet,
                                                  Bink2DecodedMacroblock& macroblock);

bool Bink2ReconstructFirstMacroblockDcOnly(const Bink2IntraMacroblockProbe& probe,
                                           Bink2MacroblockPixels& pixels);

bool Bink2ReadPackedFlagBit(const std::vector<uint8_t>& flags,
                            uint32_t bit_index,
                            bool& value);

bool Bink2ExpandPackedFlagBits(const std::vector<uint8_t>& flags,
                               uint32_t bit_count,
                               std::vector<uint8_t>& bits);

bool Bink2BuildPackedFlagRuns(const std::vector<uint8_t>& flags,
                              uint32_t bit_count,
                              std::vector<Bink2PackedFlagRun>& runs);

bool Bink2BuildPackedFlagGroups(const std::vector<uint8_t>& flags,
                                uint32_t bit_count,
                                bool split_on_set_boundary,
                                uint32_t unit_pixels,
                                std::vector<Bink2PackedFlagGroup>& groups);

bool Bink2BuildMacroblockRowCoverage(const Bink2DecodedFrame& frame,
                                     std::vector<Bink2MacroblockCoverageLine>& lines);

bool Bink2BuildMacroblockColumnCoverage(const Bink2DecodedFrame& frame,
                                        std::vector<Bink2MacroblockCoverageLine>& lines);

bool Bink2BuildPackedFlagGroupCoverage(const std::vector<Bink2PackedFlagGroup>& groups,
                                       const std::vector<Bink2MacroblockCoverageLine>& lines,
                                       uint32_t line_capacity,
                                       uint32_t stop_line_index,
                                       std::vector<Bink2PackedFlagGroupCoverage>& coverage);

size_t Bink2CountSetFlagBits(const std::vector<uint8_t>& flags,
                             uint32_t bit_count);

#endif /* BINK2_VIDEO_H */
