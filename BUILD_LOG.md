# Build log

Provenance for every build and proof. Newest first.

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
