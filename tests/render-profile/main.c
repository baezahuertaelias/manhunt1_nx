#include <assert.h>
#include <string.h>
#include <pthread.h>
#include "nv2a_profile.h"

_Thread_local Nv2aProfile nv2a_profile;
static int enabled, clock_reads;
static uint64_t now;
int nv2a_profile_enabled(void) { return enabled; }
uint64_t xbox_HostNowNs(void) { clock_reads++; return now; }

static void *other_thread(void *unused)
{
    (void)unused;
    assert(nv2a_profile.calls[NV_PROFILE_SCAN] == 0);
    nv2a_profile.calls[NV_PROFILE_SCAN] = 99;
    return NULL;
}

int main(void)
{
    uint64_t exclusive = 999;
    Nv2aProfile empty = {0};
    Nv2aProfileStart start = nv2a_profile_begin();
    now = 10;
    nv2a_profile_end(NV_PROFILE_SCAN, start);
    nv2a_profile_flip(now);
    nv2a_profile_report();
    assert(clock_reads == 0 && !memcmp(&empty, &nv2a_profile, sizeof empty));
    assert(nv2a_profile_scan_only(&exclusive) && exclusive == 0);

    enabled = 1;
    now = 0;
    start = nv2a_profile_begin();
    now = 10;
    Nv2aProfileStart method = nv2a_profile_begin();
    now = 30;
    nv2a_profile_end(NV_PROFILE_METHOD, method);
    now = 50;
    nv2a_profile_end(NV_PROFILE_SCAN, start);
    assert(nv2a_profile.ns[NV_PROFILE_SCAN] == 50);
    assert(nv2a_profile.ns[NV_PROFILE_METHOD] == 20);
    assert(nv2a_profile.calls[NV_PROFILE_SCAN] == 1);
    assert(nv2a_profile_scan_only(&exclusive) && exclusive == 30);
    nv2a_profile.ns[NV_PROFILE_METHOD] = 51;
    assert(!nv2a_profile_scan_only(&exclusive));
    nv2a_profile.ns[NV_PROFILE_METHOD] = 20;

    start = nv2a_profile_begin();
    now = 49;
    nv2a_profile_end(NV_PROFILE_CLEAR, start);
    assert(nv2a_profile.invalid == 1 && !nv2a_profile.calls[NV_PROFILE_CLEAR]);
    start = nv2a_profile_begin();
    nv2a_profile_end(NV_PROFILE_CLEAR, start);
    assert(nv2a_profile.calls[NV_PROFILE_CLEAR] == 1 && !nv2a_profile.ns[NV_PROFILE_CLEAR]);

    nv2a_profile_flip(0);
    nv2a_profile_flip(25);
    nv2a_profile_flip(70);
    assert(nv2a_profile.flips == 2 && nv2a_profile.flip_ns == 70);
    assert(nv2a_profile.first_ns == 0 && nv2a_profile.last_flip_ns == 70);
    nv2a_profile_flip(69);
    assert(nv2a_profile.invalid == 2 && nv2a_profile.flips == 2);
    assert(nv2a_profile.last_flip_ns == 70);
    pthread_t thread;
    assert(pthread_create(&thread, NULL, other_thread, NULL) == 0);
    assert(pthread_join(thread, NULL) == 0);
    assert(nv2a_profile.calls[NV_PROFILE_SCAN] == 1);
    nv2a_profile_report();
    puts("RENDER-PROFILE accounting self-test PASS");
    return 0;
}
