#!/usr/bin/env bash
# Run in manhunt-recompiler:xboxrecomp-f0f8a46 with --user 0.
set -euo pipefail
export MANHUNT_AMBIENT_LIGHT=-1
root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$root"
out="$root/game/build/hw-test/2026-11-10-gpu-offload/host"
mkdir -p "$out"
run() { local name="$1"; shift; if "$@" >"$out/$name.log" 2>&1; then printf 'PASS %s\n' "$name"; else cat "$out/$name.log"; return 1; fi; }
run review-legacy env MANHUNT_GL_PERSISTENT=0 MANHUNT_GL_UBO=0 MANHUNT_GL_STATE_CACHE=0 bash tests/nv2a-gl-review/run.sh
run review-new env MANHUNT_GL_PERSISTENT=1 MANHUNT_GL_UBO=1 MANHUNT_GL_STATE_CACHE=1 bash tests/nv2a-gl-review/run.sh
run queue-pack bash tests/nv2a-gl-review/run-queue-pack.sh
run streaming bash tests/nv2a-gl-review/run-streaming.sh
run ubo bash tests/nv2a-gl-review/run-ubo.sh
run ubo-real bash tests/nv2a-gl-review/run-ubo-real.sh
run ambient bash tests/nv2a-gl-review/run-ambient.sh
run validation bash tests/nv2a-gl-review/run-validation.sh
run progcache bash tests/nv2a-gl-review/run-progcache.sh
run backend-contract bash tests/nv2a-gl-review/run-contract.sh
run state-cache bash tests/nv2a-gl-review/run-state-cache.sh
run lighting bash tests/shader-lighting/run.sh
run cmp bash tests/shader-cmp/run.sh
run audio-apu bash tests/audio-apu/run.sh
run real-legacy env MANHUNT_PERF_LOG=2 MANHUNT_GL_PERSISTENT=0 MANHUNT_GL_UBO=0 MANHUNT_GL_STATE_CACHE=0 bash tests/nv2a-gl-review/run-real.sh
run real-persistent env MANHUNT_PERF_LOG=2 MANHUNT_GL_PERSISTENT=1 MANHUNT_GL_UBO=1 MANHUNT_GL_STATE_CACHE=1 bash tests/nv2a-gl-review/run-real.sh
run real-packed-fallback env MESA_EXTENSION_OVERRIDE=-GL_ARB_buffer_storage MANHUNT_PERF_LOG=2 MANHUNT_GL_PERSISTENT=1 MANHUNT_GL_UBO=1 MANHUNT_GL_STATE_CACHE=1 bash tests/nv2a-gl-review/run-real.sh
