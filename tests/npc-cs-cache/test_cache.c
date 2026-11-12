/* Exercise the production resolver and recursive critical sections. */
#define _POSIX_C_SOURCE 200809L
#include "kernel.h"
#include "xbox_memory_layout.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
static _Thread_local unsigned long long table_reads;
static void tracked_read(PSRWLOCK lock) { table_reads++; AcquireSRWLockShared(lock); }
#define AcquireSRWLockShared tracked_read
#ifndef NPC_RTL_SOURCE
#define NPC_RTL_SOURCE "../../game/build/npc-cs-cache/kernel_rtl.c"
#endif
#include NPC_RTL_SOURCE
#undef AcquireSRWLockShared
RECOMP_TLS uint32_t g_esp;
RECOMP_TLS uint32_t g_xbox_kernel_caller;
ptrdiff_t g_xbox_mem_offset;
uint32_t g_xbox_code_lo, g_xbox_code_hi;
void xbox_GuestCpuBlocked(void) {}
void xbox_log(int level,const char *subsystem,const char *fmt,...) {(void)level;(void)subsystem;(void)fmt;assert(0); }
static RTL_CRITICAL_SECTION locks[1025];
static CRITICAL_SECTION *worker_identity[4];
static unsigned counter;
static CRITICAL_SECTION *shared_identity;
static void *worker(void *arg)
{
    unsigned index=(unsigned)(uintptr_t)arg;
    worker_identity[index]=xbox_cs_shadow(&locks[1024]);
    assert(worker_identity[index]);
    assert(xbox_cs_shadow(&locks[0]) == shared_identity);
    for (int i=0;i<30000;i++) {
        xbox_RtlEnterCriticalSection(&locks[0]);
        xbox_RtlEnterCriticalSection(&locks[0]);
        counter++;
        xbox_RtlLeaveCriticalSection(&locks[0]);
        xbox_RtlLeaveCriticalSection(&locks[0]);
    }
    return NULL;
}
static uint64_t ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000000u+t.tv_nsec; }
int main(int argc,char **argv)
{
    assert(argc>=2);setenv("MANHUNT_CS_CACHE",argv[1],1);
    const int on=atoi(argv[1])!=0;
    const int single=argc>2;
    if(single)setenv("RECOMP_CS_MODE","single",1);
    assert(!xbox_cs_shadow(NULL));
    shared_identity=xbox_cs_shadow(&locks[0]);assert(shared_identity);
    assert(xbox_cs_shadow(&locks[0])==shared_identity);
    /* Direct-mapped cache replacement must preserve every table identity. */
    CRITICAL_SECTION *identity[1024];
    for(unsigned i=0;i<1024;i++){identity[i]=xbox_cs_shadow(&locks[i]);assert(identity[i]);
        if(!single)for(unsigned j=0;j<i;j++)assert(identity[i]!=identity[j]);
    }
    for(unsigned i=0;i<1024;i++)assert(xbox_cs_shadow(&locks[i])==identity[i]);
    const unsigned loops=3000000;CRITICAL_SECTION *volatile sink=NULL;
    unsigned long long start_reads=table_reads;uint64_t start=ns();
    for(unsigned i=0;i<loops;i++)sink=xbox_cs_shadow(&locks[0]);
    uint64_t elapsed=ns()-start;unsigned long long reads=table_reads-start_reads;
    assert(sink==shared_identity);if(on||single)assert(reads<=1);else assert(reads==loops);
    printf("BENCH resolver cache=%d calls=%u table_reads=%llu ns_per_call=%.2f\n",on,loops,reads,(double)elapsed/loops);
    /* A cached resolver still pays for (and obeys) each real lock pair. */
    start=ns();
    for(unsigned i=0;i<500000;i++){xbox_RtlEnterCriticalSection(&locks[0]);xbox_RtlLeaveCriticalSection(&locks[0]);}
    elapsed=ns()-start;
    printf("BENCH uncontended lock pair cache=%d calls=500000 ns_per_pair=%.2f\n",on,(double)elapsed/500000);
    g_cs_contention_reports=17; /* diagnostic chatter is unrelated to ownership */
    pthread_t threads[4];
    for(int i=0;i<4;i++)assert(!pthread_create(&threads[i],NULL,worker,(void *)(uintptr_t)i));
    for(int i=0;i<4;i++)assert(!pthread_join(threads[i],NULL));
    assert(counter==120000);
    for(unsigned i=1;i<4;i++)assert(worker_identity[i]==worker_identity[0]);
    if(single)assert(worker_identity[0]==shared_identity);
    printf("PASS cache=%d null, 1024 identities/collisions, per-thread identity, recursive four-thread mutual exclusion\n",on);
    return 0;
}
