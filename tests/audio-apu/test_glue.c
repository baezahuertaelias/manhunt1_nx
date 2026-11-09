#define _GNU_SOURCE
#include <assert.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include "audio_mmio.h"
#include "apu.h"
#include "recomp_types.h"
ptrdiff_t g_xbox_mem_offset;
RECOMP_TLS uint32_t g_eax, g_esp;
MCPXAPUState *g_apu_state;
static int device_ok=1, starts, stops, reads, writes;
static unsigned last_width;
static uint32_t last_offset,last_value;
int xa2_init(void) { return device_ok; }
void xa2_shutdown(void) {}
MCPXAPUState *mcpx_apu_init_standalone(uint8_t *p) { assert(p);starts++;return (MCPXAPUState*)1; }
void mcpx_apu_shutdown(MCPXAPUState *p) { assert(p);assert(!manhunt_audio_enabled());stops++; }
uint64_t mcpx_apu_mmio_read(MCPXAPUState *p,uint64_t a,unsigned w) { assert(p);reads++;last_offset=a;last_width=w;return 0x12345678; }
void mcpx_apu_mmio_write(MCPXAPUState *p,uint64_t a,uint64_t v,unsigned w) { assert(p);writes++;last_offset=a;last_width=w;last_value=v; }
static void dies(int which) {
 pid_t child=fork(); assert(child>=0);
 if(!child) {
  if(which==0) manhunt_audio_write8(0xFE800000,1);
  if(which==1) manhunt_audio_read32(0xFE800001);
  if(which==2) manhunt_audio_raw_ptr(0xFE7FFFFF,2);
  if(which==3) manhunt_audio_raw_ptr(0xFFFFFFFE,4);
  if(which==4) manhunt_audio_read16(0xFE7FFFFF);
  if(which==5) manhunt_audio_write32(0xFFFFFFFE,1);
  if(which==6) manhunt_audio_write16(0xFEC01000-1,1);
  if(which==7) manhunt_audio_raw_ptr(0xFEC0010B,1);
  _exit(0);
 }
 int status;assert(waitpid(child,&status,0)==child);assert(WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);
}
int main(void) {
 assert(prctl(PR_SET_DUMPABLE, 0) == 0); /* piped core handlers ignore RLIMIT_CORE */
 void *base=mmap(0,0x100000000ULL,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE,-1,0);assert(base!=MAP_FAILED);
 g_xbox_mem_offset=(ptrdiff_t)base;
 unsetenv("MANHUNT_AUDIO_APU");assert(manhunt_audio_init(base)&&!manhunt_audio_enabled()&&!starts);
 /* Gate preserves stdcall ABI, including the output and caller's canary. */
 const uint32_t entry[]={0x225757,0x22579E,0x223C31};
 const unsigned bytes[]={12,8,16}, outarg[]={1,1,3};
 for(unsigned i=0;i<3;i++) {g_esp=0x10000;MEM32(g_esp+4+outarg[i]*4)=0x11000;MEM32(0x11000)=0xdead;MEM32(0x11004)=0xbeef;assert(manhunt_audio_guest_gate(entry[i]));assert(g_eax==0x88780078&&g_esp==0x10000+4+bytes[i]);assert(!MEM32(0x11000)&&MEM32(0x11004)==0xbeef);}
 manhunt_audio_write32(0xFE800010,0x777);assert(manhunt_audio_read32(0xFE800010)==0x777&&!reads&&!writes);
 manhunt_audio_write8(0xFE800010,0x88);assert(manhunt_audio_read8(0xFE800010)==0x88);
 manhunt_audio_write16(0xFE800010,0x9999);assert(manhunt_audio_read16(0xFE800010)==0x9999);
 setenv("MANHUNT_AUDIO_APU","typo",1);assert(!manhunt_audio_configure());
 setenv("MANHUNT_AUDIO_APU","1",1);setenv("RECOMP_AC97_READY","0",1);assert(!manhunt_audio_configure());unsetenv("RECOMP_AC97_READY");
 device_ok=0;assert(!manhunt_audio_init(base)&&!manhunt_audio_enabled()&&!starts);device_ok=1;
 assert(manhunt_audio_init(base)&&manhunt_audio_enabled()&&starts==1);assert(MEM32(0xFEC00130)==0x100);assert(!manhunt_audio_guest_gate(0x225757));
 assert(manhunt_audio_read32(0xFE820010)==0x12345678&&reads==1&&last_offset==0x20010&&last_width==4);
 assert(manhunt_audio_read8(0xFE801101)==0x56&&last_offset==0x1100);
 assert(manhunt_audio_read16(0xFE801102)==0x1234&&last_offset==0x1100);
 manhunt_audio_write32(0xFE820280,0x888);assert(writes==1&&last_offset==0x20280&&last_width==4&&last_value==0x888);
 manhunt_audio_write8(0x12000,0xab);manhunt_audio_write16(0x12002,0xcdef);assert(manhunt_audio_read8(0x12000)==0xab&&manhunt_audio_read16(0x12002)==0xcdef);
 manhunt_audio_write32(0xFE830100,0x999);assert(MEM32(0xFE830100)==0x999&&writes==1);assert(manhunt_audio_raw_ptr(0xFE830000,4)==(char*)base+0xFE830000);
 manhunt_audio_write32(0xFE83FFFC,3);assert(writes==2&&last_offset==0x3FFFC&&last_value==3);assert(manhunt_audio_read32(0xFE83FFFC)==3);
 for(uint32_t off=0x10B;off<0x180;off+=0x10) {
  manhunt_audio_write8(0xFEC00000+off,3);assert(manhunt_audio_read8(0xFEC00000+off)==1);
  manhunt_audio_write16(0xFEC00000+off-1,0x0307);assert(manhunt_audio_read16(0xFEC00000+off-1)==0x0107);
  manhunt_audio_write32(0xFEC00000+off-3,0x03070809);assert(manhunt_audio_read32(0xFEC00000+off-3)==0x01070809);
 }
 manhunt_audio_write8(0xFEC00108,2);assert(manhunt_audio_read8(0xFEC00108)==2);
 for(int i=0;i<8;i++) dies(i);
 manhunt_audio_shutdown();assert(!manhunt_audio_enabled()&&!g_apu_state&&stops==1);manhunt_audio_shutdown();assert(stops==1);
 munmap(base,0x100000000ULL);puts("PASS audio glue: rollback ABI, output failure, register routing, RAM widths, GP storage, rejection and shutdown");
}
