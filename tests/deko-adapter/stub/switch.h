/* Host stand-in for the few libnx calls game/src/nv2a_deko3d.c uses. Test only. */
#ifndef STUB_SWITCH_H
#define STUB_SWITCH_H
#include <stddef.h>
#include <stdint.h>
static inline uint64_t armGetSystemTick(void) { static uint64_t t; return t += 1920; }
static inline void svcOutputDebugString(const char *s, size_t n) { (void)s; (void)n; }
#endif
