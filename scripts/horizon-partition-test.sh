#!/usr/bin/env bash
# Test the exact authored partition-write helper, without game inputs.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
mode="${1:-host}"
case "$mode" in host|switch) ;; *) echo 'usage: horizon-partition-test.sh [host|switch]' >&2; exit 2;; esac
work="$root/tests/horizon-partition-proof/build"
mkdir -p "$work"
sha256sum "$root/game/patches/xboxrecomp/0012-private-partition-write.patch" "$root/tests/horizon-partition-proof/main.c" "$0"
image=sha256:fc226cc9dd0cead9bd0a5e4d3f59a705812a9c699c0a4c105d05e8da893eec63
docker run --rm --network none --cpus 1 --memory 1g --user "$(id -u):$(id -g)" \
 --cap-drop ALL --security-opt no-new-privileges \
 --mount "type=bind,src=$root/game/patches/xboxrecomp,dst=/patches,readonly" \
 --mount "type=bind,src=$root/tests/horizon-partition-proof,dst=/test" "$image" bash -ec '
cp -r /opt/xboxrecomp /tmp/runtime
for patch in /patches/*.patch; do git -C /tmp/runtime apply "$patch"; done
cp /tmp/runtime/src/kernel/kernel_path.c /test/build/kernel_path.c
python3 - <<"PY"
from pathlib import Path
s=Path("/test/build/kernel_path.c").read_text()
start="static ssize_t write_partition_sector(int fd, const void *sector)\n{\n"
assert s.count(start)==1
body=s[s.index(start):];body=body[:body.index("\n}")+2]+"\n"
assert body.count("lseek(")==1 and body.count("write(fd, sector, 512)")==1
Path("/test/build/partition-helper.h").write_text(body)
# Negative control: omit the seek. Existing-file offset/preservation must detect it.
seek_block = "if (lseek(fd, XBOX_PART_TABLE_OFFSET, SEEK_SET) < 0) {\n        if (!errno) errno = EIO;\n        return -1;\n    }"
assert body.count(seek_block)==1
negative=body.replace(seek_block, "")
Path("/test/build/negative").mkdir(exist_ok=True)
Path("/test/build/negative/partition-helper.h").write_text(negative)
PY
cc -std=c11 -O2 -Wall -Wextra -D__SWITCH__ -I/test/build /test/main.c -o /test/build/host
/test/build/host
cc -std=c11 -O2 -Wall -Wextra -D__SWITCH__ -I/test/build/negative /test/main.c -o /test/build/negative/host
if /test/build/negative/host > /test/build/negative.log 2>&1; then echo "HZ-PARTITION-NEGATIVE FAIL"; exit 1; fi
grep -F "HZ-PARTITION-PROOF FAIL" /test/build/negative.log
echo "HZ-PARTITION-NEGATIVE PASS missing seek detected"
'
sha256sum "$work/partition-helper.h" "$work/kernel_path.c"
if [ "$mode" = switch ]; then
 image=devkitpro/devkita64@sha256:1fc388c3a0d34bd2045a6dadcb1020e069d5f876a187fd705de14b4440c00282
 docker run --rm --network none --cpus 1 --memory 1g --user "$(id -u):$(id -g)" \
  --cap-drop ALL --security-opt no-new-privileges \
  --mount "type=bind,src=$root/tests/horizon-partition-proof,dst=/test" -w /test "$image" bash -ec '
/opt/devkitpro/devkitA64/bin/aarch64-none-elf-gcc -std=c11 -O2 -Wall -Wextra \
 -D__SWITCH__ -DHZ_PLATFORM_SWITCH -march=armv8-a+crc+crypto -mtp=soft -fPIE \
 -Ibuild -I/opt/devkitpro/libnx/include -specs=/opt/devkitpro/libnx/switch.specs \
 -L/opt/devkitpro/libnx/lib -ffile-prefix-map=/test=. main.c -lnx -o build/proof.elf
/opt/devkitpro/tools/bin/nacptool --create "Private partition write proof" "Manhunt Recompilation Team" "0.0.1" build/proof.nacp
/opt/devkitpro/tools/bin/elf2nro build/proof.elf build/proof.nro --nacp=build/proof.nacp
'
 sha256sum "$work/proof.nro"
 mkdir -p "$work/eden-data/sdmc"
 EMU_DATA="${EMU_DATA:-$work/eden-data}" EMU_LOG="${EMU_LOG:-$work/eden.log}" "$root/scripts/emu-run.sh" "$work/proof.nro" HZ-PARTITION-PROOF 120
fi
