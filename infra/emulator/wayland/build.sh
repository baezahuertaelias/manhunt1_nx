#!/usr/bin/env bash
set -euo pipefail
expected=sha256:68d0f0847e3ff18f84eedb227b38b5e1a977c036dc202ee6b01debf72762fecb
[[ "$(docker image inspect --format '{{.Id}}' dd-eden:local)" == "$expected" ]] || {
  echo 'Unexpected Eden base image; refusing build' >&2; exit 1;
}
docker build -t manhunt-eden-wayland:weston-14.0.2 "$(dirname "$0")"
docker image inspect --format '{{.Id}}' manhunt-eden-wayland:weston-14.0.2
