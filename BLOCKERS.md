# Blockers

Ranked. Evidence: [reports/FEASIBILITY.md](reports/FEASIBILITY.md), [reports/RECOMP_EVAL.md](reports/RECOMP_EVAL.md), [reports/PREFLIGHT.md](reports/PREFLIGHT.md).

1. **P0 — No GPU back end.** Frames come from xboxrecomp's CPU rasteriser (pushbuffer executor) at about 0.1–0.5 fps in Eden. Milestone 3+ needs a deko3d back end behind `nv2a_backend.h`.
2. **P1 — Audio not implemented.** DirectSound reports "no driver" (`game/src/dsound_hle.c`); a native DirectSound at API level is required (milestone 6).
3. **P1 — Game's x87 control word unknown.** 64-bit precision control is approximated with doubles (last-ulp differences).
4. **P1 — Eden runner not reproducible** from this repo (`dd-eden:local`, checked by image ID) (EMU-IMAGE-001).
5. **P1 — Hardware GPU in containers.** Eden + `/dev/dri` under Xvfb fails ("Device lacks a present queue"); every emulator run used llvmpipe (GPU-PROOF-001).
6. **P2 — `main` not protected on GitHub.** The private repo exists (`github.com/baezahuertaelias/manhunt1_nx`), but branch protection needs GitHub Pro for private repos (HTTP 403). Until then `main` is protected only by the AGENTS.md rules.
7. **P2 — RAM mirrors unsupported on Horizon.** Not used on the boot path (measured); re-check at level load.
8. **P2 — No physical Switch.** All hardware gates stay `UNVERIFIED`.
