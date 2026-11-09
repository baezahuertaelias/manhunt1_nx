#define _POSIX_C_SOURCE 200809L
#include "nv2a_backend.h"
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/prctl.h>
#endif
static unsigned syncs;
static int last_invalidate;
static void raw(const Nv2aRawBatch *b) {(void)b;}
static void sync_call(int invalidate) {syncs++;last_invalidate=invalidate;}
static void invalid_case(int missing_raw)
{
    Nv2aBackend b={0};b.flags=NV2A_BACKEND_DEFERRED_RAM;
    if(missing_raw)b.sync=sync_call;else b.draw_raw=raw;
    pid_t p=fork();assert(p>=0);
    if(!p){nv2a_backend_register(&b);_exit(42);}
    int status;assert(waitpid(p,&status,0)==p);
    assert(WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);
}
int main(void)
{
#if defined(__linux__)
    assert(prctl(PR_SET_DUMPABLE,0)==0);
#endif
    invalid_case(0);invalid_case(1);
    Nv2aBackend b={0};b.flags=NV2A_BACKEND_DEFERRED_RAM;b.draw_raw=raw;b.sync=sync_call;
    nv2a_backend_register(&b);nv2a_backend_sync(0);assert(syncs==1&&last_invalidate==0);
    nv2a_backend_register(NULL);assert(syncs==2&&last_invalidate==1);
    nv2a_backend_sync(0);assert(syncs==2);
    puts("PASS actual executor registration rejects missing deferred hooks and synchronizes removal");
    return 0;
}
