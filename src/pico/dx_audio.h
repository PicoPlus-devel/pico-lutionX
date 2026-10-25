// dx_audio.h -- sound output (dx_audio.c): the mixer's ring, played by core1.

#ifndef DX_AUDIO_H
#define DX_AUDIO_H

#ifdef __cplusplus
extern "C" {
#endif

// Sets up the I2S DAC and starts core1's audio pump (after dx_display_init).
void dx_audio_init(void);
// Mixes the engine's sounds into the ring up to the target fill (core0, thread context).
void dx_audio_update(void);
// Mixes a little further ahead while core0 would otherwise wait (dx_display_present's
// wait for the vertical blank), in short steps; returns 0 when there is nothing to do.
int dx_audio_idle(void);

// Around flash writes (dx_flash.c): stops core1's pump, which may run from flash, and
// waits until it is out of it.
void dx_audio_hold(void);
void dx_audio_release(void);

#ifdef __cplusplus
}
#endif

#endif
