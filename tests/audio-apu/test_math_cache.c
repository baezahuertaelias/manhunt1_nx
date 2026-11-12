#define MANHUNT_APU_MATH_CACHE_TEST 1
#include "apu_math_cache.h"
#include <assert.h>
#include <stdio.h>
#include <time.h>

static void same(float a, float b)
{
    uint32_t aa, bb;
    memcpy(&aa, &a, 4); memcpy(&bb, &b, 4);
    assert(aa == bb || (isnan(a) && isnan(b)));
}

static void *check(void *arg)
{
    unsigned offset = (unsigned)(uintptr_t)arg;
    for (unsigned i = 0; i < 65536; ++i) {
        uint16_t volume = (uint16_t)(i + offset);
        unsigned masked = volume & 0xFFF;
        float expected = masked == 0xFFF ? 0.0f : powf(10.0f, masked / (64.0f * -20.0f));
        same(apu_attenuation_cached(volume), expected);
        float exponent = ((int)i - 32768) / 4096.0f;
        same(apu_pow2_cached(exponent), powf(2.0f, exponent));
        /* Revisit entries after collisions, including fractional envelopes. */
        exponent = exponent * 0.31f + 0.12345f;
        same(apu_pow2_cached(exponent), powf(2.0f, exponent));
    }
    same(apu_pow2_cached(INFINITY), INFINITY);
    same(apu_pow2_cached(-INFINITY), 0.0f);
    same(apu_pow2_cached(NAN), NAN);
    same(apu_pow2_cached(-0.0f), 1.0f);
    return NULL;
}

int main(void)
{
    /* Race the first table initialization and exercise private pow caches. */
    pthread_t workers[4];
    for (unsigned i = 0; i < 4; ++i) assert(!pthread_create(&workers[i], NULL, check, (void *)(uintptr_t)(i * 777)));
    for (int i = 0; i < 4; ++i) assert(!pthread_join(workers[i], NULL));
    puts("PASS exact APU math: all 65536 volume inputs, pitch/cutoff/envelope values, cache collisions and concurrent initialization");
}
