#!/usr/bin/env bash
# One physical-Switch test cycle from a Mac, so every run is prepared, recorded and
# summarised the same way. You only start and close the game on the console.
#
#   SWITCH_IP=<console ip> scripts/hw-run.sh [options]
#
# Needs: Sphaira running on the console with its FTP server (port 5000), a build in
# game/build/switch/manhunt.nro, curl, nc, shasum, python3. Never prints credentials.
#
# What it does:
#   1. preflight: refuses a config that makes a run meaningless or risky
#   2. uploads the NRO and manhunt.env over FTP and verifies both by reading them back
#   3. starts a TCP log listener and a ping monitor on this Mac
#   4. waits for you to launch the game (Sphaira's FTP closes) and close it again (FTP returns)
#   5. collects log.txt, wd.txt, frames and new Atmosphere reports, then writes summary.md
#
# Results go to game/build/hw-test/<date>/run-<time>[-tag]/ (gitignored: game-derived).
#
# Options:
#   --nro FILE            NRO to upload (default game/build/switch/manhunt.nro)
#   --env FILE            use this manhunt.env verbatim (after preflight) instead of the generated one
#   --extra KEY=VALUE     add a key to the generated env (repeatable)
#   --profile             render-timing run: RECOMP_RENDER_PROFILE=1, no frame dumps, no fsync log
#   --tag NAME            label for the run directory
#   --allow-gpu-backend   permit MANHUNT_GPU_BACKEND=1 (UNVERIFIED on the console after patch 0016)
#   --wait-start SECS     how long to wait for you to launch (default 900)
#   --max-run SECS        how long to wait for the game to be closed (default 900)
#   --no-upload           do not upload; only listen, collect and summarise
#   --dry-run             run the preflight and print the plan; touch nothing on the console
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
IP="${SWITCH_IP:-}"
FTP_PORT="${SWITCH_FTP_PORT:-5000}"
LOG_PORT="${HW_LOG_PORT:-5555}"
REMOTE="sdmc:/switch/manhunt1_nx"
NRO="$root/game/build/switch/manhunt.nro"
ENVFILE=""
EXTRA=()
PROFILE=0; TAG=""; ALLOW_GPU=0; WAIT_START=900; MAX_RUN=900; UPLOAD=1; DRY=0

while [ $# -gt 0 ]; do
  case "$1" in
    --nro) NRO="$2"; shift 2 ;;
    --env) ENVFILE="$2"; shift 2 ;;
    --extra) EXTRA+=("$2"); shift 2 ;;
    --profile) PROFILE=1; shift ;;
    --tag) TAG="$2"; shift 2 ;;
    --allow-gpu-backend) ALLOW_GPU=1; shift ;;
    --wait-start) WAIT_START="$2"; shift 2 ;;
    --max-run) MAX_RUN="$2"; shift 2 ;;
    --no-upload) UPLOAD=0; shift ;;
    --dry-run) DRY=1; shift ;;
    -h|--help) sed -n '2,36p' "$0"; exit 0 ;;
    *) echo "hw-run: unknown option $1" >&2; exit 2 ;;
  esac
done

say() { printf '[hw-run] %s\n' "$*"; }
die() {
  printf '[hw-run] ERROR: %s\n' "$*" >&2
  # A run that never reached the console leaves no directory behind.
  if [ -n "${RUN:-}" ] && [ -d "$RUN" ] && [ -z "$(ls "$RUN" | grep -v '^manhunt.env.sent$' || true)" ]; then rm -rf "$RUN"; fi
  exit 1
}

[ -n "$IP" ] || die "set SWITCH_IP=<console ip> (not stored in the repo)"

# This Mac's address on the same network as the console (for the TCP log).
mac_ip() {
  local ip
  for i in en0 en1 en2; do
    ip="$(ipconfig getifaddr "$i" 2>/dev/null || true)"
    [ -n "$ip" ] && { echo "$ip"; return; }
  done
  die "cannot find this Mac's IP address"
}
MAC_IP="$(mac_ip)"

date_dir="$root/game/build/hw-test/$(date +%Y-%m-%d)"
RUN="$date_dir/run-$(date +%H%M%S)${TAG:+-$TAG}"

# ---- the manhunt.env that will be uploaded
gen_env() {
  local sync=1 dump=5
  [ "$PROFILE" = 1 ] && { sync=0; dump=0; }
  {
    echo "MANHUNT_LOG_FILE=$REMOTE/log.txt"
    echo "MANHUNT_LOG_SYNC=$sync"
    echo "MANHUNT_WD=1"
    echo "MANHUNT_LOG_TCP=$MAC_IP:$LOG_PORT"
    echo "MANHUNT_DUMP_SECS=$dump"
    echo "RECOMP_SPAN_4G=1"
    if [ "$PROFILE" = 1 ]; then
      echo "RECOMP_RENDER_PROFILE=1"
      echo "RECOMP_PB_REPORT_MS=5000"
    fi
    for kv in ${EXTRA[@]+"${EXTRA[@]}"}; do echo "$kv"; done
  }
}

mkdir -p "$RUN"
if [ -n "$ENVFILE" ]; then
  [ -f "$ENVFILE" ] || die "env file not found: $ENVFILE"
  cp "$ENVFILE" "$RUN/manhunt.env.sent"
else
  gen_env > "$RUN/manhunt.env.sent"
fi

# ---- 1. preflight
say "preflight"
[ -f "$NRO" ] || die "NRO not found: $NRO (build it: scripts/game-build-native.sh)"
NRO_SHA="$(shasum -a 256 "$NRO" | cut -d' ' -f1)"
say "NRO $(basename "$NRO") sha256 ${NRO_SHA:0:12}... $(wc -c < "$NRO" | tr -d ' ') bytes"

if grep -q '^RECOMP_WATCHDOG_SECS' "$RUN/manhunt.env.sent"; then
  die "RECOMP_WATCHDOG_SECS in the env: it calls _exit() after that many seconds and ends the run (it ended the first successful run). Remove it."
fi
if grep -q '^MANHUNT_PAD_SCRIPT' "$RUN/manhunt.env.sent"; then
  die "MANHUNT_PAD_SCRIPT in the env: the real controller would be ignored."
fi
if grep -q '^MANHUNT_GPU_BACKEND=' "$RUN/manhunt.env.sent" && ! grep -q '^MANHUNT_GPU_BACKEND=0' "$RUN/manhunt.env.sent" && [ "$ALLOW_GPU" != 1 ]; then
  die "MANHUNT_GPU_BACKEND (deko3d) is UNVERIFIED on the console after patch 0016 (early runs froze with and without it, so it is not the known cause of the freeze). Pass --allow-gpu-backend to run it."
fi
if grep -q '^MANHUNT_LOG_TCP' "$RUN/manhunt.env.sent" && ! grep -q "^MANHUNT_LOG_TCP=$MAC_IP:" "$RUN/manhunt.env.sent"; then
  say "note: the env's MANHUNT_LOG_TCP does not point at this Mac ($MAC_IP); the live stream will stay empty"
fi
if ! grep -q '^RECOMP_SPAN_4G=1' "$RUN/manhunt.env.sent"; then
  say "note: RECOMP_SPAN_4G=1 is missing; without it the register apertures can land in the libnx heap (reports/HW_SWITCH_FIRST_RUN.md)"
fi
if [ "$PROFILE" = 1 ]; then say "profile run: render timing on, frame dumps and fsync logging off"; fi
say "env that will be uploaded:"; sed 's/^/    /' "$RUN/manhunt.env.sent"

ftp_url() { echo "ftp://$IP:$FTP_PORT/$REMOTE/$1"; }
ftp_up() { nc -z -G 3 "$IP" "$FTP_PORT" >/dev/null 2>&1; }

if [ "$DRY" = 1 ]; then
  say "dry run: plan = upload NRO + env to $REMOTE on $IP, listen on $MAC_IP:$LOG_PORT, collect to $RUN. Nothing was touched."
  rm -rf "$RUN"
  exit 0
fi

# ---- cleanup of background helpers
LISTENER=""; MONITOR=""
cleanup() {
  [ -n "$LISTENER" ] && kill "$LISTENER" 2>/dev/null || true
  [ -n "$MONITOR" ] && kill "$MONITOR" 2>/dev/null || true
}
trap cleanup EXIT

# ---- 2. upload and verify
if [ "$UPLOAD" = 1 ]; then
  ftp_up || die "the console's FTP is closed: open Sphaira with FTP on (and do not run the game)"
  tmp="$RUN/.readback"
  RATE="${HW_RATE:-1500k}"   # Sphaira's FTP drops full-speed uploads; this rate worked
  # Skip the upload when the card already holds exactly this NRO.
  curl -s --max-time 300 -o "$tmp" "$(ftp_url manhunt.nro)" || true
  if [ "$(shasum -a 256 "$tmp" 2>/dev/null | cut -d' ' -f1)" = "$NRO_SHA" ]; then
    say "NRO already on the card (SHA-256 matches); upload skipped"
  else
    # Upload under a temporary name and rename only after the hash is verified, so a
    # broken transfer can never leave a truncated manhunt.nro behind.
    sent=0
    for try in 1 2 3 4 5 6 7 8; do
      if curl -s --max-time 900 --limit-rate "$RATE" -T "$NRO" "$(ftp_url manhunt.nro.part)"; then rc=0; else rc=$?; fi
      say "NRO upload try $try rc=$rc"
      if [ "$rc" = 0 ]; then
        curl -s --max-time 300 -o "$tmp" "$(ftp_url manhunt.nro.part)" || true
        if [ "$(shasum -a 256 "$tmp" 2>/dev/null | cut -d' ' -f1)" = "$NRO_SHA" ]; then sent=1; break; fi
        say "read-back hash differs, retrying"
      fi
      sleep 8
    done
    [ "$sent" = 1 ] || die "could not upload the NRO and verify it (the card's manhunt.nro was not touched)"
    curl -s --max-time 30 -Q "-DELE /$REMOTE/manhunt.nro" "ftp://$IP:$FTP_PORT/" -o /dev/null 2>/dev/null || true
    curl -s --max-time 30 -Q "-RNFR /$REMOTE/manhunt.nro.part" -Q "-RNTO /$REMOTE/manhunt.nro" "ftp://$IP:$FTP_PORT/" -o /dev/null || die "could not rename the verified upload into place: the verified NRO is on the card as manhunt.nro.part and manhunt.nro is MISSING; fix or rerun"
    curl -s --max-time 300 -o "$tmp" "$(ftp_url manhunt.nro)" || true
    [ "$(shasum -a 256 "$tmp" 2>/dev/null | cut -d' ' -f1)" = "$NRO_SHA" ] || die "manhunt.nro on the card does not match after the rename"
    say "NRO on the card verified by SHA-256 read-back"
  fi

  curl -s --max-time 60 -T "$RUN/manhunt.env.sent" "$(ftp_url manhunt.env)" || die "env upload failed"
  curl -s --max-time 60 -o "$tmp" "$(ftp_url manhunt.env)" || die "env read-back failed"
  cmp -s "$tmp" "$RUN/manhunt.env.sent" || die "manhunt.env on the card differs from what was sent"
  say "manhunt.env on the card verified"
  rm -f "$tmp"

  # Cheap sanity check on the game data: DEFAULT.xbe size.
  want="$(wc -c < "$root/assets/manhunt1_xbe/DEFAULT.xbe" 2>/dev/null | tr -d ' ' || true)"
  have="$(curl -s --max-time 30 "ftp://$IP:$FTP_PORT/$REMOTE/" | awk '$NF=="DEFAULT.xbe"{print $5}')"
  if [ -n "$want" ] && [ "$want" != "0" ] && [ "$have" != "$want" ]; then
    say "WARNING: DEFAULT.xbe on the card is '$have' bytes, local is '$want' (game data missing or different)"
  else
    say "DEFAULT.xbe size matches the local copy"
  fi
fi

# Download one file from the console's folder and check its size against the card's listing,
# retrying: a truncated frame once crashed the summary and hid a result.
get_verified() {  # remote-relative-path local-path
  local rel="$1" dst="$2" dir base want got try
  dir="$(dirname "$rel")"; base="$(basename "$rel")"
  [ "$dir" = "." ] && dir=""
  want="$(curl -s --max-time 30 "$(ftp_url "${dir:+$dir/}")" 2>/dev/null | awk -v n="$base" '$NF==n{print $5}' | head -1)"
  for try in 1 2 3; do
    curl -s --max-time 180 -o "$dst" "$(ftp_url "$rel")" 2>/dev/null || true
    got="$(wc -c < "$dst" 2>/dev/null | tr -d ' ')"
    if [ -z "$want" ] || [ "$got" = "$want" ]; then return 0; fi
    say "download of $rel: got $got of $want bytes, retrying"
  done
  say "WARNING: $rel is incomplete ($got of $want bytes)"
  return 0
}

# What Atmosphere already holds, so only new reports are collected afterwards.
list_remote() { curl -s --max-time 30 --list-only "ftp://$IP:$FTP_PORT/$1/" 2>/dev/null | tr -d '\r' | sort || true; }
list_remote "sdmc:/atmosphere/crash_reports" > "$RUN/.crash.before"
list_remote "sdmc:/atmosphere/fatal_errors" > "$RUN/.fatal.before"

# ---- 3. listener and monitor
nc -l "$LOG_PORT" > "$RUN/tcp.log" 2>/dev/null &
LISTENER=$!
sleep 1
kill -0 "$LISTENER" 2>/dev/null || { LISTENER=""; die "the log listener could not start (is port $LOG_PORT already in use? set HW_LOG_PORT)"; }
(
  while true; do
    t="$(date +%H:%M:%S)"
    if ping -c1 -t1 "$IP" >/dev/null 2>&1; then p=up; else p=DOWN; fi
    if nc -z -G 1 "$IP" "$FTP_PORT" 2>/dev/null; then f=ftp; else f=-; fi
    echo "$t ping=$p $f"
    sleep 2
  done
) > "$RUN/ping.log" 2>&1 &
MONITOR=$!

# ---- 4. wait for the game
say "READY. On the console: launch manhunt.nro from Sphaira in title-takeover mode, then close it when you are done."
say "waiting for you to launch (up to ${WAIT_START}s)..."
waited=0
while ftp_up; do
  sleep 2; waited=$((waited + 2))
  [ "$waited" -lt "$WAIT_START" ] || { say "gave up waiting for a launch"; break; }
done
if ! ftp_up; then
  say "game started (Sphaira's FTP is closed); waiting for you to close it (up to ${MAX_RUN}s)..."
  ran=0; last=0
  until ftp_up; do
    sleep 3; ran=$((ran + 3))
    if [ $((ran - last)) -ge 30 ]; then
      last=$ran; say "running ${ran}s, stream $(wc -c < "$RUN/tcp.log" | tr -d ' ') bytes"
    fi
    [ "$ran" -lt "$MAX_RUN" ] || { say "timed out waiting for the game to end; summarising the stream only"; break; }
  done
fi

# ---- 5. collect and summarise
if ftp_up; then
  sleep 3
  say "Sphaira is back; collecting"
  get_verified log.txt "$RUN/log.txt"
  get_verified wd.txt "$RUN/wd.txt"
  mkdir -p "$RUN/frames"
  for f in $(curl -s --max-time 30 --list-only "$(ftp_url frames/)" 2>/dev/null | tr -d '\r' | tail -40); do
    get_verified "frames/$f" "$RUN/frames/$f"
    case "$f" in *.bmp|*.BMP) command -v sips >/dev/null 2>&1 && sips -s format png "$RUN/frames/$f" --out "$RUN/frames/${f%.*}.png" >/dev/null 2>&1 || true ;; esac
  done
  list_remote "sdmc:/atmosphere/crash_reports" > "$RUN/.crash.after"
  list_remote "sdmc:/atmosphere/fatal_errors" > "$RUN/.fatal.after"
  comm -13 "$RUN/.crash.before" "$RUN/.crash.after" > "$RUN/crash_reports.new" || true
  comm -13 "$RUN/.fatal.before" "$RUN/.fatal.after" > "$RUN/fatal_errors.new" || true
  for f in $(cat "$RUN/crash_reports.new"); do
    mkdir -p "$RUN/crash_reports"
    curl -s --max-time 60 -o "$RUN/crash_reports/$f" "ftp://$IP:$FTP_PORT/sdmc:/atmosphere/crash_reports/$f" || true
  done
else
  say "Sphaira did not come back: nothing collected from the SD card (UNVERIFIED which files exist)"
fi

cleanup; LISTENER=""; MONITOR=""
python3 -I "$root/scripts/hw-summarize.py" "$RUN"
say "all files: $RUN"
