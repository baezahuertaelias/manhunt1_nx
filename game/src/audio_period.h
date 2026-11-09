/* Parse the complete decimal period value before opening an SDL device. */
#ifndef MANHUNT_AUDIO_PERIOD_H
#define MANHUNT_AUDIO_PERIOD_H
#include <errno.h>
#include <stdlib.h>

static inline int manhunt_audio_period(const char *text, int fallback, int *valid)
{
    char *end;
    long value;
    *valid = 1;
    if (!text || !*text) return fallback;
    /* No leading whitespace, sign, suffix or non-decimal representation. */
    for (const char *p = text; *p; ++p)
        if (*p < '0' || *p > '9') { *valid = 0; return fallback; }
    errno = 0;
    value = strtol(text, &end, 10);
    if (errno || *end || value < 256 || value > 2048 || (value & (value - 1))) {
        *valid = 0;
        return fallback;
    }
    return (int)value;
}
#endif
