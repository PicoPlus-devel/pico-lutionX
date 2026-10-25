// aulib_mix.h - the platform side of the pico-lutionX SDL_audiolib subset (src/aulib).
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Mixes the playing sounds: fills `frames` stereo frames of signed 16-bit samples
// (interleaved L, R) at Aulib::sampleRate(). Call from the thread that runs the engine.
void aulib_mix(int16_t *stereo, int frames);

#ifdef __cplusplus
}
#endif
