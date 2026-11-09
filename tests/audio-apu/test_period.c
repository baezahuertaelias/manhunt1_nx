#include <assert.h>
#include <stdio.h>
#include "audio_period.h"
int main(void)
{
    const char *bad[] = {"512junk", "512 ", " 512", "+512", "-512", "0x200", "0",
        "255", "257", "4096", "99999999999999999999999999999", "512\n", "1e3"};
    const char *good[] = {"256", "512", "1024", "2048"};
    const int values[] = {256, 512, 1024, 2048};
    for (int fallback = 512; fallback <= 2048; fallback *= 4) {
        int valid;
        assert(manhunt_audio_period(NULL, fallback, &valid) == fallback && valid);
        assert(manhunt_audio_period("", fallback, &valid) == fallback && valid);
        for (unsigned i = 0; i < sizeof bad / sizeof *bad; ++i)
            assert(manhunt_audio_period(bad[i], fallback, &valid) == fallback && !valid);
        for (unsigned i = 0; i < sizeof good / sizeof *good; ++i)
            assert(manhunt_audio_period(good[i], fallback, &valid) == values[i] && valid);
    }
    puts("PASS strict period: host/Switch defaults, four valid periods, malformed/signed/overflow rejection");
}
