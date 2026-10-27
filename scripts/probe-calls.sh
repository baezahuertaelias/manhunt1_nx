#!/usr/bin/env bash
# Print guest ebx/esp at every call line (and return) of one lifted function,
# for its first invocation. Usage: scripts/probe-calls.sh sub_XXXXXXXX [seconds]
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
fn="$1"; secs="${2:-40}"
f=$(grep -l "^void $fn(void)\$" "$root"/game/build/gen/*.c)
b=$(basename "$f")
s=$(grep -n "^void $fn(void)\$" "$f" | cut -d: -f1)
e=$(awk -v s="$s" 'NR>s && /^}$/ {print NR; exit}' "$f")
out="$root/game/build/probe-$fn.gdb"
{
  echo "set pagination off"; echo "set confirm off"
  for sg in SIGSEGV SIGBUS SIG32 SIGUSR1 SIGUSR2; do echo "handle $sg nostop noprint pass"; done
  awk -v s="$s" -v e="$e" -v b="$b" 'NR>s && NR<=e && (/RECOMP_ABI_CALL|_icall_target|return;|PUSH32\(esp, ebx\)|POP32\(esp, ebx\)/) {
      m=$0; sub(/^ */,"",m); m=substr(m,1,70); gsub(/["\\]/,"",m);
      printf "break %s:%d\ncommands\nsilent\nprintf \"=== L%d ebx=%%08x esp=%%08x  %s\\n\", g_ebx, g_esp\ncontinue\nend\n", b, NR, NR, m }' "$f"
  echo "run /game > /dev/null 2>&1"
} > "$out"
"$root/scripts/game-gdb.sh" "$out" "$secs" | awk '!seen[$0]++'
