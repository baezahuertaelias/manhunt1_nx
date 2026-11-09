/* The guest runtime does not currently expose stop/join for all guest workers.
 * An entry-point return therefore ends the entire host process. Do not destroy
 * shared APU/guest memory while those workers can still use it. No atexit cleanup
 * is run; the OS reclaims the process's resources after terminating its threads.
 */
#ifndef MANHUNT_PROCESS_EXIT_H
#define MANHUNT_PROCESS_EXIT_H
#include <stdlib.h>
static inline _Noreturn void manhunt_process_exit(int status)
{
    _Exit(status);
}
#endif
