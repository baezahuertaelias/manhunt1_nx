# Prompt: web installer for manhunt1_nx

Paste everything below the line into a new Claude Code session started in an empty directory.

---

Create a new repository, `manhunt1_nx_installer`: a static web page, published on GitHub Pages, that turns the player's own copy of Manhunt (original Xbox) into a ready-to-copy SD card package for the Nintendo Switch port `manhunt1_nx` (https://github.com/baezahuertaelias/manhunt1_nx). Everything runs in the browser. Nothing is uploaded, and the repository never contains game data.

Model the user flow and the structure on https://github.com/ChanseyIsTheBest/renut_nx_ultimate_installer. Read its `index.html`, `app.js`, `sw.js`, `lib/` and README first. Reuse its patterns: no build step, ES modules, a worker for the heavy work, and a service worker that streams the zip as a download with bounded memory. This port needs much less than that one. There are no shaders to compile, no XEX/LZX, no LZ4 packing and no Pyodide, so leave all of that out.

## What the port needs on the SD card

The output zip is extracted at the SD root and must produce exactly:

```
switch/manhunt1_nx/manhunt.nro
switch/manhunt1_nx/manhunt.env
switch/manhunt1_nx/manifest.json
switch/manhunt1_nx/asset-manifest.sha256
switch/manhunt1_nx/INSTALL.txt
switch/manhunt1_nx/LICENSES/...
switch/manhunt1_nx/game/DEFAULT.xbe
switch/manhunt1_nx/game/audio/...      (152 files)
switch/manhunt1_nx/game/levels/...     (683 files)
```

- Everything except `game/` comes from the port's release zip, `manhunt1-nx-v<version>.zip`, made by `scripts/make-release.py` in the port repository. Its own paths already start with `switch/manhunt1_nx/`.
- `manifest.json` in that zip holds `version`, `nro.sha256`, `game.install_dir` (`"game"`), `game.default_xbe_sha256` and `game.asset_manifest` (`"asset-manifest.sha256"`).
- `asset-manifest.sha256` has a first comment line, then one line per file: `<sha256>  <size>  <relative path>`. There are 836 files totalling about 2.36 GB. The paths are relative to the game root, keep the disc's case, and use `/`.
- `DEFAULT.xbe` must have SHA-256 `af69e803695e29c3f99b38d6a759fac4c9300c9a540a2c244e4e7a07fb96afde`.

## Input

Accept either:

1. **An original Xbox disc image (`.iso`, XDVDFS).** Find the volume descriptor `MICROSOFT*XBOX*MEDIA` at sector 32 (byte 0x10000) of the game partition. Try the partition at offset 0 (an xiso) and at 0x18300000 (a full redump XGD1 image). Read the directory tree, which is AVL-sorted with 2048-byte sectors. Read files in slices through `File.slice`; never load the whole image.
2. **An extracted folder** (`<input webkitdirectory>`). Drop the top folder name from `webkitRelativePath`.

Ignore any file that is not in the asset manifest, such as `.DS_Store` or `._*`. Match paths to the manifest case-insensitively, and write them with the manifest's case.

## Validation

All of these must pass before anything is written. Show clear messages for each failure.

- `DEFAULT.xbe` must exist and its SHA-256 must equal `game.default_xbe_sha256`. If it doesn't, show its hash and say this version is not supported.
- Every manifest file must be present with the listed size. Hash each one with SHA-256 as it streams into the zip; do not read files twice. If any hash differs, fail the whole package and name the file. List missing or wrong-size files before starting, so the player isn't left waiting.
- `crypto.subtle.digest` cannot stream, so use an incremental SHA-256: a small vendored JS implementation or a wasm build, with its licence in `NOTICES.md`.

## Output

- Stream a zip through the service worker, as the reference does: chunked, never holding more than a few MB.
- The game files are already compressed formats, so store them without compression (method 0).
- Use ZIP64 when sizes or offsets pass 4 GB. The total is 2.36 GB plus the release, but write ZIP64 anyway when needed.
- Write the CRC-32 for each entry as it streams, using data descriptors.
- Copy the release zip's entries into the output unchanged (the NRO, env, manifests, INSTALL.txt, LICENSES). Then add `switch/manhunt1_nx/game/<path>` for each manifest file.
- Name the download `manhunt1-nx-v<version>-sd.zip`.

## Getting the release

- Load the release zip same-origin from `release/` in the installer repository, with the version from `release/manifest.json`, which is a copy of the release's `manifest.json`.
- Check the NRO's SHA-256 against `nro.sha256` before using it.
- Add `tools/update-release.py <path to manhunt1-nx-vX.Y.Z.zip>`, which copies the zip into `release/` and writes `release/manifest.json`.
- Optionally, a GitHub Actions workflow that does the same from the port repository's latest GitHub release.

## Page and steps for the player

1. Choose the disc image or the extracted folder. The page shows "Manhunt (Xbox), supported" or the exact problem.
2. Press "Create the SD package". Show progress as bytes and files, and the current file.
3. Copy the result to the Switch:
   - Delete any old `switch/manhunt1_nx/game` folder.
   - Extract the zip at the SD card root.
   - Start `manhunt.nro` from the homebrew menu or Sphaira in title-takeover mode: hold R while opening a game, so it gets enough memory.
   - Quit with ZL + ZR + Plus + Minus held for 1.5 s.
   - Settings are in `manhunt.env`: `MANHUNT_WIDESCREEN=1` gives 16:9, `MANHUNT_LANGUAGE` sets the language.

Footer: "Manhunt is a trademark of Rockstar Games / Take-Two Interactive. Not affiliated with or endorsed by Rockstar Games, Take-Two, Microsoft or Nintendo. You need your own copy of the game; nothing is uploaded."

## Repository

- Files: `index.html`, `app.js`, `worker.js`, `sw.js`, `lib/xdvdfs.js`, `lib/zip.js`, `lib/sha256.js`, `lib/manifest.js`, `release/`, `tools/update-release.py`, `serve.py`, `test/`, `README.md`, `LICENSE`, `NOTICES.md`, `.gitignore`.
- Licence: match the port's licence files (LGPL-2.1 and MIT for xboxrecomp). Credit the reference installer's patterns in `NOTICES.md` according to its licence (GPL-3.0). If code is copied from it rather than only the approach, the installer must be GPL-3.0 too. Decide this explicitly and say which in the README.
- `.gitignore` covers zips, `*.iso`, `game/`, test outputs and anything that is game data.
- `serve.py <port> <dir>`: a local static server with the correct MIME types for `.js` and `.wasm`.

## Tests

Use no game data.

- `test/` builds a tiny synthetic XDVDFS image and a synthetic folder with a fake manifest, runs the pipeline in a headless browser (Playwright or Chromium), and checks:
  - the zip's tree and CRCs, with `unzip -t`;
  - that the SHA-256 checks reject a changed file and a missing file;
  - ZIP64 output on a sparse 5 GB synthetic entry.
- Unit tests for the XDVDFS parser (both partition offsets) and for SHA-256 against known vectors.

## Done when

- `python serve.py 8193 .` serves the page.
- In Chromium, choosing a synthetic image produces a zip that extracts to the layout above.
- All tests pass.
- The README explains, for players and for maintainers, how to update `release/` to a new port version.

Do not commit any real game file, zip output or test output made from real data.
