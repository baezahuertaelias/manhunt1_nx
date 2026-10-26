#!/usr/bin/env bash
# Build manhunt-analysis:ghidra-xbe from official Ghidra + ghidra-xbe releases.
# All inputs are pinned (image digest, release SHA-256) in infra/analysis/Dockerfile.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
docker build -t manhunt-analysis:ghidra-xbe \
  -f "$root/infra/analysis/Dockerfile" "$root/infra/analysis"
docker image inspect --format '{{.Id}}' manhunt-analysis:ghidra-xbe
