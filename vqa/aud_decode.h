/*
 * AUD/ADPCM Decoder -- Westwood AUD file format and IMA ADPCM codec.
 * Platform-independent: decodes compressed audio to signed 16-bit PCM.
 *
 * Based on ADPCM.CPP, SOSCOMP.H, AUDIO.H from the CnC Remastered
 * Collection (GPL3, Electronic Arts / Westwood Studios).
 */

#ifndef AUD_DECODE_H
#define AUD_DECODE_H

#include <cstdint>

/* ---- AUD file header (10 bytes, packed) ---- */
#pragma pack(push, 1)
struct AUDHeaderType {
    uint16_t Rate;           /* Playback rate in Hz */
    uint32_t Size;           /* Compressed data size (bytes) */
    uint32_t UncompSize;     /* Uncompressed data size (bytes) */
    uint8_t  Flags;          /* bit 0 = stereo, bit 1 = 16-bit */
    uint8_t  Compression;    /* 0 = none, 1 = WS ADPCM, 2 = IMA ADPCM */
};
#pragma pack(pop)

#define AUD_FLAG_STEREO 1
#define AUD_FLAG_16BIT  2

#define AUD_COMP_NONE       0
#define AUD_COMP_WS_ADPCM   1
#define AUD_COMP_IMA_ADPCM  2
#define AUD_COMP_SOS_CODEC  99  /* SOS CODEC — same as IMA ADPCM */

/* ---- ADPCM codec state ---- */
struct ADPCMState {
    unsigned long index;     /* Current index into step/diff table */
    long          predicted; /* Current predicted sample value */
};

/*
 * Initialize ADPCM state for a new stream.
 */
void ADPCM_Init(ADPCMState* state);

/*
 * Decompress IMA ADPCM data (Westwood variant).
 *
 * src         -- compressed data (4-bit nibbles, 2 per byte)
 * dst         -- output buffer (signed 16-bit PCM)
 * num_bytes   -- number of compressed bytes to process
 * state       -- codec state (updated in place)
 *
 * Each input byte produces 2 output samples.
 * Returns number of samples written (= num_bytes * 2).
 */
int ADPCM_Decode(const uint8_t* src, int16_t* dst, int num_bytes,
                 ADPCMState* state);

/*
 * Parse an AUD header from raw data.
 * Returns pointer to audio data (past header), or nullptr on failure.
 * Fills in header struct.
 */
const uint8_t* AUD_Parse_Header(const void* data, int data_size,
                                AUDHeaderType* header);

/*
 * Decode an entire AUD sample from memory to signed 16-bit PCM.
 *
 * aud_data    -- pointer to start of AUD data (including header)
 * aud_size    -- total size of AUD data
 * out_pcm     -- output buffer (caller allocates)
 * out_max     -- max samples in output buffer
 * out_rate    -- receives sample rate
 * out_channels -- receives channel count
 *
 * Returns number of samples written, or 0 on failure.
 */
int AUD_Decode_Memory(const void* aud_data, int aud_size,
                      int16_t* out_pcm, int out_max,
                      int* out_rate, int* out_channels);

#endif /* AUD_DECODE_H */
