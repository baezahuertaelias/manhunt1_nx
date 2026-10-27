#!/usr/bin/env python3
"""Write a gdb script that finds which call inside a lifted function returns
with a callee-saved guest register (ebx/esi/edi) changed.

Usage: python3 -I scripts/find-reg-leak.py <sub_XXXXXXXX> [reg] > cmd.gdb
Run the script with gdb against game/build/host/manhunt (see FEASIBILITY.md).
For every call in the function's generated C it saves the register before
the call and, at the return label, reports the first callee that changed it.
"""
import glob
import os
import re
import sys

func = sys.argv[1]
reg = sys.argv[2] if len(sys.argv) > 2 else "ebx"
gen = "game/build/gen"
src = None
for path in sorted(glob.glob(f"{gen}/*.c")):
    with open(path) as fh:
        text = fh.read()
    if f"\nvoid {func}(void)\n" in text:
        src, lines = path, text.split("\n")
        break
if not src:
    sys.exit(f"{func} not found in {gen}")
start = lines.index(f"void {func}(void)")
end = next(i for i in range(start, len(lines)) if lines[i] == "}")
base = os.path.basename(src)

print("set pagination off\nset confirm off")
for s in ("SIGSEGV", "SIGBUS", "SIG32", "SIGUSR1", "SIGUSR2"):
    print(f"handle {s} nostop noprint pass")
sites = 0
for i in range(start, end):
    line = lines[i]
    m = re.search(r"RECOMP_ABI_CALL\(0x([0-9A-F]{8})u|\b(sub_[0-9A-F]{8})\(\);", line)
    ind = re.search(r"_icall_target = MEM32\((0x[0-9A-Fa-f]+)\)", line)
    if not m and not ind:
        continue
    if ind:
        # Name the target the pointer holds at the moment the return is checked.
        callee = f"icall [{ind.group(1)}]=%08x"
        extra = f", *(unsigned*)(g_xbox_mem_offset + {ind.group(1)})"
    else:
        callee = m.group(1) or m.group(2)
        extra = ""
    j = i + 1
    while j < end and not lines[j].startswith("loc_"):
        j += 1
    if j >= end:
        continue
    # before: the call line itself; after: first statement of the return label
    print(f"break {base}:{i + 1}\ncommands\nsilent\nset $before_{sites} = g_{reg}\ncontinue\nend")
    print(f"break {base}:{j + 2}\ncommands\nsilent\n"
          f"if g_{reg} != $before_{sites}\n"
          f"printf \"=== {func}: call {callee} changed {reg} %08x -> %08x (return {lines[j][:12]})\\n\"{extra}, $before_{sites}, g_{reg}\n"
          f"kill\nquit\nend\ncontinue\nend")
    sites += 1
print("run /game > /dev/null 2>&1")
sys.stderr.write(f"{func}: {sites} call sites in {base}\n")
