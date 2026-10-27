#!/usr/bin/env python3
"""Gate (a)/(b) reference: run original XBE functions in Unicorn (x86-32).

Generates deterministic test vectors, executes the original guest bytes from
DEFAULT.xbe, and writes:
  <build>/vectors.h      cases consumed by harness.c
  <build>/reference.txt  expected output, one line per case
  <build>/reference.h    the same lines as C strings, for the Switch build

Usage: python3 -I reference.py <repo-root> <DEFAULT.xbe> <build-dir>
"""
import random
import struct
import sys
from fractions import Fraction

from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_PROT_ALL
from unicorn.x86_const import (
    UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDX,
    UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP, UC_X86_REG_ESP,
    UC_X86_REG_EIP, UC_X86_REG_FPCW, UC_X86_REG_FPSW, UC_X86_REG_FP0,
)

GUEST_MEM = 0x04000000          # 64 MiB, the retail Xbox RAM size
STACK_TOP = 0x00D00000
RET_SENTINEL = 0x03FFF000
FNINIT_STUB = 0x03FFE000        # fninit, used to reset the x87 unit per case
DATA = 0x00E00000

VECSUB = 0x0001A9C0             # out = a - b on float[3], cdecl
CTOR = 0x00089CF0               # thiscall(this, const float *v, float s), ret 8
PLANE = 0x000E3640              # returns dot(n, p) - d in st(0), cdecl
STRLEN = 0x00205A00             # CRT strlen
ALLMUL = 0x00205D90             # CRT _allmul (64-bit), ret 0x10
AULLDIV = 0x00205040            # CRT _aulldiv (unsigned 64/64), ret 0x10; used by time()

# x87 control words: FNINIT default (64-bit PC), 53-bit PC, 24-bit PC.
CONTROL_WORDS = (0x037F, 0x027F, 0x007F)

F32_SPECIAL = [
    0x00000000, 0x80000000, 0x3F800000, 0xBF800000,  # +-0, +-1
    0x00000001, 0x007FFFFF, 0x7F7FFFFF,              # denormals, max
    0x7F800000, 0xFF800000,                          # +-inf
]
F32_NAN = [0x7FC00000, 0x7FC12345, 0x7F812345]       # qNaN, payload qNaN, sNaN

REGS = (UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EBX,
        UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_ESP)


def f32(x):
    return struct.unpack("<I", struct.pack("<f", x))[0]


class Case:
    def __init__(self, fn, label, cw=0x037F):
        self.fn, self.label, self.cw = fn, label, cw
        self.regs = {"ecx": 0x0C0C0C0C, "ebx": 0xB0B0B0B0, "esi": 0x5E5E5E5E, "edi": 0xD1D1D1D1}
        self.pushes = []     # in push order (last pushed = first argument)
        self.mem = []        # (va, bytes)
        self.out = (0, 0)    # (va, len) dumped after the call


def make_cases(rng):
    cases = []

    def rand_f32():
        return f32(rng.uniform(-1e6, 1e6) * rng.choice((1, 1e-30, 1e30)))

    vec_pool = F32_SPECIAL + [rand_f32() for _ in range(23)]
    for i in range(48):
        for cw in CONTROL_WORDS if i < 16 else (0x037F,):
            c = Case(VECSUB, "num", cw)
            a = [rng.choice(vec_pool) for _ in range(3)]
            b = [rng.choice(vec_pool) for _ in range(3)]
            c.mem = [(DATA + 0x100, struct.pack("<3I", *a)),
                     (DATA + 0x200, struct.pack("<3I", *b)),
                     (DATA, b"\xCC" * 16)]
            c.pushes = [DATA + 0x200, DATA + 0x100, DATA]
            c.out = (DATA, 16)
            cases.append(c)
    for n in F32_NAN:        # NaN propagation, reported separately
        c = Case(VECSUB, "nan")
        c.mem = [(DATA + 0x100, struct.pack("<3I", n, 0x3F800000, n)),
                 (DATA + 0x200, struct.pack("<3I", 0x3F800000, n, F32_NAN[0])),
                 (DATA, b"\xCC" * 16)]
        c.pushes = [DATA + 0x200, DATA + 0x100, DATA]
        c.out = (DATA, 16)
        cases.append(c)

    for i in range(32):
        c = Case(CTOR, "num", CONTROL_WORDS[i % 3])
        this, vec = DATA + 0x300, DATA + 0x400
        c.regs["ecx"] = this
        c.mem = [(this, bytes(rng.getrandbits(8) for _ in range(0x24))),
                 (vec, struct.pack("<3I", *(rng.choice(vec_pool) for _ in range(3))))]
        c.pushes = [rng.choice(vec_pool), vec]
        c.out = (this, 0x24)
        cases.append(c)

    # x87 precision: three products summed, then a subtraction, left in st(0).
    # Mixed magnitudes force rounding in the intermediate sums.
    for cw in CONTROL_WORDS:
        for i in range(60):
            c = Case(PLANE, "x87", cw)
            p, pl = DATA + 0x500, DATA + 0x600
            vals = [rng.uniform(-1, 1) * 10 ** rng.randrange(-3, 6) for _ in range(7)]
            c.mem = [(p, struct.pack("<3f", *vals[:3])),
                     (pl, b"\0" * 12 + struct.pack("<4f", *vals[3:]))]
            c.pushes = [pl, p]
            cases.append(c)

    for align in range(4):
        for length in list(range(0, 12)) + [rng.randrange(12, 200) for _ in range(6)]:
            c = Case(STRLEN, "num")
            s = DATA + 0x1000 + align
            body = bytes(rng.randrange(1, 256) for _ in range(length))
            tail = bytes(rng.randrange(0, 256) for _ in range(8))
            c.mem = [(s, body + b"\0" + tail)]
            c.pushes = [s]
            cases.append(c)

    pairs = [(0, 0), (1, 1), (0xFFFFFFFF, 0xFFFFFFFF), (0xFFFFFFFFFFFFFFFF, 2),
             (0x100000000, 0x100000000), (0x123456789, 0)]
    pairs += [(rng.getrandbits(64), rng.getrandbits(64)) for _ in range(40)]
    pairs += [(rng.getrandbits(32), rng.getrandbits(32)) for _ in range(10)]
    for a, b in pairs:
        c = Case(ALLMUL, "num")
        c.pushes = [b >> 32, b & 0xFFFFFFFF, a >> 32, a & 0xFFFFFFFF]
        cases.append(c)

    # Unsigned 64/64 division: 32-bit divisors (time(): FILETIME / 10^7) and
    # wide divisors (the shift path).
    filetime = 0x01DD0000_00000000 + rng.getrandbits(48)
    divs = [(filetime, 10_000_000), (filetime - 116444736000000000, 10_000_000),
            (0xFFFFFFFFFFFFFFFF, 1), (0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF), (5, 7)]
    divs += [(rng.getrandbits(64), rng.getrandbits(32) | 1) for _ in range(20)]
    divs += [(rng.getrandbits(64), rng.getrandbits(64) | (1 << 32)) for _ in range(20)]
    for a, b in divs:
        c = Case(AULLDIV, "num")
        c.pushes = [b >> 32, b & 0xFFFFFFFF, a >> 32, a & 0xFFFFFFFF]
        cases.append(c)
    return cases


def load_xbe(uc, xbe_path, repo):
    sys.path.insert(0, f"{repo}/analysis")
    from xbe_header import Xbe
    data = open(xbe_path, "rb").read()
    x = Xbe(data)
    uc.mem_write(x.base, data[:x.headers_size])
    for s in x.sections:
        uc.mem_write(s["va"], data[s["raw"]:s["raw"] + s["raw_size"]])
    return data


SCRATCH = ((STACK_TOP - 0x1000, 0x1000), (DATA, 0x2000))


def run(uc, case):
    # Same per-case reset as harness.c: the stack page and the data scratch
    # area are zeroed. The XBE image is loaded once; these functions only
    # write to the stack and to DATA.
    for va, n in SCRATCH:
        uc.mem_write(va, b"\0" * n)
    for va, blob in case.mem:
        uc.mem_write(va, blob)
    esp = STACK_TOP
    for v in case.pushes:
        esp -= 4
        uc.mem_write(esp, struct.pack("<I", v))
    esp -= 4
    uc.mem_write(esp, struct.pack("<I", RET_SENTINEL))
    uc.reg_write(UC_X86_REG_EAX, 0xEAEAEAEA)
    uc.reg_write(UC_X86_REG_EDX, 0xEDEDEDED)
    uc.reg_write(UC_X86_REG_EBP, 0xBEBEBEBE)
    uc.reg_write(UC_X86_REG_ECX, case.regs["ecx"])
    uc.reg_write(UC_X86_REG_EBX, case.regs["ebx"])
    uc.reg_write(UC_X86_REG_ESI, case.regs["esi"])
    uc.reg_write(UC_X86_REG_EDI, case.regs["edi"])
    uc.reg_write(UC_X86_REG_ESP, esp)
    uc.emu_start(FNINIT_STUB, FNINIT_STUB + 2)
    uc.reg_write(UC_X86_REG_FPCW, case.cw)
    uc.emu_start(case.fn, RET_SENTINEL, count=100000)
    eip = uc.reg_read(UC_X86_REG_EIP)
    if eip != RET_SENTINEL:
        raise RuntimeError(f"case fn={case.fn:08x}: stopped at {eip:08x}, not the return sentinel")
    regs = [uc.reg_read(r) for r in REGS]
    va, n = case.out
    mem = bytes(uc.mem_read(va, n)) if n else b""
    top = uc.reg_read(UC_X86_REG_FPSW) >> 11 & 7
    st0 = f80_to_f64_bits(*uc.reg_read(UC_X86_REG_FP0 + top)) if top else None
    scratch = fnv1a64(b"".join(bytes(uc.mem_read(a, n)) for a, n in SCRATCH))
    return regs, mem, top, st0, scratch


def fnv1a64(data):
    """FNV-1a 64 over the whole scratch state; harness.c computes the same."""
    h = 0xCBF29CE484222325
    for b in data:
        h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def f80_to_f64_bits(mantissa, exp_sign):
    """80-bit x87 value -> IEEE double bits, round to nearest even."""
    sign = -1 if exp_sign & 0x8000 else 1
    e = exp_sign & 0x7FFF
    if e == 0:
        e = 1                              # 80-bit denormal: same scale as the smallest normal
    elif e == 0x7FFF:
        if mantissa & 0x7FFFFFFFFFFFFFFF:
            # NaN: keep sign and top 51 payload bits, as the x87 -> double store does.
            return (0x8000000000000000 if sign < 0 else 0) | 0x7FF8000000000000 | ((mantissa >> 11) & 0x7FFFFFFFFFFFF)
        return struct.unpack("<Q", struct.pack("<d", sign * float("inf")))[0]
    value = sign * Fraction(mantissa) * Fraction(2) ** (e - 16383 - 63)
    try:
        f = float(value)                   # Fraction -> float rounds correctly
    except OverflowError:
        f = sign * float("inf")
    if f == 0 and sign < 0:
        f = -0.0
    return struct.unpack("<Q", struct.pack("<d", f))[0]


def fmt(i, case, regs, mem, top, st0, scratch):
    names = ("eax", "ecx", "edx", "ebx", "esi", "edi", "esp")
    r = " ".join(f"{k}={v:08x}" for k, v in zip(names, regs))
    s = f"{st0:016x}" if st0 is not None else "-"
    return (f"{i:04d} fn={case.fn:08x} cw={case.cw:04x} {case.label} {r} top={top} st0={s} "
            f"scr={scratch:016x} mem={mem.hex()}")


def c_bytes(blob):
    return ",".join(str(b) for b in blob)


def write_vectors(path, cases):
    with open(path, "w") as f:
        f.write("/* Generated by tests/recomp-proof/reference.py -- do not edit. */\n")
        f.write("#define CASE_COUNT %d\n" % len(cases))
        for i, c in enumerate(cases):
            for j, (va, blob) in enumerate(c.mem):
                f.write(f"static const uint8_t m{i}_{j}[] = {{{c_bytes(blob)}}};\n")
        f.write("static const struct proof_case cases[CASE_COUNT] = {\n")
        for i, c in enumerate(cases):
            mem = ",".join(f"{{0x{va:08x}u,{len(blob)}u,m{i}_{j}}}" for j, (va, blob) in enumerate(c.mem))
            pushes = ",".join(f"0x{v:08x}u" for v in c.pushes)
            f.write(f"  {{0x{c.fn:08x}u,0x{c.cw:04x}u,\"{c.label}\","
                    f"0x{c.regs['ecx']:08x}u,0x{c.regs['ebx']:08x}u,0x{c.regs['esi']:08x}u,0x{c.regs['edi']:08x}u,"
                    f"{len(c.pushes)},{{{pushes}}},{len(c.mem)},{{{mem}}},0x{c.out[0]:08x}u,{c.out[1]}u}},\n")
        f.write("};\n")


def main(repo, xbe_path, build):
    rng = random.Random(0x4D48)
    cases = make_cases(rng)
    uc = Uc(UC_ARCH_X86, UC_MODE_32)
    uc.mem_map(0, GUEST_MEM, UC_PROT_ALL)
    load_xbe(uc, xbe_path, repo)
    uc.mem_write(FNINIT_STUB, b"\xDB\xE3")
    lines = [fmt(i, c, *run(uc, c)) for i, c in enumerate(cases)]
    with open(f"{build}/reference.txt", "w") as out:
        out.write("".join(line + "\n" for line in lines))
    with open(f"{build}/reference.h", "w") as out:     # for the on-device check
        out.write("/* Generated by tests/recomp-proof/reference.py -- do not edit. */\n")
        out.write("static const char *const reference_lines[CASE_COUNT] = {\n")
        out.write("".join(f'  "{line}",\n' for line in lines))
        out.write("};\n")
    write_vectors(f"{build}/vectors.h", cases)
    print(f"reference: {len(cases)} cases")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    main(*sys.argv[1:])
