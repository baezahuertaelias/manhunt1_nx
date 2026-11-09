#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <unistd.h>
#include "process_exit.h"
static int ready[2];
static void must_not_cleanup(void) { _Exit(99); }
static void *guest_worker(void *unused)
{
    (void)unused;
    assert(write(ready[1], "R", 1) == 1);
    for (;;) pause();
    return NULL;
}
int main(void)
{
    assert(prctl(PR_SET_DUMPABLE, 0) == 0);
    assert(!pipe(ready));
    pid_t child = fork(); assert(child >= 0);
    if (!child) {
        assert(!atexit(must_not_cleanup));
        pthread_t worker;
        assert(!pthread_create(&worker, NULL, guest_worker, NULL));
        char byte;
        assert(read(ready[0], &byte, 1) == 1 && byte == 'R');
        manhunt_process_exit(23);
    }
    int status;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 23);
    close(ready[0]); close(ready[1]);
    puts("PASS process exit: active guest worker terminated with process; atexit cleanup bypassed");
}
