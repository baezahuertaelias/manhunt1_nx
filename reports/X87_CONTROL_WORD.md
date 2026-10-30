# x87 control-word investigation

Task: **X87-CW-001 — OPEN**. Measurement date: 2026-10-28.

A bounded observation of the existing Linux debug build found guest requests
for both 53-bit and 64-bit x87 precision. Consequently, the double-based
approximation remains a numerical-fidelity concern; this investigation does
not establish correct physics or AI, and does not close the blocker.

## Executed method

From the primary checkout, ran:

```sh
GAME_BUILD_DIR=host-O0 scripts/game-gdb.sh game/build/logs/x87-watch.gdb 60
```

The private GDB command file stopped at `main`, recorded the initial
`g_fp_control_word`, and installed a hardware watchpoint on that main-thread
TLS location. Each change recorded its value and three stack frames. This
observes the recompiled runtime's simulated guest control word, not the host
processor's native control register. The wrapper completed with exit 0, but
filters output and does not propagate the guest timeout status; that exit is
not a game-success assertion.

No production code changed and no binary was rebuilt. The existing debug
binary's correspondence with current source is **UNVERIFIED**. Git checkout:
`1273eb1f3cfeff8060216487707564436f864873`. Container image:
`sha256:fc226cc9dd0cead9bd0a5e4d3f59a705812a9c699c0a4c105d05e8da893eec63`.

## Observations

| Control word | Observation | Precision-control bits | Rounding-control bits |
|---|---|---|---|
| `0x037f` | Initial runtime value at `main` | 3: 64-bit significand | 0: nearest/even |
| `0x023f` | 1,024 recorded changes to this value | 2: 53-bit significand | 0: nearest/even |
| `0x027f` | 1,020 recorded changes to this value | 2: 53-bit significand | 0: nearest/even |
| `0x173f` | 2 recorded changes to this value | 3: 64-bit significand | 1: toward negative infinity |
| `0x1b3f` | 1 recorded change to this value | 3: 64-bit significand | 2: toward positive infinity |

The precision and rounding columns decode bits 8–9 and 10–11 respectively;
they describe guest requests, not proof that the lifter implements them
faithfully. All observed values mask the six x87 exceptions. Counts are
watchpoint changes, not instruction execution counts, and repeated writes of
the same value are not counted.

The 64-bit requests were observed in `sub_00208625`, whose decoded `fldcw`
site is guest `0x00208640`. The sampled callers included guest routines
`0x00203C4D` and `0x002044EF`. The first observed change to `0x023f` occurred
in `sub_0020BA0B`, corresponding to decoded `fldcw` at `0x0020BA35`.
These observations narrow the previous unknown-control-word blocker; they do
not justify accepting the double approximation for all game arithmetic.

## Static inventory and unresolved coverage

Scanning the cached section disassembly for decoded `fldcw` mnemonics found
11 sites: ten in its `text` section and `0x00221F96` in `D3DX`:

`0x00207992`, `0x002079F9`, `0x00207B8A`, `0x00207BB7`,
`0x00207C36`, `0x00207C60`, `0x00207CC5`, `0x00208640`,
`0x002087AD`, `0x0020BA35`, `0x00221F96`.

The generated C contains 13 corresponding assignments across two generated
files. Overlapping function translations duplicate some sites. The decoded
site `0x00221F96` has no corresponding generated `fldcw` assignment in that
search, and the current `game/build/functions.fixed.json` contains no function range covering it. Treat this as an explicitly unresolved code region in D3DX; whether it is unreachable, omitted, or otherwise represented remains
**UNVERIFIED**. Cached disassembly coverage is not proven complete. This is
not an exhaustive inventory of all executable regions, control-word changes,
or `_controlfp` callers; no claim about complete function discovery is made.

## Evidence provenance

Paths below refer to the primary checkout, `/Downloads/repos/manhunt1_nx`.
Private artifacts are gitignored and must remain local. This report contains
authored analysis and addresses, not copied game instructions or runtime logs.

| Local input/artifact | SHA-256 |
|---|---|
| `game/build/host-O0/manhunt` | `f8c6043d98c9236e1592659d8c87a1a20f6eae8eb3cdaa4a621e1c3ead81d89f` |
| `assets/manhunt1_xbe/DEFAULT.xbe` | `af69e803695e29c3f99b38d6a759fac4c9300c9a540a2c244e4e7a07fb96afde` |
| `game/build/logs/x87-watch.gdb` | `6537aa48477113a50e38ffe8a504fb6c89e9057685f2ecd0b6b0ad669c3a45a3` |
| `game/build/logs/x87-watch-private.log` | `78f37ed59ca5865a0b33543f82867f59759d2d25894a4d886cebcc1e4ca2b603` |
| `game/build/logs/x87-sites-private.json` | `7124eb76da9e2b5b5bb38860095f9996e1f92582a9226549700a1c336367217c` |

The local manifest `game/build/logs/x87-provenance-private.json` records the
command, image, checkout, counts, hashes and limitations. Reviewers must inspect
the actual private artifacts when available; this report alone does not replace
that inspection.

## Remaining acceptance work

1. Rebuild with recorded source provenance and repeat the observation.
2. Reconcile cached disassembly, generated assignments and `0x00221F96`,
   explicitly tracking unknown code regions and `_controlfp` call sites.
3. Instrument or watch each guest thread's control word; the executed watchpoint
   covers the main thread's TLS address only.
4. Exercise menu navigation and first-level gameplay with explicit checkpoints;
   neither menu arrival nor level load was asserted by this 60-second run.
5. Revisit the double-based x87 decision using focused reference cases for the
   observed precision/rounding combinations and affected guest callers.

Switch execution and physical hardware behavior are **UNVERIFIED**. No new
build, numerical differential test, or gameplay acceptance gate passed here.
