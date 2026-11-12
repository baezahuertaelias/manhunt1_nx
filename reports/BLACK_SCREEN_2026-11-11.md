# Black-screen investigation — 2026-11-11

Status: root cause and fix **not yet established**. No NaN clamping, draw dropping, or fallback camera has been added.

## Observed evidence

- Combined console run: owner reported black output after the first court punch. `game/build/hw-test/2026-11-10-gpu-offload/run-combined.log` later records all-NaN modelview register rows. The last finite dump is near present 7584, and the first all-NaN dump is near present 7646. This is executor state, before GL shader evaluation.
- Rollback run: all four experimental renderer switches were disabled in `game/build/hw-test/gpu-offload/20261110-234419-rollback/run.log`. The first incoming nonfinite fixed transform is at line 19806, after analog button mask `0x04` at poll 4832. World/modelview NaNs appear later. Thus the experimental GPU paths are not necessary for this symptom.
- Allocation failures already occur at rollback line 3728, well before the nonfinite transform. The later burst at line 19830 must not be mistaken for the first allocation failure. Allocation failure is a candidate precursor, not a proven cause of the invalid transform.

## Matrix provenance localized in generated code

`sub_002946B0` in `game/build/gen/recomp_0058.c` emits the NV2A modelview packet `0x400480` and composite packet `0x400680`. It multiplies device world matrix at `device+0xDE0` by view matrix at `device+0xC60`, then combines projection state at `device+0x980`, using the SSE matrix helper `sub_00292D00`.

`sub_0028DA30` in `recomp_0057.c` copies the title's SetTransform source into `device+0xC60 + type*64`. This provides an earlier point for determining whether the game already supplied an invalid source matrix.

The two functions are observed source boundaries; camera, skinning, physics, allocation failure, and instruction-emulation causes remain to be distinguished at runtime.

## Checks completed

- Accepted baseline NRO versus combined NRO: 14,655 generated-span functions, 18,299,696 bytes compared. All match after address/branch-immediate normalization; 1,722 match byte for byte. Eight selected camera/vector functions differ only in their initial relocated memory-base load; their remaining instruction bytes match exactly. Evidence and limitations: `game/build/hw-test/2026-11-10-gpu-offload/binary-comparison/guest-comparison.json`.
- Architecture-specific reciprocal-square-root check: the x86 host uses RSQRTSS, while ARM uses exact reciprocal square root. Executed the actual generated vector-normalization and matrix-orthonormalization functions on 200,000 finite inputs, including zero, collinear, and identity cases. Numerical outputs often differ, but neither path produced nonfinite output. This bounded test did **not** establish an ARM math failure. Evidence: `game/build/black-investigation/camera-rsqrt-result.txt`.

## Runtime work prepared

An investigation-only host binary, `game/build/black-investigation/manhunt-matrix-trace`, recompiles isolated copies of generated chunks 57 and 58 and records the first invalid SetTransform source or matrix-multiply input/output. It logs matrix addresses and values, device-relative offsets, guest registers/stack, and a host backtrace. `BLACK_RAM_DUMP` optionally writes the first RAM snapshot; `BLACK_STOP_ON_NAN=1` stops that diagnostic process at the first finding. Shared generated code, runtime sources, and product behavior are not modified by this binary.

The next required evidence is the first bad source address and its writer during an executable gameplay reproduction. Host reproduction may differ from ARM behavior and cannot replace final Switch acceptance.
