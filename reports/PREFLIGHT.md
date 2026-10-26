# Preflight

Date: 2026-10-26  
Workspace: `/Downloads/repos/manhunt1_nx`

| Check | Result | Evidence |
|---|---|---|
| Repository | PARTIAL | Fresh local Git repository now works on `main`, with no commits or remote; only project documentation exists. |
| CPU/RAM/disk | PASS | 4 CPUs; 15 GiB RAM total, 9.0 GiB available; 518 GiB free. |
| Docker CLI/Compose | PASS | Docker server/client 29.7.2; Compose v5.4.0; daemon reports 34 containers and 43 images. |
| Intel GPU device access | PASS | `/dev/dri/card0` and `/dev/dri/renderD128` exist with group access for `video`/`render`; accelerated rendering still requires a container proof. |
| Codex CLI | PARTIAL | `codex-cli 0.164.0` (rechecked); model/subagent support and `.codex/agents` config syntax not verified. |
| GitHub CLI/auth | PASS | `gh auth status`: logged in to github.com, scopes `gist, read:org, repo, workflow` (token not printed). No remote configured yet. |
| Storage budget | PASS | 517 GiB free of 922 GiB; inputs 2.4 GiB (836 files); Ghidra project 49 MiB. |
| Compose config | PASS | `docker compose -f infra/compose.yaml config -q` exits 0. |
| In-container GPU rendering | UNVERIFIED | Device nodes only; no container render proof (GPU-PROOF-001). |
| Network requirements | UNVERIFIED | Loader download succeeded at image build; no further egress inventory. |
| Switch toolchain | UNVERIFIED | `devkitPro`, `devkitA64`, AArch64 compiler, and ARM compiler not found on `PATH`. |
| Ghidra/recompiler | PARTIAL | Host tools are absent, but Docker image `manhunt-analysis:ghidra-xbe` now includes the pinned XBE loader; recompiler remains unavailable. |
| xemu/Switch emulator | UNVERIFIED | `xemu`, `eden`, and `ryujinx` not found on `PATH`. |
| Lawful game input | PASS (hashes) | User supplied `assets/manhunt1_xbe/` (XBE + levels + audio); legality/provenance is user responsibility. Manifest: `reports/ASSET_MANIFEST.sha256` (SHA-256 `1e49ffc9e04a3f51c1c250d691f652eead297e2af5a1ca238a3f1750b5b71700`). Header: `reports/XBE_HEADER.md`. |
| Host mutation policy | PASS | No host package, driver, Docker resource, or credential changes made. |

## Gate decision

Preflight is complete (PREFLIGHT-001 done). Docker, GPU device nodes, `gh` auth, the XBE input, the Dockerized devkitA64 build and the Ghidra XBE import pass. The Switch toolchain lives in Docker, not on the host `PATH`. Implementation is still blocked on the recompiler (no candidate evaluated) and on a working emulator runner. Beta 1 is **UNVERIFIED**.

## Baseline command evidence

Executed 2026-10-26 from the project root. `git status -sb` reports an empty `main` history with uncommitted documentation. `docker info` reports server/client 29.7.2, 34 containers, and 43 images. Hardware baseline: 4 CPUs, 15 GiB RAM, 9.1 GiB available, 518 GiB free disk.

## XBE input evidence

- Path: `assets/manhunt1_xbe/DEFAULT.xbe`
- Size: 3,141,632 bytes
- SHA-256: `af69e803695e29c3f99b38d6a759fac4c9300c9a540a2c244e4e7a07fb96afde`
- Host identification: Microsoft Xbox executable, title `Manhunt`, signed, region `Rest_of_World`
- Git policy: excluded by `*.xbe`; not staged or committed.

## Proof results

- Dockerized devkitA64/libnx proof: **PASS**. Image `devkitpro/devkita64:20260219` produced `tests/nro-proof/nro-proof.nro`; SHA-256 `6b477a31a5a038982cc954be2b54eb5f696a6a7aa68f3b842f0229353c01e917`.
- Dockerized Ghidra proof: **PASS — loader/import**. Derived image digest `sha256:e513e945173e7a22469e0432add84699064d3c33f37aa03d104caca8ab988d68` installs XboxDev/ghidra-xbe. Official `analyzeHeadless` selected `Xbox Executable Format (XBE)` and `x86:LE:32:default:windows` for `DEFAULT.xbe`. Full auto-analysis was stopped after import because decompiler warnings became prolonged; this is not a complete analysis pass. *Superseded 2026-10-26:* that image paired Ghidra 12.1.2 with a 12.1.4 loader. It was replaced by an image built from official Ghidra 12.1.4, `sha256:fc761493…4178` (see BUILD_LOG.md).
- Eden CLI capability: **UNVERIFIED**. `dd-eden:local` exposes `--help`, but no NRO/game load was attempted because no verified automation/configuration path exists yet.
- Eden launch proof: **FAIL/UNVERIFIED**. Running the generated NRO failed before game loading: the binary rejected `-n`, could not create its config path, and could not determine an SDL3 Vulkan window backend. This is an emulator-runner failure, not a native-port failure.

