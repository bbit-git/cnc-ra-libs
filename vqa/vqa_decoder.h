/*
 * VQA Decoder — minimal Westwood VQA video format decoder
 * Based on format documentation from CnC_Red_Alert/VQ/VQA32/ (GPL3)
 *
 * Supports: VQA version 2, 4x2 blocks, 8-bit indexed color,
 *           LCW-compressed codebooks/pointers/palettes
 * Does not support: audio (handled separately), captions
 *
 * Platform-independent: uses VideoRendererInterface for presentation.
 */

#ifndef VQA_DECODER_H
#define VQA_DECODER_H

#include <cstdint>
#include <cstdlib>
#include "video_renderer.h"

#pragma pack(push, 1)
/* VQA file header (VQHD chunk) */
struct VQAHeader {
    uint16_t Version;
    uint16_t Flags;
    uint16_t Frames;
    uint16_t ImageWidth;
    uint16_t ImageHeight;
    uint8_t  BlockWidth;
    uint8_t  BlockHeight;
    uint8_t  FPS;
    uint8_t  Groupsize;
    uint16_t Num1Colors;
    uint16_t CBentries;
    uint16_t Xpos;
    uint16_t Ypos;
    uint16_t MaxFramesize;
    uint16_t SampleRate;
    uint8_t  Channels;
    uint8_t  BitsPerSample;
    uint16_t AltSampleRate;
    uint8_t  AltChannels;
    uint8_t  AltBitsPerSample;
    uint16_t FutureUse[5];
};

/* IFF chunk header */
struct ChunkHeader {
    uint32_t id;
    uint32_t size; /* big-endian in file */
};
#pragma pack(pop)

/* I/O callback: same signature as engine's MixFileHandler */
typedef long (*VQAIOHandler)(void* handle, long action, void* buffer, long nbytes);

/* I/O commands (may already be defined in td_platform.h) */
#ifndef VQACMD_READ
#define VQACMD_READ    1
#define VQACMD_WRITE   2
#define VQACMD_SEEK    3
#define VQACMD_OPEN    4
#define VQACMD_CLOSE   5
#define VQACMD_INIT    6
#define VQACMD_CLEANUP 7
#endif

/* Playback modes */
#ifndef VQAMODE_RUN
#define VQAMODE_RUN   0
#define VQAMODE_WALK  1
#endif

/* Config flags */
#ifndef VQAOPTF_AUDIO
#define VQAOPTF_AUDIO  0x0001
#endif
#ifndef VQACFGF_BUFFER
#define VQACFGF_BUFFER 0x0020
#endif

/* Opaque handle used by engine */
struct VQAHandle {
    unsigned long VQAio;     /* file handle (CCFileClass*) */
    void*         internal;  /* VQADecoder* */
};

struct VQAConfig {
    void*    DrawerCallback;
    void*    EventHandler;
    long     NotifyFlags;
    int      Vmode;
    int      VBIBit;
    uint8_t* ImageBuf;
    long     ImageWidth;
    long     ImageHeight;
    long     X1, Y1;
    long     FrameRate;
    long     DrawRate;
    long     TimerMethod;
    long     DrawFlags;
    long     OptionFlags;
    long     NumFrameBufs;
    long     NumCBBufs;
    char*    VocFile;
    void*    AudioBuf;
    long     AudioBufSize;
    long     AudioRate;
    long     Volume;
    long     HMIBufSize;
    long     DigiHandle;
    long     DigiCard;
    long     DigiPort;
    long     DigiIRQ;
    long     DigiDMA;
    long     Language;
    char*    CaptionFont;
};

/* Set the video renderer backend (must be called before VQA_Play) */
void VQA_Set_Video_Renderer(VideoRendererInterface* renderer);

/* Public API — matches engine's expected function signatures */
VQAHandle* VQA_Alloc(void);
void       VQA_Free(VQAHandle* handle);
void       VQA_Init(VQAHandle* handle, void* iohandler);
long       VQA_Open(VQAHandle* handle, char const* filename, VQAConfig* config);
void       VQA_Close(VQAHandle* handle);
long       VQA_Play(VQAHandle* handle, int mode);

/* Called by engine to pause/resume audio during movie */
void       VQA_PauseAudio(void);
void       VQA_ResumeAudio(void);

/* Set the audio renderer backend for VQA movie audio */
class AudioRendererInterface;
void       VQA_Set_Audio_Renderer(AudioRendererInterface* renderer);

#endif /* VQA_DECODER_H */
