# Build log

Provenance for every build and proof. Newest first.

## 2026-10-27 — Milestone 2 after review fixes: PASS

- `scripts/switch-boot-proof.sh` (stricter checks: successful menu opens, Eden-side fault scan, increasing presents, provenance): `MH-BOOT-PROOF PASS nro=95f77035d0f869c7`, 32 presents, 11 frames (9 with ≥5 % non-black pixels, all distinct).
- `manhunt.nro` `95f77035d0f869c71a2d6d1fd9dd7b940fc4553ea5570cee06903284cce5df2f` (patch 0003 with locked/bounded directory fds).
- Milestone-1 suite re-run on the branch: all gates PASS. The recomp proof now has 6 functions (`_aulldiv` added): Eden `RECOMP-PROOF PASS exact=452/468 gating_mismatches=0/405`; `recomp-proof.nro` `10899ca355fe3258073db63f8efad98ee200d56231ca0298f8bb427cde67b747`.

## 2026-10-27 — Milestone 2: Switch boot to main menu (Eden): PASS

- Command: `scripts/switch-boot-proof.sh` (240 s)
- XBE `af69e803…afde`; xboxrecomp `f0f8a46` + patches 0001–0003; recompiler image `sha256:fc226cc9…63`; devkitA64 `sha256:1fc388c3…0282`; Eden `sha256:68d0f084…fecb` (llvmpipe)
- Lift: 14,820 functions (14,812 generated + 8 hand-written: XInput ×4, `D3D_BlockOnTime`, DirectSound ×3), 60 files, about 1.58 M lines; 2 function-boundary fixes
- `manhunt.nro` `0cf5e8ff7f810c20a84123c3e61648ddd11d87693ca80499ede13664fb937a3f` (21.6 MB). Contains lifted game code: local only, never distributed.
- Result: `MH-BOOT-PROOF PASS`: entry reached, intro movie, `MAIN_EX.TXD` + `FE_M.RIB` loaded, 32 presents, 11 frame dumps (10 non-black, all distinct), no crash/exit. Last frame visually confirmed as the main menu.
- Hardware: UNVERIFIED.

## 2026-10-26 — Milestone 1 suite after review fixes: PASS (hardware excluded)

- Changes: `scr=` scratch hash, return-sentinel assertion, full per-case state reset, vacuous-pass guards, deko3d per-row geometry check, `make -B`, and missing-input refusal in the scripts.
- `nro-proof.nro` `a340ad44…329d` (unchanged)
- `recomp-proof.nro` `350e5c044e69d23b7db7b1536d4e4399f08aecb1fe8d9bb4d35dc7028e03890c`: Eden `PASS exact=407/423 gating_mismatches=0/360`
- `deko3d-proof.nro` `8d8483073531bdca807576b5595f07b183c14bd402c234b0ad9c83d4991a7114`, built twice, identical: Eden `PASS … bad_rows=0`

## 2026-10-26 — Milestone 1 proof suite: PASS (hardware excluded; superseded above)

- Command: `scripts/toolchain-proof.sh` (40 s with the pipeline cache warm; a cold run adds ~4 min of disassembly plus ~76 s per lifted function).
- XBE `af69e803…afde`; xboxrecomp `f0f8a461…`; recompiler image `sha256:a6de3090…7d63c`; devkitA64 `sha256:1fc388c3…0282`; Eden image `sha256:68d0f084…fecb`.
- `nro-proof.nro` `a340ad44…329d`, built twice, identical.
- Recompiled functions: 360/360 gating cases exact on x86-64 Linux, AArch64 Linux (qemu) and Horizon in Eden (`RECOMP-PROOF PASS exact=407/423 gating_mismatches=0/360`).
- `recomp-proof.nro` `5414274ac35b89cf809102107de24861b3359023abecebbb0a0a2984359d6ff1`. It contains lifted game code: local only, never distributed.
- `deko3d-proof.nro` `045162be940562867546e572a27b8a447c97c60ab88defe50491014fa8d14ed3`, built twice, identical. Eden/llvmpipe: `DEKO3D-PROOF PASS … coverage=50.000%`.
- Emulator renderer: llvmpipe (software). Hardware: UNVERIFIED.

## 2026-10-26 — Analysis image on own Ghidra 12.1.4: PASS

- Command: `scripts/build-analysis-image.sh`
- Inputs: `eclipse-temurin:21-jdk-noble@sha256:10721595…e964`; `ghidra_12.1.4_PUBLIC_20260921.zip` (`ddac49f9…d4db`, OK); ghidra-xbe `build-202610161023` (`339b2f67…5152`, OK); version assertion 12.1.4/12.1.4 OK.
- Output: `manhunt-analysis:ghidra-xbe` `sha256:fc761493cdf9751d24cf97a8d210b074eda4bfe8eef61838adf07ae1501b4178`
- Import proof (`--network none`, 3 CPU / 6 GiB): loader `Xbox Executable Format (XBE)`, language `x86:LE:32:default:windows`, "Import succeeded".
- Cross-check: `analysis/ghidra_scripts/DumpLayout.java` reports entry `001fe908`, image base `00010000` and 15 section blocks plus headers. The addresses, sizes and W/X flags match `reports/XBE_HEADER.md`.
- Auto-analysis: not run (`-noanalysis`).

## 2026-10-26 — Analysis image rebuild: FAIL (version guard)

- Command: `scripts/build-analysis-image.sh`
- Base ID check: PASS (`sha256:fc4729a6…f6ad`)
- Ghidra version assertion: **FAIL**. The base has `application.version=12.1.2`; the loader zip and its `extension.properties` declare `12.1.4`.
- The previously built image `sha256:e513e945…8d68` therefore runs a 12.1.4 extension on Ghidra 12.1.2. Its XBE import worked, but the pairing is unsupported.
- No ghidra-xbe release targets 12.1.2 (releases: 12.1.4 ×3, 12.0.3).

## 2026-10-26 — NRO proof (revised source)

- Changes: newline escape fixed, `appletMainLoop` loop with + exit and 600-frame cap, NACP metadata, `-MMD -MP`, `-ffile-prefix-map`.
- Command: `scripts/build-nro-proof.sh`, run twice
- Image: `devkitpro/devkita64@sha256:1fc388c3a0d34bd2045a6dadcb1020e069d5f876a187fd705de14b4440c00282`
- Output SHA-256 (both runs): `a340ad446d7a9376dd1bc68fac3db98ec7e47c92bcc44ae0fd66cd401def329d`
- Result: build PASS, reproducible, no `-Wall` warnings. Runtime UNVERIFIED (no emulator runner).
- Note: the earlier root-run build left a root-owned `build/`. It was removed with the same image running as root; builds now run as uid 1000.

## 2026-10-26 — NRO proof (original source)

- Image: `devkitpro/devkita64:20260219` (same digest as above)
- Output SHA-256: `6b477a31a5a038982cc954be2b54eb5f696a6a7aa68f3b842f0229353c01e917`
- Result: build PASS; runtime UNVERIFIED. The source printed a literal `\n` and exited immediately.

## 2026-10-26 — Analysis image

- Base: `dd-ghidra-mcp:local` `sha256:fc4729a6…f6ad`; loader `build-202610161023`
- Output: `manhunt-analysis:ghidra-xbe` `sha256:e513e945173e7a22469e0432add84699064d3c33f37aa03d104caca8ab988d68`
- XBE: `af69e803…afde`; asset manifest: `reports/ASSET_MANIFEST.sha256`
