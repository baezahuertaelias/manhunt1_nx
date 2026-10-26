#!/usr/bin/env bash
# Build manhunt-analysis:ghidra-xbe after checking the local base image ID.
set -euo pipefail
BASE_IMAGE="${BASE_IMAGE:-dd-ghidra-mcp:local}"
BASE_IMAGE_ID="${BASE_IMAGE_ID:-sha256:fc4729a6bb0cb39164c6d8d7aaeee6414f1a780478db04e32ce93cc46b7af6ad}"
root="$(cd "$(dirname "$0")/.." && pwd)"
actual="$(docker image inspect --format '{{.Id}}' "$BASE_IMAGE")"
if [ "$actual" != "$BASE_IMAGE_ID" ]; then
  echo "base $BASE_IMAGE is $actual, expected $BASE_IMAGE_ID" >&2
  echo "update BASE_IMAGE_ID and docs/CHANGELOG_CONFIG.md if this change is intended" >&2
  exit 1
fi
docker build --build-arg "BASE_IMAGE=$BASE_IMAGE" \
  -t manhunt-analysis:ghidra-xbe -f "$root/infra/analysis/Dockerfile" "$root/infra/analysis"
docker image inspect --format '{{.Id}}' manhunt-analysis:ghidra-xbe
