/*
 * hal_audio.h — speaker / microphone / ringer. Declarations only.
 * Backends later: I2S codec on hardware, a silent stub (or SDL2 audio) on host.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t sample_rate;  /* Hz */
    uint8_t  channels;     /* 1 = mono, 2 = stereo */
    uint8_t  bits;         /* 16 */
} hal_audio_cfg_t;

int  hal_audio_init(void);

/* Continuous playback: feed PCM frames; returns bytes consumed or <0 on error. */
int  hal_audio_write(const void *pcm, size_t len_bytes);

/* Ringer volume 0..100. */
int  hal_audio_set_volume(uint8_t percent);

/* Start/stop capture (voice notes, calls). */
int  hal_audio_capture_start(hal_audio_cfg_t cfg);
int  hal_audio_capture_stop(void);

#ifdef __cplusplus
}
#endif
