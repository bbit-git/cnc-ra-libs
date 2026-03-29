/*
 * sound.cpp -- Platform-independent audio bridge for the Westwood engine.
 *
 * These functions are called by the preserved engine code (Play_Sample,
 * Stop_Sample, etc.) and delegate to an AudioRendererInterface backend.
 * The backend is set at startup via Audio_Set_Renderer().
 *
 * Origin: WIN32LIB/SOUNDDLL.CPP + CONQUER.CPP (EA GPL3 release)
 */

#include "sound.h"
#include "function.h"
#include "aud_decode.h"
#include <cstdlib>
#include <cstdio>

/* ---- Renderer pointer (set by platform layer at startup) ---- */
static AudioRendererInterface* g_audio_renderer = nullptr;

void Audio_Set_Renderer(AudioRendererInterface* renderer)
{
    g_audio_renderer = renderer;
}

AudioRendererInterface* Audio_Get_Renderer(void)
{
    return g_audio_renderer;
}

/* ---- Engine-facing audio functions ---- */

int Play_Sample(void const* sample, int priority, int volume, signed short panloc)
{
    if (!sample) return 0;
    AudioRendererInterface* audio = g_audio_renderer;
    if (!audio) { Audio_Log("Play_Sample: no audio renderer"); return 0; }

    /* The engine passes raw AUD data (with header) from MIX files.
     * Decode to PCM first, then play. */
    AUDHeaderType hdr;
    const uint8_t* aud_bytes = (const uint8_t*)sample;
    const uint8_t* audio_data = AUD_Parse_Header(sample, 65536, &hdr);
    if (!audio_data) {
        Audio_Log("Play_Sample: AUD header parse failed (ptr=%p)", sample);
        return 0;
    }

    int max_samples = (int)hdr.UncompSize;
    if (max_samples <= 0) max_samples = (int)hdr.Size * 4;
    int16_t* pcm = (int16_t*)malloc(max_samples * sizeof(int16_t));
    if (!pcm) return 0;

    int rate = 0, channels = 0;
    int decoded = AUD_Decode_Memory(sample, (int)hdr.Size + 12,
                                     pcm, max_samples, &rate, &channels);
    if (decoded <= 0) {
        Audio_Log("Play_Sample: AUD decode failed (comp=%d size=%d uncomp=%d rate=%d)",
                  hdr.Compression, hdr.Size, hdr.UncompSize, hdr.Rate);
        free(pcm);
        return 0;
    }

    int handle = audio->play_sample(pcm, decoded, rate, channels,
                                     priority, volume, (int)panloc,
                                     sample); /* pass original AUD ptr for tracking */
    if (!handle) {
        Audio_Log("Play_Sample: play_sample returned 0 (pri=%d vol=%d pan=%d rate=%d decoded=%d)",
                  priority, volume, (int)panloc, rate, decoded);
    }
    free(pcm);
    return handle;
}

int File_Stream_Sample_Vol(char const* filename, int volume, int handle)
{
    AudioRendererInterface* audio = g_audio_renderer;
    if (!audio || !filename) return 0;
    return audio->stream_file(filename, volume);
}

void Fade_Sample(int handle, int ticks)
{
    AudioRendererInterface* audio = g_audio_renderer;
    if (audio) audio->fade(handle, ticks);
}

int Sample_Status(int handle)
{
    AudioRendererInterface* audio = g_audio_renderer;
    if (!audio) return 0;
    return audio->status(handle);
}

void Stop_Sample(int handle)
{
    AudioRendererInterface* audio = g_audio_renderer;
    if (audio) audio->stop(handle);
}

void Stop_Sample_Playing(void const* sample)
{
    AudioRendererInterface* audio = g_audio_renderer;
    if (audio) audio->stop_by_pointer(sample);
}

int Is_Sample_Playing(void const* sample)
{
    AudioRendererInterface* audio = g_audio_renderer;
    if (!audio) return 0;
    return audio->is_pointer_playing(sample) ? 1 : 0;
}

void Sound_Callback(void)
{
    AudioRendererInterface* audio = g_audio_renderer;
    if (audio) audio->callback();
}

void* Load_Sample(char const* filename)
{
    /* Load entire AUD file into memory -- engine handles this via MIX normally */
    return nullptr;
}

void Free_Sample(void const*) {}

/* RA provides its own Set_Score_Vol in audio.cpp (returns int) */
#ifndef ENGLISH
void Set_Score_Vol(int volume)
{
    AudioRendererInterface* audio = g_audio_renderer;
    if (audio) audio->set_score_volume(volume);
}
#endif

void Set_Score_Volume(int volume)
{
    AudioRendererInterface* audio = g_audio_renderer;
    if (audio) audio->set_score_volume(volume);
}
