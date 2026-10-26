#!/usr/bin/env bash
# Hash every file under a local game-input directory. Output lists only
# SHA-256, size, and relative path; no proprietary content is copied.
# Usage: scripts/asset-manifest.sh [input-dir] > reports/ASSET_MANIFEST.sha256
set -euo pipefail
dir="${1:-${GAME_INPUT_DIR:-assets/manhunt1_xbe}}"
[ -d "$dir" ] || { echo "input dir not found: $dir" >&2; exit 1; }
cd "$dir"
echo "# sha256  size  path   (root: $(basename "$PWD"))"
find . -type f -print0 | LC_ALL=C sort -z | while IFS= read -r -d '' f; do
  printf '%s  %s  %s\n' "$(sha256sum -- "$f" | cut -d' ' -f1)" "$(stat -c %s -- "$f")" "${f#./}"
done
