/*
 * sound.h -- Platform-independent audio bridge for the Westwood engine.
 *
 * Provides Play_Sample, Stop_Sample, etc. by delegating to an
 * AudioRendererInterface set at startup. No SDL3 dependency.
 *
 * Origin: WIN32LIB/SOUNDDLL.CPP (EA GPL3 release)
 */

#ifndef LIBS_AUDIO_SOUND_H
#define LIBS_AUDIO_SOUND_H

#include "audio_renderer.h"

/* Set the audio backend. Must be called before any Play_Sample etc. */
void Audio_Set_Renderer(AudioRendererInterface* renderer);

/* Get the current audio renderer (may be nullptr). */
AudioRendererInterface* Audio_Get_Renderer(void);

/* Diagnostic log (implemented by the platform layer, declared here for bridge use) */
void Audio_Log(const char* fmt, ...);

#endif /* LIBS_AUDIO_SOUND_H */
