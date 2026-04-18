/*
 * bink2_audio.h - Bink audio decoder (DCT / RDFT variants).
 *
 * Implements the in-packet audio block decode used by Bink 2 (and inherited
 * from legacy Bink): quantized band coefficients + inverse transform (DCT-III
 * for the DCT variant, real IDFT for the RDFT variant) + 1/16 lapped overlap
 * with the previous block. Matches FFmpeg's `libavcodec/binkaudio.c`.
 */

#ifndef BINK2_AUDIO_H
#define BINK2_AUDIO_H

#include "bink2_bitstream.h"

#include <array>
#include <cstdint>
#include <vector>

struct Bink2AudioDecoder {
    // Transform kind. DCT is signalled by `Bink2AudioTrack::Use_DCT()`.
    enum class Kind : uint8_t { DCT, RDFT };

    bool     ready        = false;
    Kind     kind         = Kind::DCT;
    uint32_t sample_rate  = 0;      // Container-level Hz (post RDFT-interleave).
    uint32_t channels     = 1;
    uint32_t frame_len    = 0;      // Transform length (samples per channel).
    uint32_t overlap_len  = 0;      // frame_len / 16.
    uint32_t block_size   = 0;      // (frame_len - overlap_len) * channels.
    uint32_t num_bands    = 0;
    bool     version_b    = false;  // True for revision 'b' files.
    bool     first_block  = true;
    float    root         = 0.f;
    std::vector<uint32_t> bands;                    // num_bands + 1 entries.
    std::array<float, 96> quant_table{};
    std::vector<std::vector<float>> previous;       // per-channel overlap tail.
    std::vector<std::vector<float>> scratch_coeffs; // working buffer per ch.
};

// Initialises the decoder for a given audio track. `sample_rate` is the
// container-level Hz (for stereo RDFT, pass the stereo rate as-is: the
// decoder handles the interleave-scale internally). `use_dct` selects the
// DCT variant; `stereo` must match the audio track. `version_b` should be
// true only for Bink files whose revision nibble is 'b' (older FFmpeg
// float-encoding quirk). Returns false if the parameters are unsupported.
bool Bink2AudioInit(Bink2AudioDecoder& d,
                    uint32_t sample_rate,
                    bool use_dct,
                    bool stereo,
                    bool version_b);

// Decodes one audio block from `bits`. Writes `d.block_size /
// d.channels` samples into `out[ch]`. Floats are in the nominal
// [-1.0, 1.0] range; FFmpeg calls this output "fltp" (planar float).
// On success returns true and advances `bits`.
bool Bink2AudioDecodeBlock(Bink2AudioDecoder& d,
                           Bink2BitReader& bits,
                           std::vector<std::vector<float>>& out);

// Convenience: decode every block available in `packet` (which is the
// raw audio sub-packet including the leading u32 reported-size). Skips
// the reported-size u32 on entry. Appends all decoded samples to
// `out[ch]`. Returns true on clean end-of-packet.
bool Bink2AudioDecodePacket(Bink2AudioDecoder& d,
                            const std::vector<uint8_t>& packet,
                            std::vector<std::vector<float>>& out);

#endif /* BINK2_AUDIO_H */
