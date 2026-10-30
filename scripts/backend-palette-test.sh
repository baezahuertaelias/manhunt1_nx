#!/usr/bin/env bash
# Synthetic regression against the pinned runtime; no game inputs.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
image=sha256:fc226cc9dd0cead9bd0a5e4d3f59a705812a9c699c0a4c105d05e8da893eec63
docker image inspect "$image" >/dev/null
printf 'image=%s commit=%s\n' "$image" "$(git -C "$root" rev-parse HEAD)"
sha256sum "$root/game/patches/xboxrecomp/"*.patch "$0"
docker run --rm --network none --cpus 1 --memory 1g \
  --user "$(id -u):$(id -g)" --cap-drop ALL --security-opt no-new-privileges \
  --mount "type=bind,src=$root/game/patches/xboxrecomp,dst=/patches,readonly" \
  --entrypoint bash "$image" -ec '
cp -r /opt/xboxrecomp /tmp/runtime
for patch in /patches/*.patch; do git -C /tmp/runtime apply "$patch"; done
cmake -S /tmp/runtime/tests/nv2a_ffp_backend -B /tmp/proof -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/proof -j1
ctest --test-dir /tmp/proof --output-on-failure
# Restore the original palette-dropping defect; the pixel regression must detect it.
python3 - <<"PY"
from pathlib import Path
p=Path("/tmp/runtime/src/kernel/nv2a_pb_exec.c")
s=p.read_text()
old="s_gpu.texs[0].palette = tex->palette_va;"
assert s.count(old)==1
p.write_text(s.replace(old,"s_gpu.texs[0].palette = 0;"))
PY
cmake --build /tmp/proof -j1
if ctest --test-dir /tmp/proof --output-on-failure >/tmp/negative.log 2>&1; then
  cat /tmp/negative.log
  echo "BACKEND-PALETTE FAIL negative control passed unexpectedly"
  exit 1
fi
grep -F "decoded[i] ==" /tmp/negative.log || { cat /tmp/negative.log; exit 1; }
echo "BACKEND-PALETTE PASS fixed regression + original-defect negative control"
'
