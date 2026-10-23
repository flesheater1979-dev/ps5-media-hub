/*
 * evo_audio_gain.c — Volume boost for decoded PCM audio with clean clipping (#166).
 *
 * Multiplies decoded PCM samples by (1 + percent_boost / 100) using int32 math
 * and clamps to [-32768, 32767] to prevent wraparound crackle.
 */
#include "evo_audio_gain.h"

static int s_active_boost_percent = 0;

void evo_audio_apply_gain_s16(int16_t *samples, size_t count, int percent_boost)
{
    if (!samples || count == 0 || percent_boost <= 0)
        return;

    int32_t scale = 100 + percent_boost;
    for (size_t i = 0; i < count; i++) {
        int32_t v = ((int32_t)samples[i] * scale) / 100;
        if (v > 32767)
            v = 32767;
        else if (v < -32768)
            v = -32768;
        samples[i] = (int16_t)v;
    }
}

void evo_audio_gain_set_boost(int percent)
{
    if (percent < 0)
        percent = 0;
    else if (percent > 300)
        percent = 300;
    s_active_boost_percent = percent;
}

int evo_audio_gain_get_boost(void)
{
    return s_active_boost_percent;
}

void evo_audio_gain_process(int16_t *samples, size_t count)
{
    if (s_active_boost_percent > 0)
        evo_audio_apply_gain_s16(samples, count, s_active_boost_percent);
}
