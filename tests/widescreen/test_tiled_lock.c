#define _POSIX_C_SOURCE 200809L
#include "recomp_types.h"
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

_Alignas(8) unsigned char test_ram[8192];
uint32_t g_eax,g_ecx,g_edx,g_ebx,g_esi,g_edi,g_esp;
enum { STACK=256, OUTPUT=1024 };
static uint32_t result_pointer;
static unsigned original_calls;
extern void __wrap_sub_00295EC0(void);
void __real_sub_00295EC0(void)
{
    assert(g_esp==STACK&&g_esi==OUTPUT);
    assert(MEM32(STACK)==0x12345678&&MEM32(STACK+4)==5);
    assert(MEM32(STACK+8)==7&&MEM32(STACK+12)==11);
    MEM32(g_esi)=2560; MEM32(g_esi+4)=result_pointer;
    g_esp+=20;g_eax=0;g_ecx=29;g_edx=31;
    ++original_calls;
}
static void lock_case(uint32_t flags,uint32_t ptr,uint32_t expected)
{
    memset(test_ram,0xa5,sizeof test_ram);
    g_esp=STACK;g_ebx=17;g_esi=OUTPUT;g_edi=23;
    MEM32(STACK)=0x12345678;MEM32(STACK+4)=5;MEM32(STACK+8)=7;
    MEM32(STACK+12)=11;MEM32(STACK+16)=flags;
    unsigned char before[sizeof test_ram];memcpy(before,test_ram,sizeof before);
    result_pointer=ptr;unsigned count=original_calls;
    __wrap_sub_00295EC0();
    assert(original_calls==count+1);
    assert(g_esp==STACK+20&&g_eax==0&&g_ecx==29&&g_edx==31);
    assert(g_ebx==17&&g_esi==OUTPUT&&g_edi==23);
    assert(MEM32(OUTPUT)==2560&&MEM32(OUTPUT+4)==expected);
    for(unsigned i=0;i<sizeof test_ram;i++)
        if(i<OUTPUT||i>=OUTPUT+8) assert(test_ram[i]==before[i]);
}
static void bad_alias(uint32_t ptr)
{
    pid_t child=fork();assert(child>=0);
    /* If the wrapper wrongly returns, avoid a fixture assertion masking it
     * as the expected SIGABRT. A missing-bounds mutant remaps this value. */
    if(!child) {lock_case(0x40,ptr,ptr-0x70000000u);_exit(0);}
    int status;assert(waitpid(child,&status,0)==child);
    assert(WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);
}
int main(void)
{
    lock_case(0,0xf0bef000,0xf0bef000);
    lock_case(0x40,0xf0000000,0x80000000);
    lock_case(0x40,0xf0bef000,0x80bef000);
    lock_case(0x40,0xf3ffffff,0x83ffffff);
    lock_case(0x40,0x80bef000,0x80bef000);
    lock_case(0x40,0x10000,0x10000);
    lock_case(0x4000,0xf0bef000,0xf0bef000); /* byte-sized flags, not whole dword */
    lock_case(0x140,0xf0bef000,0x80bef000);
    bad_alias(0xf4000000);bad_alias(0xffffffff);
    puts("PASS production tiled lock: flags, alias bounds, ABI, pitch and canaries");
    return 0;
}
