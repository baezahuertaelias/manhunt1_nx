"""Synthetic audio-bus lowering checks; run against a patched xboxrecomp tree."""
import os, pathlib, subprocess, sys, tempfile, unittest
sys.path.insert(0,sys.argv.pop(1))
from tools.recomp.audio_mmio import lower_function, selected
from tools.recomp.lifter import Lifter
from tools.recomp.disasm import Disassembler
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ESI

class AudioMMIO(unittest.TestCase):
    def test_scope_and_gate(self):
        code='void f(void) { MEM32(eax) = MEM32(ebx); }'
        self.assertEqual(lower_function(code,0x11000),code)
        self.assertTrue(selected(0x2A411E))
        self.assertFalse(selected(0x2A4135))
        for addr in (0x225757,0x22579E,0x223C31):
            out=lower_function(code,addr)
            self.assertIn('manhunt_audio_guest_gate(0x%08Xu)'%addr,out)
            self.assertLess(out.index('guest_gate'),out.index('write32'))

    def test_tokens_nested_calls_and_signed(self):
        source='void f(void) { /* MEM32(x) = hidden; */ MEM16(eax + MEM32(ebx)) = fn(MEM8(ecx), 4); edx = SMEM16(esi); puts("MEM32(x)"); }'
        out=lower_function(source,0x222820)
        self.assertIn('/* MEM32(x) = hidden; */',out)
        self.assertIn('puts("MEM32(x)")',out)
        self.assertIn('manhunt_audio_write16',out)
        self.assertIn('((int16_t)manhunt_audio_read16',out)
        self.assertEqual(out.count('manhunt_audio_read32'),1)

    def test_unknown_writes_and_pointer_escapes_fail(self):
        for code in ('MEM32(eax) += 1;', 'MEM32(eax)++;', 'void *p = XBOX_PTR(eax);','foo(MEM32(eax) = 1, 2);', 'void *p = &MEM32(eax);', 'eax = MMX_LOAD(esi);'):
            with self.assertRaisesRegex(ValueError,'audio MMIO:'):
                lower_function('void f(void) {'+code+'}',0x222820)

    def test_float_and_atomic_fail_closed_bus_guard(self):
        out=lower_function('void f(void) { MEMF(eax) = 1.0f; eax = RECOMP_ATOMIC_CAS32(XBOX_PTR(ecx), 1, 2); }',0x222820)
        self.assertIn('manhunt_audio_raw_ptr((uint32_t)(eax), 4)',out)
        self.assertIn('manhunt_audio_raw_ptr((uint32_t)(ecx), 4)',out)

    def test_capstone_lift_c_vs_unicorn_dynamic_rmw(self):
        # Hand-authored x86: MOV [ESI],EAX; ADD [ESI],7; MOV EAX,[ESI].
        raw=bytes.fromhex('89068306078b06')
        dis=Disassembler(); lf=Lifter(); lf.func_start=0x222820
        insns=dis.disassemble_function(raw,0x222820,0x222820+len(raw))
        body='\n'.join(line for i in insns for line in lf.lift_instruction(i))
        code=lower_function('void run(void) {'+body+'}',0x222820)
        self.assertIn('manhunt_audio_write32',code)
        self.assertIn('manhunt_audio_read32',code)
        uc=Uc(UC_ARCH_X86,UC_MODE_32);uc.mem_map(0x222000,0x2000);uc.mem_map(0xFE820000,0x1000)
        uc.mem_write(0x222820,raw);uc.reg_write(UC_X86_REG_EAX,0x12345678);uc.reg_write(UC_X86_REG_ESI,0xFE820200)
        uc.emu_start(0x222820,0x222820+len(raw))
        expected=(uc.reg_read(UC_X86_REG_EAX),int.from_bytes(uc.mem_read(0xFE820200,4),'little'))
        prelude='''#include <stdint.h>
#include <stdio.h>
uint32_t eax=0x12345678,esi=0xFE820200,word=0,_fa,_fb;
int32_t _fas,_fbs; int _cf; unsigned reads,writes;
uint32_t manhunt_audio_read32(uint32_t a) { if(a!=0xFE820200) __builtin_trap();reads++;return word; }
void manhunt_audio_write32(uint32_t a,uint32_t v) { if(a!=0xFE820200) __builtin_trap();writes++;word=v; }
'''
        with tempfile.TemporaryDirectory() as tmp:
            src=pathlib.Path(tmp)/'case.c';exe=pathlib.Path(tmp)/'case'
            src.write_text(prelude+code+'\nint main(void){run();printf("%u %u %u %u\\n",eax,word,reads,writes);}')
            subprocess.run(['cc','-std=c11','-Wall','-Werror',str(src),'-o',str(exe)],check=True,capture_output=True)
            got=tuple(map(int,subprocess.check_output([str(exe)],text=True).split()))
        self.assertEqual(got[:2],expected)
        self.assertEqual(got[3],2)
        self.assertGreaterEqual(got[2],2)

    def test_strings_have_no_pointer_escape(self):
        lf=Lifter();lf.func_start=0x222820
        for size in (1,2,4):
            source='void f(void) {'+'\n'.join(lf._lift_rep_movs(size,'rep movs'))+'}'
            out=lower_function(source,lf.func_start)
            self.assertNotIn('XBOX_PTR',out)
            self.assertIn('manhunt_audio_write%d'%(size*8),out)
            self.assertIn('manhunt_audio_read%d'%(size*8),out)
        lf.func_start=0x11000
        self.assertIn('memcpy','\n'.join(lf._lift_rep_movs(4,'rep movsd')))

if __name__=='__main__':unittest.main()
