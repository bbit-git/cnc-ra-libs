/*
 * VQA Decoder — minimal Westwood VQA video format decoder
 * Format reference: CnC_Red_Alert/VQ/VQA32/VQAFILE.H, UNVQBUFF.ASM
 * Licensed under GPL3 (same as original Westwood source)
 */

#include "vqa_decoder.h"
#include "aud_decode.h"
#include "audio_renderer.h"
#include <cstring>
#include <cstdio>

/* Debug trace macro — matches td_platform.h definition */
#ifndef DBG
#ifdef DEBUG
#define DBG(fmt, ...) do { fprintf(stderr, fmt "\n", ##__VA_ARGS__); fflush(stderr); } while(0)
#else
#define DBG(fmt, ...) ((void)0)
#endif
#endif

/* LCW decompression (defined in td_stubs.cpp) */
extern unsigned long LCW_Uncompress(void const* source, void* dest, unsigned long length, unsigned long dest_max = 0);

/* Global video renderer — set by platform layer before playback */
static VideoRendererInterface* g_video_renderer = nullptr;
/* Global audio renderer — set by platform layer before playback */
static AudioRendererInterface* g_audio_renderer = nullptr;

void VQA_Set_Video_Renderer(VideoRendererInterface* renderer) {
    g_video_renderer = renderer;
}

void VQA_Set_Audio_Renderer(AudioRendererInterface* renderer) {
    g_audio_renderer = renderer;
}

/* ---- IFF chunk ID helpers ---- */
static uint32_t make_id(char a, char b, char c, char d) {
    return ((uint32_t)(unsigned char)a) |
           ((uint32_t)(unsigned char)b << 8) |
           ((uint32_t)(unsigned char)c << 16) |
           ((uint32_t)(unsigned char)d << 24);
}

static uint32_t swap32(uint32_t v) {
    return ((v >> 24) & 0xFF) | ((v >> 8) & 0xFF00) |
           ((v << 8) & 0xFF0000) | ((v << 24) & 0xFF000000);
}

/* Chunk IDs */
static const uint32_t ID_FORM = make_id('F','O','R','M');
static const uint32_t ID_WVQA = make_id('W','V','Q','A');
static const uint32_t ID_VQHD = make_id('V','Q','H','D');
static const uint32_t ID_FINF = make_id('F','I','N','F');
static const uint32_t ID_VQFR = make_id('V','Q','F','R');
static const uint32_t ID_VQFK = make_id('V','Q','F','K');
static const uint32_t ID_CBF0 = make_id('C','B','F','0');
static const uint32_t ID_CBFZ = make_id('C','B','F','Z');
static const uint32_t ID_CBP0 = make_id('C','B','P','0');
static const uint32_t ID_CBPZ = make_id('C','B','P','Z');
static const uint32_t ID_CPL0 = make_id('C','P','L','0');
static const uint32_t ID_CPLZ = make_id('C','P','L','Z');
static const uint32_t ID_VPT0 = make_id('V','P','T','0');
static const uint32_t ID_VPTZ = make_id('V','P','T','Z');
static const uint32_t ID_VPTR = make_id('V','P','T','R');
static const uint32_t ID_VPRZ = make_id('V','P','R','Z');
static const uint32_t ID_SND0 = make_id('S','N','D','0');
static const uint32_t ID_SND1 = make_id('S','N','D','1');
static const uint32_t ID_SND2 = make_id('S','N','D','2');

/* ---- Internal decoder state ---- */
struct VQADecoder {
    VQAIOHandler io;
    VQAHandle*   handle;
    VQAConfig    config;
    VQAHeader    header;

    /* Codebook buffer — sized for in-place LCW decompression.
     * Compressed data is read into the END of the buffer, then
     * decompressed in-place to the START (matching original Westwood approach). */
    uint8_t*     codebook;        /* current active codebook */
    int          max_cb_size;     /* buffer size (original: (CBentries*bw*bh+250)&~3) */

    /* Second codebook buffer for partial accumulation (ping-pong) */
    uint8_t*     cb_next;         /* next codebook being built from partial chunks */
    int          cb_next_compressed; /* 1 = contains compressed data, 0 = uncompressed */
    long         cb_next_offset;  /* offset where compressed data starts in cb_next */
    int          cb_partial_size; /* total compressed bytes accumulated */
    int          cb_parts_received;
    int          groupsize;

    /* Vector pointers — same in-place LCW approach */
    uint8_t*     pointers;
    int          max_ptr_size;    /* buffer size (original: (nblocks*2+1024)&~3) */

    /* Frame buffer: 8-bit indexed */
    uint8_t*     framebuf;
    int          fb_width;
    int          fb_height;

    /* Palette — same in-place LCW approach */
    uint8_t*     palette;
    int          max_pal_size;
    bool         palette_dirty;

    /* Audio */
    ADPCMState   audio_adpcm;
    int          audio_rate;
    int          audio_channels;
    int          audio_bits;
    bool         has_audio;

    /* Timing */
    int          fps;
    int          total_frames;
    int          current_frame;
    bool         stopped;
};

/* ---- Direct file I/O (bypasses engine's broken threaded MixFileHandler) ---- */

/* Forward-declare the CCFileClass we need */
class CCFileClass;
extern "C++" {
    /* These methods exist on CCFileClass via RawFileClass */
    int  ccfile_read(CCFileClass* f, void* buf, int nbytes);
    int  ccfile_seek(CCFileClass* f, long offset, int whence);
    bool ccfile_open_read(CCFileClass* f);
    void ccfile_close(CCFileClass* f);
    bool ccfile_is_available(CCFileClass* f);
}

/* Store the file handle directly in the decoder */
static long vqa_read(VQADecoder* dec, void* buf, long nbytes) {
    CCFileClass* f = (CCFileClass*)dec->handle->VQAio;
    if (!f) return -1;
    int got = ccfile_read(f, buf, nbytes);
    return (got == nbytes) ? 0 : -1;
}

static long vqa_seek(VQADecoder* dec, long offset) {
    CCFileClass* f = (CCFileClass*)dec->handle->VQAio;
    if (!f) return -1;
    ccfile_seek(f, offset, 1); /* SEEK_CUR */
    return 0;
}

/* Round up to even (IFF padding) */
static inline uint32_t padsize(uint32_t n) { return (n + 1) & ~1u; }

/* ---- 4x2 block decode (C port of UNVQBUFF.ASM UnVQ_4x2) ---- */
static void unvq_4x2(uint8_t* codebook, uint8_t* pointers,
                      uint8_t* buffer, int blocks_per_row,
                      int num_rows, int buf_width, int num_blocks) {
    uint8_t* dst = buffer;
    uint8_t* row_start = buffer;

    for (int row = 0; row < num_rows; row++) {
        dst = row_start;
        for (int col = 0; col < blocks_per_row; col++) {
            int idx = row * blocks_per_row + col;
            /* Pointer is split: low byte at [idx], high byte at [idx + num_blocks] */
            uint8_t lo = pointers[idx];
            uint8_t hi = pointers[idx + num_blocks];

            if (hi == 0x0F) {
                /* One-color block: fill 4x2 with color = lo */
                uint32_t fill = lo | (lo << 8) | (lo << 16) | (lo << 24);
                *(uint32_t*)(dst) = fill;
                *(uint32_t*)(dst + buf_width) = fill;
            } else {
                /* Multi-color block: 8 bytes from codebook */
                int cb_idx = (hi << 8) | lo;
                uint8_t* cb = codebook + cb_idx * 8;
                *(uint32_t*)(dst) = *(uint32_t*)(cb);
                *(uint32_t*)(dst + buf_width) = *(uint32_t*)(cb + 4);
            }
            dst += 4; /* next block (4 pixels wide) */
        }
        row_start += buf_width * 2; /* next row of blocks (2 pixels high) */
    }
}

/* ---- 4x4 block decode ---- */
static void unvq_4x4(uint8_t* codebook, uint8_t* pointers,
                      uint8_t* buffer, int blocks_per_row,
                      int num_rows, int buf_width, int num_blocks) {
    uint8_t* row_start = buffer;

    for (int row = 0; row < num_rows; row++) {
        uint8_t* dst = row_start;
        for (int col = 0; col < blocks_per_row; col++) {
            int idx = row * blocks_per_row + col;
            uint8_t lo = pointers[idx];
            uint8_t hi = pointers[idx + num_blocks];
            int cb_idx = (hi << 8) | lo;

            if (hi == 0xFF) {
                /* One-color block: fill 4x4 with color = lo */
                uint32_t fill = lo | (lo << 8) | (lo << 16) | (lo << 24);
                *(uint32_t*)(dst)                  = fill;
                *(uint32_t*)(dst + buf_width)      = fill;
                *(uint32_t*)(dst + buf_width * 2)  = fill;
                *(uint32_t*)(dst + buf_width * 3)  = fill;
            } else {
                /* Multi-color block: 16 bytes from codebook */
                uint8_t* cb = codebook + cb_idx * 16;
                *(uint32_t*)(dst)                  = *(uint32_t*)(cb);
                *(uint32_t*)(dst + buf_width)      = *(uint32_t*)(cb + 4);
                *(uint32_t*)(dst + buf_width * 2)  = *(uint32_t*)(cb + 8);
                *(uint32_t*)(dst + buf_width * 3)  = *(uint32_t*)(cb + 12);
            }
            dst += 4;
        }
        row_start += buf_width * 4; /* next row of blocks (4 pixels high) */
    }
}

/* ---- Parse VQA header ---- */
static bool vqa_parse_header(VQADecoder* dec) {
    ChunkHeader ch;

    /* Read FORM header */
    if (vqa_read(dec, &ch, 8) != 0) return false;
    if (ch.id != ID_FORM) {
        DBG("VQA: not a FORM file");
        return false;
    }

    /* Read WVQA type */
    uint32_t form_type;
    if (vqa_read(dec, &form_type, 4) != 0) return false;
    if (form_type != ID_WVQA) {
        DBG("VQA: not WVQA format");
        return false;
    }

    /* Read chunks until we find VQHD and FINF */
    bool got_header = false;
    while (!got_header) {
        if (vqa_read(dec, &ch, 8) != 0) return false;
        uint32_t chunk_size = swap32(ch.size);

        if (ch.id == ID_VQHD) {
            if (chunk_size > sizeof(VQAHeader)) chunk_size = sizeof(VQAHeader);
            if (vqa_read(dec, &dec->header, chunk_size) != 0) return false;
            /* Skip any extra header bytes */
            if (swap32(ch.size) > chunk_size)
                vqa_seek(dec, swap32(ch.size) - chunk_size);

            DBG("VQA: %dx%d, %d frames, %d fps, block %dx%d, %d cb entries, groupsize=%d",
                    dec->header.ImageWidth, dec->header.ImageHeight,
                    dec->header.Frames, dec->header.FPS,
                    dec->header.BlockWidth, dec->header.BlockHeight,
                    dec->header.CBentries, dec->header.Groupsize);
            got_header = true;
        } else if (ch.id == ID_FINF) {
            /* Skip frame info table — we read frames sequentially */
            vqa_seek(dec, chunk_size);
            /* Pad to even */
            if (chunk_size & 1) vqa_seek(dec, 1);
        } else {
            /* Skip unknown chunk */
            vqa_seek(dec, chunk_size);
            if (chunk_size & 1) vqa_seek(dec, 1);
        }
    }

    /* Skip FINF if it comes after VQHD */
    if (vqa_read(dec, &ch, 8) != 0) return false;
    uint32_t chunk_size = swap32(ch.size);
    if (ch.id == ID_FINF) {
        vqa_seek(dec, chunk_size);
        if (chunk_size & 1) vqa_seek(dec, 1);
    } else {
        /* Not FINF — seek back so frame reader can see this chunk */
        vqa_seek(dec, -8);
    }

    return true;
}

/* ---- Allocate decoder buffers ---- */
static bool vqa_alloc_buffers(VQADecoder* dec) {
    int bw = dec->header.BlockWidth;
    int bh = dec->header.BlockHeight;
    int iw = dec->header.ImageWidth;
    int ih = dec->header.ImageHeight;

    if (bw == 0) bw = 4;
    if (bh == 0) bh = 2;

    dec->fb_width = iw;
    dec->fb_height = ih;
    dec->fps = dec->header.FPS;
    dec->total_frames = dec->header.Frames;

    int blocks_x = iw / bw;
    int blocks_y = ih / bh;
    int num_blocks = blocks_x * blocks_y;

    /* Buffer sizes — match original Westwood VQA player formulas.
     * These include slack for in-place LCW decompression (compressed data
     * is read into the END of the buffer, then decompressed to the START). */
    dec->max_cb_size = ((dec->header.CBentries * bw * bh) + 250) & ~3;
    if (dec->max_cb_size < 0x4000) dec->max_cb_size = 0x4000; /* min 16KB */
    dec->max_ptr_size = (num_blocks * 2 + 1024) & ~3;
    dec->max_pal_size = (768 + 1024) & ~3;

    dec->codebook = (uint8_t*)calloc(1, dec->max_cb_size);
    dec->cb_next  = (uint8_t*)calloc(1, dec->max_cb_size);
    dec->cb_next_compressed = 0;
    dec->cb_next_offset = 0;
    dec->cb_partial_size = 0;
    dec->cb_parts_received = 0;
    dec->groupsize = dec->header.Groupsize ? dec->header.Groupsize : 8;

    dec->pointers = (uint8_t*)calloc(1, dec->max_ptr_size);

    dec->framebuf = (uint8_t*)calloc(1, iw * ih);

    dec->palette = (uint8_t*)calloc(1, dec->max_pal_size);

    if (!dec->codebook || !dec->cb_next || !dec->pointers || !dec->framebuf || !dec->palette) {
        DBG("VQA: allocation failed");
        return false;
    }

    memset(dec->palette, 0, 768);
    dec->palette_dirty = false;
    dec->current_frame = 0;
    dec->stopped = false;

    /* Audio setup from VQA header */
    dec->audio_rate = dec->header.SampleRate;
    dec->audio_channels = dec->header.Channels;
    dec->audio_bits = dec->header.BitsPerSample;
    dec->has_audio = (dec->audio_rate > 0 && dec->audio_channels > 0);
    ADPCM_Init(&dec->audio_adpcm);

    if (dec->has_audio) {
        DBG("VQA: audio %d Hz, %d ch, %d bit",
                dec->audio_rate, dec->audio_channels, dec->audio_bits);
    }

    return true;
}

/* ---- Load and decode one frame ---- */
/*
 * C&C VQA files use a FLAT chunk layout (no VQFR container).
 * Chunks appear at the top level: SND2, CBF0/CBFZ, CBP0/CBPZ, CPL0/CPLZ, VPT0/VPTZ.
 * A frame is complete when we receive vector pointers (VPT0/VPTZ).
 * Some VQA files DO use VQFR/VQFK containers — handle both.
 */
static bool vqa_load_frame(VQADecoder* dec) {
    ChunkHeader ch;
    bool got_pointers = false;
    long container_remaining = 0; /* bytes left in current VQFR container */

    while (!got_pointers) {
        if (vqa_read(dec, &ch, 8) != 0) return false;
        uint32_t chunk_size = swap32(ch.size);
        uint32_t id = ch.id;

        /* If this is a VQFR/VQFK container, track its size and descend */
        if (id == ID_VQFR || id == ID_VQFK) {
            container_remaining = (long)chunk_size;
            continue;
        }

        /* Track bytes consumed inside container */
        if (container_remaining > 0)
            container_remaining -= 8 + chunk_size + (chunk_size & 1);

        if (id == ID_CBF0) {
            /* Full codebook, uncompressed */
            uint32_t to_read = (chunk_size < (unsigned)dec->max_cb_size) ? chunk_size : dec->max_cb_size;
            vqa_read(dec, dec->codebook, to_read);
            if (chunk_size > to_read) vqa_seek(dec, chunk_size - to_read);
            dec->cb_partial_size = 0;
            dec->cb_parts_received = 0;

        } else if (id == ID_CBFZ) {
            /* Full codebook, LCW compressed — read into end, decompress in-place */
            uint32_t psize = padsize(chunk_size);
            long lcw_off = dec->max_cb_size - psize;
            if (lcw_off < 0) lcw_off = 0;
            vqa_read(dec, dec->codebook + lcw_off, chunk_size);
            if (chunk_size & 1) vqa_seek(dec, 1); /* pad consumed here */
            LCW_Uncompress(dec->codebook + lcw_off, dec->codebook, psize);
            dec->cb_partial_size = 0;
            dec->cb_parts_received = 0;
            chunk_size = 0; /* already consumed pad byte */

        } else if (id == ID_CBP0) {
            /* Partial codebook, uncompressed — accumulate into cb_next */
            uint32_t to_read = chunk_size;
            if (dec->cb_partial_size + to_read > (unsigned)dec->max_cb_size)
                to_read = dec->max_cb_size - dec->cb_partial_size;
            vqa_read(dec, dec->cb_next + dec->cb_partial_size, to_read);
            if (chunk_size > to_read) vqa_seek(dec, chunk_size - to_read);
            dec->cb_partial_size += to_read;
            dec->cb_next_compressed = 0;
            dec->cb_parts_received++;

        } else if (id == ID_CBPZ) {
            /* Partial codebook, LCW compressed — accumulate compressed data
             * into the end of cb_next buffer (original Westwood approach) */
            uint32_t psize = padsize(chunk_size);
            if (dec->cb_parts_received == 0) {
                /* First partial: compute offset for compressed data at end */
                long est = psize * dec->groupsize + 100;
                dec->cb_next_offset = dec->max_cb_size - est;
                if (dec->cb_next_offset < 0) dec->cb_next_offset = 0;
                dec->cb_partial_size = 0;
            }
            /* Read compressed chunk into cb_next at current position */
            vqa_read(dec, dec->cb_next + dec->cb_next_offset + dec->cb_partial_size, chunk_size);
            dec->cb_partial_size += chunk_size;
            dec->cb_next_compressed = 1;
            dec->cb_parts_received++;

        } else if (id == ID_CPL0) {
            /* Palette, uncompressed (6-bit VGA, same as game) */
            int to_read = (chunk_size < 768) ? chunk_size : 768;
            vqa_read(dec, dec->palette, to_read);
            if (chunk_size > (unsigned)to_read) vqa_seek(dec, chunk_size - to_read);
            dec->palette_dirty = true;

        } else if (id == ID_CPLZ) {
            /* Palette, LCW compressed — read into end, decompress in-place */
            uint32_t psize = padsize(chunk_size);
            long lcw_off = dec->max_pal_size - psize;
            if (lcw_off < 0) lcw_off = 0;
            vqa_read(dec, dec->palette + lcw_off, chunk_size);
            if (chunk_size & 1) vqa_seek(dec, 1);
            LCW_Uncompress(dec->palette + lcw_off, dec->palette, psize);
            dec->palette_dirty = true;
            chunk_size = 0; /* already consumed pad byte */

        } else if (id == ID_VPT0) {
            /* Vector pointers, uncompressed — frame complete */
            uint32_t to_read = (chunk_size < (unsigned)dec->max_ptr_size) ? chunk_size : dec->max_ptr_size;
            vqa_read(dec, dec->pointers, to_read);
            if (chunk_size > to_read) vqa_seek(dec, chunk_size - to_read);
            got_pointers = true;

        } else if (id == ID_VPTZ) {
            /* Vector pointers, LCW compressed — read into end, decompress in-place */
            uint32_t psize = padsize(chunk_size);
            long lcw_off = dec->max_ptr_size - psize;
            if (lcw_off < 0) lcw_off = 0;
            vqa_read(dec, dec->pointers + lcw_off, chunk_size);
            if (chunk_size & 1) vqa_seek(dec, 1);
            LCW_Uncompress(dec->pointers + lcw_off, dec->pointers, psize);
            got_pointers = true;
            chunk_size = 0; /* already consumed pad byte */

        } else if (id == ID_SND0 && dec->has_audio && g_audio_renderer) {
            /* Uncompressed audio */
            int16_t* pcm = (int16_t*)malloc(chunk_size);
            if (pcm) {
                vqa_read(dec, pcm, chunk_size);
                int num_samples = chunk_size / 2;
                g_audio_renderer->queue_vqa_audio(pcm, num_samples,
                    dec->audio_rate, dec->audio_channels);
                free(pcm);
            } else {
                vqa_seek(dec, chunk_size);
            }

        } else if (id == ID_SND2 && dec->has_audio && g_audio_renderer) {
            /* IMA ADPCM compressed audio */
            uint8_t* compressed = (uint8_t*)malloc(chunk_size);
            if (compressed) {
                vqa_read(dec, compressed, chunk_size);
                int max_samples = chunk_size * 2;
                int16_t* pcm = (int16_t*)malloc(max_samples * sizeof(int16_t));
                if (pcm) {
                    int decoded = ADPCM_Decode(compressed, pcm, chunk_size,
                                               &dec->audio_adpcm);
                    if (decoded > 0) {
                        g_audio_renderer->queue_vqa_audio(pcm, decoded,
                            dec->audio_rate, dec->audio_channels);
                    }
                    free(pcm);
                }
                free(compressed);
            } else {
                vqa_seek(dec, chunk_size);
            }

        } else {
            /* Skip unknown chunk (SND1, CAP*, etc.) */
            vqa_seek(dec, chunk_size);
        }

        /* IFF chunks are padded to even size */
        if (chunk_size & 1) vqa_seek(dec, 1);
    }

    /* Skip remaining bytes in VQFR container after getting pointers */
    if (container_remaining > 0)
        vqa_seek(dec, container_remaining);

    /* Decode frame FIRST with the current codebook */
    if (got_pointers) {
        int bw = dec->header.BlockWidth;
        int bh = dec->header.BlockHeight;
        if (bw == 0) bw = 4;
        if (bh == 0) bh = 2;

        int blocks_x = dec->fb_width / bw;
        int blocks_y = dec->fb_height / bh;
        int num_blocks = blocks_x * blocks_y;

        if (bh == 4) {
            unvq_4x4(dec->codebook, dec->pointers, dec->framebuf,
                      blocks_x, blocks_y, dec->fb_width, num_blocks);
        } else {
            unvq_4x2(dec->codebook, dec->pointers, dec->framebuf,
                      blocks_x, blocks_y, dec->fb_width, num_blocks);
        }
    }

    /* THEN promote partial codebook for the NEXT frame */
    if (dec->cb_parts_received >= dec->groupsize) {
        if (dec->cb_next_compressed) {
            /* Decompress in-place: compressed data at cb_next + cb_next_offset,
             * decompress to cb_next start */
            LCW_Uncompress(dec->cb_next + dec->cb_next_offset,
                           dec->cb_next, dec->cb_partial_size);
        }
        /* Swap codebook buffers (ping-pong) */
        uint8_t* tmp = dec->codebook;
        dec->codebook = dec->cb_next;
        dec->cb_next = tmp;
        memset(dec->cb_next, 0, dec->max_cb_size);
        dec->cb_partial_size = 0;
        dec->cb_next_compressed = 0;
        dec->cb_parts_received = 0;
    }

    dec->current_frame++;
    return true;
}

/* ---- Present frame via VideoRendererInterface ---- */
static void vqa_present(VQADecoder* dec) {
    if (!g_video_renderer) return;
    g_video_renderer->present_frame(dec->framebuf, dec->fb_width, dec->fb_height, dec->palette);
}

/* ---- Public API ---- */

VQAHandle* VQA_Alloc(void) {
    VQAHandle* h = (VQAHandle*)calloc(1, sizeof(VQAHandle));
    return h;
}

void VQA_Free(VQAHandle* handle) {
    if (!handle) return;
    VQADecoder* dec = (VQADecoder*)handle->internal;
    if (dec) {
        free(dec->codebook);
        free(dec->cb_next);
        free(dec->pointers);
        free(dec->palette);
        free(dec->framebuf);
        free(dec);
    }
    free(handle);
}

void VQA_Init(VQAHandle* handle, void* iohandler) {
    if (!handle) return;
    handle->internal = nullptr;
    /* Store the I/O handler — it will be called with 'handle' as first arg */
    VQADecoder* dec = (VQADecoder*)calloc(1, sizeof(VQADecoder));
    dec->io = (VQAIOHandler)iohandler;
    dec->handle = handle;
    handle->internal = dec;
}

/* Allocate and open a CCFileClass directly */
extern "C++" void* vqa_open_file(const char* filename);
extern "C++" void  vqa_close_file(void* file);

long VQA_Open(VQAHandle* handle, char const* filename, VQAConfig* config) {
    if (!handle || !handle->internal) return -1;
    VQADecoder* dec = (VQADecoder*)handle->internal;

    if (config) dec->config = *config;

    /* Open the file directly (bypass threaded MixFileHandler) */
    void* file = vqa_open_file(filename);
    if (!file) {
        DBG("VQA: failed to open %s", filename);
        return -1;
    }
    handle->VQAio = (unsigned long)(uintptr_t)file;

    /* Parse the VQA header */
    if (!vqa_parse_header(dec)) {
        DBG("VQA: failed to parse header for %s", filename);
        return -1;
    }

    /* Allocate internal buffers */
    if (!vqa_alloc_buffers(dec)) {
        return -1;
    }

    DBG("VQA: opened %s (%d frames)", filename, dec->total_frames);
    return 0;
}

void VQA_Close(VQAHandle* handle) {
    if (!handle) return;
    if (handle->VQAio) {
        vqa_close_file((void*)(uintptr_t)handle->VQAio);
        handle->VQAio = 0;
    }
}

long VQA_Play(VQAHandle* handle, int mode) {
    if (!handle || !handle->internal || !g_video_renderer) {
        DBG("VQA_Play: null check failed handle=%p internal=%p renderer=%p",
                (void*)handle, handle ? handle->internal : nullptr, (void*)g_video_renderer);
        return -1;
    }
    VQADecoder* dec = (VQADecoder*)handle->internal;
    DBG("VQA_Play: dec=%p fps=%d frames=%d file=%lu",
            (void*)dec, dec->fps, dec->total_frames, handle->VQAio);

    g_video_renderer->on_playback_start();
    unsigned int frame_ms = 1000 / (dec->fps ? dec->fps : 15);

    DBG("VQA: playing %d frames at %d fps (%dms/frame)",
            dec->total_frames, dec->fps, frame_ms);

    /* Audio pipeline compensation: SDL audio buffering + OS pipeline adds
     * latency between queue_vqa_audio() and actual speaker output.  We
     * pre-load the first frame (which queues its SND2 audio chunk) and
     * then wait briefly so the audio device starts consuming data before
     * we present the first video frame.  Without this the video leads
     * audio by ~100-200 ms. */
    static const unsigned AUDIO_PIPELINE_MS = 150;

    for (int f = 0; f < dec->total_frames && !dec->stopped; f++) {
        uint64_t t0 = g_video_renderer->get_ticks_ms();

        if (!vqa_load_frame(dec)) {
            DBG("VQA: frame %d load failed", f);
            break;
        }

        if (f == 0 && dec->has_audio && g_audio_renderer) {
            g_video_renderer->delay_ms(AUDIO_PIPELINE_MS);
            t0 = g_video_renderer->get_ticks_ms();
        }

        vqa_present(dec);

        if (g_video_renderer->poll_abort()) {
            dec->stopped = true;
            extern bool Brokeout;
            Brokeout = true;
            break;
        }

        uint64_t elapsed = g_video_renderer->get_ticks_ms() - t0;
        if (elapsed < frame_ms)
            g_video_renderer->delay_ms(frame_ms - elapsed);
    }

    g_video_renderer->on_playback_stop();
    return 0;
}

#ifndef GAME_RA
void VQA_PauseAudio(void) {
    if (g_audio_renderer) g_audio_renderer->pause_vqa_audio();
}
#endif
void VQA_ResumeAudio(void) {
    if (g_audio_renderer) g_audio_renderer->resume_vqa_audio();
}
