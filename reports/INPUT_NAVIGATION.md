# INPUT-NAV-001 — Switch-build menu navigation

2026-10-28: scripted input visibly moves the original main-menu selection in Eden. Independent reviewer runtime_analysis inspected the actual before/after images and logs and accepted this scope. This does not prove level loading, gameplay or hardware input.

Used the freshly built Switch NRO `bf122c4f763163727efb15ad223d884743ce3a904e49b398891ae1cf9082fca8`, the pinned default Eden/Xvfb llvmpipe runner and a 240-second run. No input or runtime implementation changed. Exact command/config and binary/input/image/log/frame hashes are in the local gitignored `game/build/render-profile/manifest.json`.

Configuration: `MANHUNT_DUMP_SECS=10`, `RECOMP_PB_REPORT_MS=10000`, `MANHUNT_PAD_SCRIPT=120000:down:20000,180000:up:20000`. Script times begin at the first pad sample, not process startup. The log records DOWN sampled at 122038 ms and UP at 188579 ms from that origin. Input logs alone were not counted as selection proof.

The primary agent inspected actual local frames: `game/build/evidence/navigation-20261028/frames/fb00009.png` highlights PLAY, while `game/build/evidence/navigation-20261028/frames/fb00022.png` highlights BONUS FEATURES after UP. UP wrapping from the first to last entry proves at least one selection movement. The sampled DOWN did not establish a separately verified visible transition; no claim is made for it. The capture has 23 BMPs, successful menu texture/music opens, continued presents, no logged guest crash/exit or Eden fault, and runner exit 0 after the full 240 seconds.

Frames/logs/binaries remain private and gitignored. The report contains authored observations only. Periodic dumps read the CPU surface concurrently with rasterization; some captures are black or partial, so frame count or changing hashes alone is insufficient visual evidence. These dumps prove guest surface content, not physical display correctness. Native real-controller behavior, low-FPS input timing, level selection/load and physical Switch remain UNVERIFIED.

Guest API addresses: XInputGetState `0x002A01D2`, XInputOpen `0x002A0170`; no implementation change or new guest override was made. Reproduce with the runner directly: `switch-boot-proof.sh` overwrites `manhunt.env`. Rollback: restore the prior env file; code is unchanged.

Retention: the complete frame directory and original run logs were moved/copied to `game/build/evidence/navigation-20261028/` before a later capture. That directory has a separate manifest; the old binary itself is no longer retained, so this is historical frame/log evidence, not a current binary hash assertion.
