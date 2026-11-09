#ifndef WIDE_TEST_RECOMP_TYPES_H
#define WIDE_TEST_RECOMP_TYPES_H
#include <stdint.h>
extern uint32_t g_eax, g_ecx, g_edx, g_ebx, g_esi, g_edi, g_esp;
extern unsigned char test_ram[8192];
#define MEM32(a) (*(uint32_t *)(test_ram + (uint32_t)(a)))
#define MEM8(a) (test_ram[(uint32_t)(a)])
#endif
