#!/usr/bin/env bash
# Synthetic helper test; no game inputs, Docker or host package changes.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
cd "$work"
git apply --include=src/kernel/nv2a_profile.h "$root/game/patches/xboxrecomp/0004-render-profile.patch"
mkdir -p platform
printf '#include <stdint.h>\nuint64_t xbox_HostNowNs(void);\n' > platform/host_time.h
"${CC:-cc}" -std=c11 -pthread -Wall -Wextra -Werror -I . -I src/kernel \
  "$root/tests/render-profile/main.c" -o profile-test
./profile-test
