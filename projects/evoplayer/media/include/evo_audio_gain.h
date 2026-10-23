#ifndef EVO_AUDIO_GAIN_H
#define EVO_AUDIO_GAIN_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Apply digital gain to signed 16-bit PCM audio samples with clean clipping (#166).
 *
 * Multiplies decoded PCM samples by (1 + percent_boost / 100) using int32 math:
 *   v = (s * (100 + boost)) / 100
 * Clamped cleanly to [-32768, 32767] so loud passages never wrap around into crackle.
 *
 * @param samples Array of interleaved S16 PCM samples.
 * @param count Total number of int16_t samples to process (frames * channels).
 * @param percent_boost Boost percentage (0 = Off, 50 = +50%, 100 = +100%, 200 = +200%, 300 = +300%).
 */
void evo_audio_apply_gain_s16(int16_t *samples, size_t count, int percent_boost);

/**
 * Configure the active playback volume boost percentage (0 to 300).
 */
void evo_audio_gain_set_boost(int percent);

/**
 * Retrieve the active playback volume boost percentage.
 */
int evo_audio_gain_get_boost(void);

/**
 * Process samples in-place using the current active boost level.
 */
void evo_audio_gain_process(int16_t *samples, size_t count);

#ifdef __cplusplus
}
#endif

#endif /* EVO_AUDIO_GAIN_H */
