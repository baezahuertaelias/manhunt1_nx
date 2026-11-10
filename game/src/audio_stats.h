#ifndef MANHUNT_AUDIO_STATS_H
#define MANHUNT_AUDIO_STATS_H
#include <stdint.h>

/* One SDL device-lock snapshot. Queue values are stereo frames, not bytes. */
typedef struct {
    unsigned queued, capacity, underruns;
    uint64_t consumed;
    int active;
} ManhuntAudioStats;
void manhunt_audio_stats(ManhuntAudioStats *out);
#endif
