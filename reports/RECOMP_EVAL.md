# Recompiler evaluation and milestone 1 proofs

Date: 2026-10-26. Task: RECOMP-EVAL-001. Rerun everything with `scripts/toolchain-proof.sh`.

## Candidates

Both candidates were cloned at the commits below and inspected before use. Neither was run on the host.

| | sp00nznet/xboxrecomp | TGP482/XboxXBERecomp |
|---|---|---|
| Commit | `f0f8a461496a4f23d9b0aeaa284b8d70ba7ab82b` | `0fd00442660573dc342b41c11c6574318f0e8698` |
| License | MIT. xemu-derived APU/NV2A files are LGPL-2.1+; we don't use them. | MIT |
| Lifter | Python + capstone → C | C++ + Zydis → C++ |
| Output | `void sub_X(void)` over global registers and a simulated stack | Overridable C++ functions |
| Guest memory | `XBOX_PTR(va) = (uint32)va + g_xbox_mem_offset` | — (not evaluated) |
| x87 | 8-entry `double` stack; `RECOMP_FP_PC` honours 24-bit precision control | Run-time x87 stack, "x87 precision" support |
| Runtime hosts | Windows; libraries also build for Linux/macOS x86-64 and arm64 | Windows (D3D12, XAudio2); a headless Linux harness |
| Relevant to Manhunt | RenderWare function classifier; LTCG D3D notes | XDK **5849** signatures (Manhunt's XDK); LTCG support |

**Chosen for the gate proofs: xboxrecomp.** Its generated C needs no host-specific intrinsics on AArch64, and its address model ports to Horizon without change. XboxXBERecomp's 5849 signature database remains a candidate input for identifying XDK functions in milestone 2 (FEASIBILITY-001).

## Pipeline run on DEFAULT.xbe

Image `manhunt-recompiler:xboxrecomp-f0f8a46` (`infra/recompiler/Dockerfile`). It runs with no network, the XBE mounted read-only, and 3 CPU / 8 GiB.

| Step | Result |
|---|---|
| `xbe_parser` | OK. **Its log prints the certificate LAN and signature keys:** logs stay in gitignored `build/`. |
| `disasm` | 13,396 functions, 155,220 xrefs, 177 s |
| `func_id` | 747 RenderWare-identified functions; 11 CRT functions (`strlen`, `_allmul`, `memmove`, `_ftol`, …) |
| `abi_analysis` | 13,396 entries; 0 thiscall detected. The ABI tags are unreliable; the lifted code uses the simulated stack, so it doesn't depend on them. |
| `recomp -f` | about 76 s per function (the whole database is reloaded each time). Output is cached by XBE hash + toolkit commit + function list. |

## Gate (a)/(b) proof

Five functions, chosen to cover different risks:

| VA | What | Exercises |
|---|---|---|
| `0x0001A9C0` | float[3] subtract, 239 callers (RenderWare region) | x87 load/sub/store, three guest pointers, cdecl |
| `0x00089CF0` | thiscall initializer, `ret 8` | `ecx` this, x87 multiply, struct writes, push/pop `esi` |
| `0x000E3640` | plane distance `dot(n,p) − d`, result in `st(0)` | x87 precision control, register-resident result |
| `0x00205A00` | CRT `strlen` | alignment tests on the **guest** address, dword scanning |
| `0x00205D90` | CRT `_allmul` | 64-bit `edx:eax`, `ret 0x10` |

**Method.** 423 seeded cases:
- **Reference:** the original bytes from DEFAULT.xbe, run in Unicorn 2.1.4 (x86-32) with the x87 unit reset (`fninit`) for every case. Unicorn honours precision control: 1/3 has 0, 11 and 40 trailing zero mantissa bits at the 64-, 53- and 24-bit settings.
- **Harness:** the lifted C, with guest memory in a 64 MiB arena wherever the host maps it, the XBE sections loaded at their VAs, and every access going through `XBOX_PTR`.
- **Comparison:** all general registers, ESP, x87 TOP, `st(0)` (as double) and output memory.

| Target | Arena (guest VA 0) | Gating cases | x87 at 64-bit PC (info) | NaN (info) |
|---|---|---|---|---|
| x86-64 Linux (gcc 12.2) | `0x7f…` (above 4 GiB) | 360/360 exact | 45/60 exact, 60/60 equal as float | 2/3 |
| AArch64 Linux (gcc 12.2, qemu-aarch64 7.2) | `0x5500802000` | 360/360 exact | same | same |
| Horizon in Eden 1.0.0 (devkitA64, on-device check) | `0x21xxxxxxxx` | 360/360 exact (`exact=407/423`) | same | same |

The gating cases are all `num` cases, plus the `x87` cases at 53-bit and 24-bit precision control.

**Negative controls**, all detected:
- Changing one `fsub` to an add in `sub_000E3640` fails on Linux (0/60 and 11/60 exact at the gating precisions) and in Eden (`FAIL … gating_mismatches=109/360`).
- Truncating `g_xbox_mem_offset` to 32 bits makes the harness segfault (exit 139).
- An NRO that never prints the marker returns runner exit 2, not a pass.

**Findings**
1. **x87 at 64-bit precision control** is modelled with doubles. 15 of 60 results differ in the last double ulp; all 60 agree after rounding to float. Which control word Manhunt uses at runtime is **unknown**. Determine it in milestone 2 before trusting physics or AI numerics.
2. **NaN with two NaN operands:** x87 returns the NaN with the larger significand; the lifted code returns the other one. Single-NaN propagation and signalling-NaN quieting match.
3. **Required flag:** compile generated code with `-ffp-contract=off`. AArch64 GCC otherwise fuses `a*b+c` and rounds differently from x87.
4. **Generated code needs `#define RECOMP_GENERATED_CODE`** before `recomp_types.h`, or the register aliases are missing.

## Gate (d): deko3d

`tests/deko3d-proof`:
- **Render:** a solid red triangle over a blue clear into an offscreen 1280×720 RGBA8 target.
- **Copy:** `DkBarrier_Full` + L2 invalidate, then `dkCmdBufCopyImageToBuffer` into an uncached buffer.
- **Check:** exact pixels and coverage.
- **Result in Eden:** `PASS center=ff0000ff corner=ffff0000 red=460800 clear=460800 other=0 coverage=50.000%`.
- **Control:** a green shader fails with `red=0`.

Eden rendered through **llvmpipe (software Vulkan)**. With `/dev/dri` passed in, Eden fails with "Device lacks a present queue" under Xvfb (GPU-PROOF-001). Real-hardware rendering is UNVERIFIED.

## Gate (e): emulator automation

`scripts/emu-run.sh` runs an NRO in Eden headlessly and waits for `<marker> PASS|FAIL` on `svcOutputDebugString`. Exit codes: 0 PASS, 1 FAIL, 2 no result (runner failure), 3 setup error.

The Eden image `dd-eden:local` (`sha256:68d0f084…`) is a host-local build from another project, with patches, and is not reproducible from this repo. The script checks its image ID. Building our own Eden image is follow-up EMU-IMAGE-001.

## Limits

- Five leaf functions with no calls, no indirect calls, no kernel imports, no SEH and no threads.
- No game-wide lift or link has been attempted.
- Physical Switch: UNVERIFIED.
