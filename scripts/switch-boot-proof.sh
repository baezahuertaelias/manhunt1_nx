#!/usr/bin/env bash
# Milestone 2 (Boot) proof on the Switch build in Eden.
#   1. build the NRO (scripts/game-build.sh switch) unless SKIP_BUILD=1;
#   2. copy the game data to the emulated SD card once, verified against
#      reports/ASSET_MANIFEST.sha256 (Eden opens SD files read/write, so a
#      read-only bind mount of the originals does not work);
#   3. run headless for SECS seconds (default 240) with frame dumps;
#   4. check: boot markers, the entry point reached, frames presented,
#      main-menu assets opened (MAIN_EX.TXD, FE_M.RIB), no crash/exit, and
#      dumped frames that are not all black and change over time.
# Prints MH-BOOT-PROOF PASS|FAIL. Frames land in game/build/eden-data/sdmc/
# manhunt-frames (PNG beside each BMP) for the visual check, which is manual:
# game-derived reference images are not stored in the repo. Hardware: UNVERIFIED.
set -uo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
secs="${SECS:-240}"
data="$root/game/build/eden-data"
sd="$data/sdmc"
src="$root/${GAME_INPUT_DIR:-assets/manhunt1_xbe}"
cd "$root"

if [ "${SKIP_BUILD:-0}" != 1 ]; then
  scripts/game-build.sh switch > /dev/null || { echo "MH-BOOT-PROOF FAIL build"; exit 1; }
fi

mkdir -p "$sd/manhunt"
want=$(grep -m1 'DEFAULT.xbe' reports/ASSET_MANIFEST.sha256 | cut -d' ' -f1)
have=$(sha256sum "$sd/manhunt/DEFAULT.xbe" 2>/dev/null | cut -d' ' -f1)
if [ "$have" != "$want" ]; then
  echo "copying game data to the emulated SD card"
  cp -r "$src/." "$sd/manhunt/"
fi
( cd "$sd" && "$root/scripts/asset-manifest.sh" manhunt | tail -n +2 ) \
  | diff -q - <(tail -n +2 reports/ASSET_MANIFEST.sha256) > /dev/null \
  || { echo "MH-BOOT-PROOF FAIL SD copy does not match the asset manifest"; exit 1; }

rm -rf "$sd/manhunt-frames"
printf 'MANHUNT_DUMP_SECS=20\n' > "$sd/manhunt.env"
log="$root/game/build/logs/switch-boot-$(date +%Y%m%d-%H%M%S).log"
EMU_DATA="$data" EMU_LOG="$log" scripts/emu-run.sh game/build/switch/manhunt.nro - "$secs" > /dev/null 2>&1
emu=$?
guest="${log%.log}.guest.log"
grep -a 'OutputDebugString' "$log" | sed 's/.*OutputDebugString: //' > "$guest"

python3 -I scripts/bmp2png.py "$sd/manhunt-frames" > /dev/null 2>&1 || true
frames=$(python3 -I - "$sd/manhunt-frames" <<'EOF'
import hashlib, os, sys
d = sys.argv[1]
fs = sorted(f for f in os.listdir(d) if f.endswith(".bmp")) if os.path.isdir(d) else []
lit = sum(1 for f in fs if any(open(os.path.join(d, f), "rb").read()[54:]))
uniq = len({hashlib.sha256(open(os.path.join(d, f), "rb").read()).hexdigest() for f in fs})
print(len(fs), lit, uniq)
EOF
)
read -r n_frames n_lit n_uniq <<< "$frames"
presents=$(grep -ao 'MH-FRAME\] present [0-9]*' "$guest" | tail -1 | awk '{print $3}')
dumps_presents=$(grep -ao 'presents=[0-9]*' "$guest" | tail -1 | cut -d= -f2)

fail=()
grep -aq '\[MH-BOOT\] entry 0x001FE908' "$guest" || fail+=("entry point not reached")
[ "${dumps_presents:-0}" -ge 10 ] || fail+=("fewer than 10 frames presented (${dumps_presents:-0})")
grep -aq 'pictures\\menu\\MAIN_EX.TXD' "$guest" || fail+=("main-menu textures not opened")
grep -aq 'MUSIC\\FRONTEND\\FE_M.RIB' "$guest" || fail+=("front-end music not opened")
grep -aqE '\[CRASH\]|\[EXIT\]|HalReturnToFirmware|entry point returned|\[MH-BOOT\] FAIL' "$guest" && fail+=("crash/exit/FAIL line in guest log")
[ "$emu" -eq 0 ] || fail+=("emulator stopped before ${secs}s (emu-run exit $emu)")
[ "${n_lit:-0}" -ge 1 ] || fail+=("no non-black frame dumped")
[ "${n_uniq:-0}" -ge 3 ] || fail+=("dumped frames do not change")

echo "guest log: $guest"
echo "frames: ${n_frames:-0} dumped, ${n_lit:-0} non-black, ${n_uniq:-0} distinct; presents=${dumps_presents:-0}"
if [ ${#fail[@]} -eq 0 ]; then
  echo "MH-BOOT-PROOF PASS (Eden; hardware UNVERIFIED)"
  exit 0
fi
printf 'MH-BOOT-PROOF FAIL: %s\n' "${fail[@]}"
exit 1
