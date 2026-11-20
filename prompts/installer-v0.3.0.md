# Prompt: move the installer to port v0.3.0

Paste everything below the line into a Claude Code session started in a checkout of https://github.com/marmota94/manhunt1_nx_installer.

---

Update this installer to ship port release **v0.3.0** of `manhunt1_nx` and replace v0.2.0. Read `README.md` (the "For maintainers" section), `tools/update-release.py`, `app.js`, `worker.js`, `test/` and `.github/workflows/pages.yml` first.

## Facts

- The GitHub account `baezahuertaelias` is now `marmota94`. Repository URLs redirect, but GitHub Pages does not: `https://baezahuertaelias.github.io/manhunt1_nx_installer/` returns 404, and the live page is `https://marmota94.github.io/manhunt1_nx_installer/`.
- The v0.3.0 release is a draft on https://github.com/marmota94/manhunt1_nx/releases (tag `v0.3.0`). It will be published after a console test. Its assets:

  | File | SHA-256 | Bytes |
  |---|---|---|
  | `manhunt1-nx-v0.3.0.zip` | `89a7dd47a7589f6014d6e1363d5746813047f30f0bb9c334c0830c7ef7e76048` | 10426588 |
  | `manhunt.nro` inside it | `5c103ee3086e8e839dd8dedb50d1b322e3fda0e326dd687e1ef7d14ad20cd6ab` | 26436797 |
  | `manhunt1-nx-v0.3.0-lgpl-source.zip` | `d22dd2d8e6bcb9cf55c7d35cd9616f7e2b591cd02708b4a7d3317a79c0b6a204` | 93266 |

- The v0.3.0 zip has the same layout as v0.2.0, plus one file: `switch/manhunt1_nx/LICENSES/SOURCE-OFFER.txt`. `tools/update-release.py` already accepts any `LICENSES/*` file. The asset manifest (836 files) and the `DEFAULT.xbe` hash (`af69e803…afde`) are unchanged.
- **The tracked `release/manhunt1-nx-v0.2.0.zip` must go.** Its `manhunt.nro` embeds an icon made from the game's cover art (21,819-byte JPEG in the NRO's ASET section). The project must not publish that. v0.3.0 has no custom icon.
- The NRO statically links an LGPL-2.1 library. Its source is the `-lgpl-source.zip` on the port's release page, and `LICENSES/SOURCE-OFFER.txt` in the port zip points there. The installer passes the port zip's entries through unchanged, so that notice reaches the SD card.

## Tasks

1. Get the release zip. If the draft is still unpublished, ask me for `manhunt1-nx-v0.3.0.zip` (I have it in the port checkout under `dist/`); otherwise download it from the release. Check its SHA-256 against the table and stop if it differs.
2. Run `python tools/update-release.py <path>/manhunt1-nx-v0.3.0.zip` and confirm it stages `release/manhunt1-nx-v0.3.0.zip` and `release/manifest.json` with version `0.3.0` and the NRO hash above.
3. Remove `release/manhunt1-nx-v0.2.0.zip` from the index (`git rm`). Force-add only `release/manhunt1-nx-v0.3.0.zip`, and add `release/manifest.json`.
4. Update every `baezahuertaelias` link in the repository to `marmota94` (README, page text, NOTICES, workflow, tests). Update text that names v0.2.0 to v0.3.0. Keep the README's statement of the file count (836) only if it is still true.
5. In the page and README, add one line under the player steps: the NRO includes an LGPL-2.1 library, and its source is listed in `switch/manhunt1_nx/LICENSES/SOURCE-OFFER.txt`.
6. Run the tests in the README (`node --test test/unit.mjs`, then `python test/browser.py`). They must pass. Report each command's result.
7. Commit as one change with a message that says why v0.2.0 was removed (cover-art icon) and that the links follow the account rename. Show me the diff summary. Do not push until I say so.

## Not to do on your own

- Removing v0.2.0 from the latest commit does not remove it from history, and the old Pages deployment may still be cached. Rewriting history or deleting old Pages deployments is irreversible and affects everyone who cloned the repository. Explain the options (for example `git filter-repo --path release/manhunt1-nx-v0.2.0.zip --invert-paths` and a force push), and wait for my decision.
- Do not add game files, disc images, generated SD packages or real-data test output.
- Do not change the packaging logic beyond what the new release needs.
