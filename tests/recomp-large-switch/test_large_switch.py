"""Synthetic local-switch regression; no game inputs. Usage: python this.py <xboxrecomp>."""
import pathlib
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(sys.argv.pop(1)).resolve()))
from tools.recomp import config
from tools.recomp.lifter import Lifter
from tools.recomp.translator import FunctionTranslator

BASE = 0x10000

def fixture(count):
    # Saved EBX and one shared epilogue expose accidental indirect tail returns.
    prefix_size = 1 + 2 + 5 + 6 + 7
    arms = [BASE + prefix_size + i * 10 for i in range(count)]
    default = BASE + prefix_size + count * 10
    epilogue = default + 5
    table = epilogue + 2
    image = b'\x53\x8b\xdc\x3d' + struct.pack('<I', count - 1)
    image += b'\x0f\x87' + struct.pack('<i', default - (BASE + len(image) + 6))
    image += b'\xff\x24\x85' + struct.pack('<I', table)
    for i, arm in enumerate(arms):
        assert BASE + len(image) == arm
        image += b'\xb8' + struct.pack('<I', i)
        image += b'\xe9' + struct.pack('<i', epilogue - (BASE + len(image) + 5))
    image += b'\xb8\xff\xff\xff\xff\x5b\xc3'
    image += b''.join(struct.pack('<I', a) for a in arms)
    return image, arms, table


def install(image):
    config._install([config.Section('.text', BASE, len(image), 0, len(image), True)],
                    entry_point=BASE, kernel_thunk_addr=BASE, origin='large-switch-test')


def run():
    image, arms, table = fixture(300)
    install(image)
    db = {BASE: {'start': hex(BASE), 'end': table, '_addr': BASE, 'size': table - BASE}}
    tr = FunctionTranslator(image, db)
    code = tr.translate_function(BASE, db[BASE])
    assert code
    for arm in arms:
        assert f'goto loc_{arm:08X};' in code, f'missing arm {arm:#x}'
    assert 'switch: 300 entries' in code
    # Readers retain their hard ceiling, mapped-input and owner bounds.
    many = struct.pack('<I', BASE) * 4097
    install(many)
    lifter = Lifter(); lifter.xbe_data = many
    assert len(lifter._read_jump_table(BASE)) == 4096
    assert len(lifter._read_jump_table_backward(BASE + 4096 * 4)) == 4096
    assert len(tr._read_local_jump_table(BASE, BASE, BASE + 1)) == 0  # old image bytes invalid here
    tr.xbe_data = many
    assert len(tr._read_local_jump_table(BASE, BASE, BASE + 1)) == 4096
    tr.xbe_data = struct.pack('<III', BASE, BASE, BASE + 100)
    install(tr.xbe_data)
    assert tr._read_local_jump_table(BASE, BASE, BASE + 1) == [BASE, BASE]
    assert lifter._read_jump_table(BASE + len(many)) == []
    # Compile the translated 300-arm function and execute every index, including
    # former omitted arm269 and the out-of-range path, with stack/callee-save checks.
    harness = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
uint32_t eax,ebx,ecx,edx,esi,edi,esp,g_ebp,g_seh_ebp;
uint8_t mem[0x20000];
#define MEM32(a) (*(uint32_t *)(mem + (uint32_t)(a)))
#define PUSH32(s,v) do{(s)-=4; MEM32(s)=(v);}while(0)
#define POP32(s,v) do{(v)=MEM32(s);(s)+=4;}while(0)
#define CMP_A(a,b) ((uint32_t)(a)>(uint32_t)(b))
#define RECOMP_ITAIL(...) abort()
#define RECOMP_ICALL_SAFE_AT(...) abort()
#define RECOMP_ICALL_SAFE(...) abort()
'''
    harness += code
    harness += "\nstatic const unsigned char image[]={" + ",".join(str(v) for v in image) + "};\n"
    harness += r'''
int main(void){
 memcpy(mem+0x10000,image,sizeof(image));
 for(uint32_t i=0;i<=300;i++){
  eax=i; ebx=0x12345678; esp=0x18000; MEM32(esp)=0xDEADBEEF;
  sub_00010000();
  if(eax!=(i<300?i:UINT32_MAX)||ebx!=0x12345678||esp!=0x18004)return 1;
 }
 puts("PASS 301 executions, 300 local arms, bounds retained"); return 0;
}
'''
    with tempfile.TemporaryDirectory() as d:
        p = pathlib.Path(d); (p/'proof.c').write_text(harness)
        subprocess.run(['cc','-std=c11','-O1',str(p/'proof.c'),'-o',str(p/'proof')],check=True)
        subprocess.run([str(p/'proof')],check=True)

if __name__ == '__main__':
    run()
