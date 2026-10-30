#!/usr/bin/env bash
# Calendar contracts through the exact production Horizon implementation.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
mode="${1:-host}"
case "$mode" in host|switch) ;; *) echo 'usage: horizon-time-test.sh [host|switch]' >&2; exit 2;; esac
work="$root/tests/horizon-time-proof"
mkdir -p "$work/build"
sha256sum "$root/game/patches/xboxrecomp/0013-horizon-time-conversion.patch" "$work/main.c" "$0"
image=sha256:fc226cc9dd0cead9bd0a5e4d3f59a705812a9c699c0a4c105d05e8da893eec63
docker run --rm --network none --cpus 1 --memory 1g --user "$(id -u):$(id -g)" \
 --cap-drop ALL --security-opt no-new-privileges \
 --mount "type=bind,src=$root/game/patches/xboxrecomp,dst=/patches,readonly" \
 --mount "type=bind,src=$work,dst=/test" "$image" bash -ec '
cp -r /opt/xboxrecomp /tmp/runtime
for patch in /patches/*.patch; do
 if [[ "$patch" == */0013-horizon-time-conversion.patch ]]; then cp /tmp/runtime/src/platform/win32_compat.c /test/build/legacy-source.c; fi
 git -C /tmp/runtime apply "$patch"
done
mkdir -p /test/build/runtime /test/build/negative
cp /tmp/runtime/src/platform/{win32_compat.c,win32_compat.h,win32_compat_horizon_time.inc,xbox_winnt.h} /test/build/runtime/
python3 - <<'"'"'PY'"'"'
from pathlib import Path
s=Path("/test/build/legacy-source.c").read_text()
def function(signature):
 start=s.index(signature);brace=s.index("{",start);depth=1;end=brace+1
 while depth:
  depth+=(s[end]=="{")-(s[end]=="}");end+=1
 return s[start:end]+"\n"
prefix="#define _POSIX_C_SOURCE 200809L\n#include <stdio.h>\n#include <string.h>\n#include <time.h>\n#include \"win32_compat.h\"\n#define FILETIME_EPOCH_DIFF 116444736000000000ULL\n"
body="".join(function(sig) for sig in ["static time_t timegm(","static void fill_systemtime(","BOOL SystemTimeToFileTime(","BOOL FileTimeToSystemTime("])
driver="""
int main(void) {
 SYSTEMTIME invalid={.wYear=2001,.wMonth=2,.wDay=29},epoch;
 FILETIME f={0,0};
 int accepted=SystemTimeToFileTime(&invalid,&f)!=FALSE;
 f.dwLowDateTime=f.dwHighDateTime=0;
 int rejected=FileTimeToSystemTime(&f,&epoch)==FALSE;
 printf("HZ-TIME-BASELINE invalid_date_accepted=%d pre1970_rejected=%d\\n",accepted,rejected);
 return !(accepted&&rejected);
}
"""
Path("/test/build/legacy.c").write_text(prefix+body+driver)
p=Path("/test/build/runtime/win32_compat_horizon_time.inc")
text=p.read_text();old="if (!hz_time_valid(st)) return hz_time_invalid();";assert text.count(old)==1
Path("/test/build/negative/win32_compat_horizon_time.inc").write_text(text.replace(old,"if (!hz_time_valid(st) && 0) return hz_time_invalid();"))
PY
cc -std=c11 -O2 -Wall -Wextra -Werror -I/test/build/runtime /test/build/legacy.c -o /test/build/legacy
/test/build/legacy
cc -std=c11 -O2 -Wall -Wextra -Werror -I/test/build/runtime /test/main.c -o /test/build/host
/test/build/host
cc -std=c11 -O2 -Wall -Wextra -Werror -I/test/build/negative -I/test/build/runtime /test/main.c -o /test/build/negative-test
if /test/build/negative-test > /test/build/negative.log; then echo "HZ-TIME-NEGATIVE FAIL missing date validation was not detected"; exit 1; fi
grep -F "HZ-TIME-PROOF FAIL" /test/build/negative.log
echo "HZ-TIME-NEGATIVE PASS removed date validation detected"
'
sha256sum "$work/build/runtime/"*
if [ "$mode" = switch ]; then
 image=devkitpro/devkita64@sha256:1fc388c3a0d34bd2045a6dadcb1020e069d5f876a187fd705de14b4440c00282
 docker run --rm --network none --cpus 1 --memory 1g --user "$(id -u):$(id -g)" \
  --cap-drop ALL --security-opt no-new-privileges \
  --mount "type=bind,src=$work,dst=/test" -w /test "$image" bash -ec '
/opt/devkitpro/devkitA64/bin/aarch64-none-elf-gcc -std=c11 -O2 -Wall -Wextra -Werror \
 -D__SWITCH__ -march=armv8-a+crc+crypto -mtp=soft -fPIE -ffunction-sections \
 -ffile-prefix-map=/test=. -Ibuild/runtime -I/opt/devkitpro/libnx/include \
 -specs=/opt/devkitpro/libnx/switch.specs -L/opt/devkitpro/libnx/lib \
 -Wl,--gc-sections main.c -lnx -o build/proof.elf
/opt/devkitpro/tools/bin/nacptool --create "Horizon UTC calendar proof" "Manhunt Recompilation Team" "0.0.1" build/proof.nacp
/opt/devkitpro/tools/bin/elf2nro build/proof.elf build/proof.nro --nacp=build/proof.nacp
'
 sha256sum "$work/build/proof.nro"
 "$root/scripts/emu-run.sh" "$work/build/proof.nro" HZ-TIME-PROOF 120
fi
