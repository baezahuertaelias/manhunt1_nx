#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <time.h>
#include <unistd.h>
#include "audio_mmio.h"
#include "apu.h"

ptrdiff_t g_xbox_mem_offset;
MCPXAPUState *g_apu_state;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
static int read_entered, release_read, active, destroyed, close_requested;

int xa2_init(void) { return 1; }
void xa2_shutdown(void) {}
MCPXAPUState *mcpx_apu_init_standalone(uint8_t *base)
{ assert(base); return (MCPXAPUState *)1; }
uint64_t mcpx_apu_mmio_read(MCPXAPUState *state, uint64_t off, unsigned width)
{
    assert(state == (MCPXAPUState *)1 && off == 0x1000 && width == 4);
    pthread_mutex_lock(&lock);
    assert(!destroyed);
    active++;
    read_entered = 1;
    pthread_cond_broadcast(&changed);
    while (!release_read) pthread_cond_wait(&changed, &lock);
    assert(!destroyed);
    active--;
    pthread_mutex_unlock(&lock);
    return 0x1234;
}
void mcpx_apu_mmio_write(MCPXAPUState *state, uint64_t off, uint64_t value, unsigned width)
{ (void)state; (void)off; (void)value; (void)width; assert(0); }
void mcpx_apu_shutdown(MCPXAPUState *state)
{
    assert(state == (MCPXAPUState *)1);
    pthread_mutex_lock(&lock);
    /* The negative control removes the production access guard and reaches
     * this assertion while the synthetic model read remains blocked. */
    assert(!active && !destroyed);
    destroyed = 1;
    pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&lock);
}
static void *reader(void *arg)
{ (void)arg; assert(manhunt_audio_read32(0xFE801000) == 0x1234); return NULL; }
static void *closer(void *arg)
{
    (void)arg;
    pthread_mutex_lock(&lock);
    close_requested = 1;
    pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&lock);
    manhunt_audio_shutdown();
    return NULL;
}
int main(void)
{
    assert(prctl(PR_SET_DUMPABLE, 0) == 0);
    void *base = mmap(NULL, 0x100000000ULL, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    assert(base != MAP_FAILED);
    g_xbox_mem_offset = (ptrdiff_t)base;
    assert(!setenv("MANHUNT_AUDIO_APU", "1", 1));
    assert(manhunt_audio_init(base));
    pthread_t r, c;
    assert(!pthread_create(&r, NULL, reader, NULL));
    pthread_mutex_lock(&lock);
    while (!read_entered) pthread_cond_wait(&changed, &lock);
    pthread_mutex_unlock(&lock);
    assert(!pthread_create(&c, NULL, closer, NULL));
    pthread_mutex_lock(&lock);
    while (!close_requested) pthread_cond_wait(&changed, &lock);
    struct timespec until;
    assert(!clock_gettime(CLOCK_REALTIME, &until));
    until.tv_nsec += 100000000;
    if (until.tv_nsec >= 1000000000) { until.tv_sec++; until.tv_nsec -= 1000000000; }
    int rc = 0;
    while (!destroyed && rc != ETIMEDOUT) rc = pthread_cond_timedwait(&changed, &lock, &until);
    assert(!destroyed);
    release_read = 1;
    pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&lock);
    assert(!pthread_join(r, NULL) && !pthread_join(c, NULL));
    assert(destroyed && !manhunt_audio_enabled() && !g_apu_state);
    manhunt_audio_shutdown(); /* idempotent after join */
    pid_t child = fork(); assert(child >= 0);
    if (!child) { manhunt_audio_read32(0xFE801000); _Exit(0); }
    int status; assert(waitpid(child, &status, 0) == child);
    assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
    munmap(base, 0x100000000ULL);
    puts("PASS concurrent audio close: active access drains before destroy; late access stops; close idempotent");
}
