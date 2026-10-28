# Host setup

How to get a host ready to work on this project. It is based on the host the project was developed on. Facts were rechecked on 2026-10-28 unless marked otherwise.

After the host is ready, continue with [SETUP.md](SETUP.md): inputs, images, builds and proofs.

## Reference host

| Item | Reference value | Needed for |
|---|---|---|
| OS | Debian GNU/Linux 13 (trixie), kernel 7.1.3, x86-64 | Any x86-64 Linux with Docker should work; others are UNVERIFIED |
| CPU | Intel N150, 4 cores | `CPUS` defaults to `nproc`. Heavy jobs (Ghidra, lift, compile, Eden) run one at a time |
| RAM | 15 GiB | Container limits: recompiler 8 GiB, analysis 6 GiB, Eden 6 GiB, switch-build 4 GiB. Plan for 8 GiB free while a job runs |
| Disk | NVMe, 535 GiB free | See the disk budget below |
| GPU | Intel iGPU: `/dev/dri/card0` (group `video`), `/dev/dri/renderD128` (group `render`) | Optional. Eden uses llvmpipe by default; `EMU_GPU=1` currently fails (no present queue) |
| Access | Headless, over SSH | No display needed: Eden runs under Xvfb inside its container |

## Host software

The project does not install host packages. Everything else runs in Docker. The host needs only:

| Tool | Reference version | Used by |
|---|---|---|
| Docker Engine + CLI | 29.7.2 | every build, test and run script |
| Docker Compose plugin | v5.4.0 | `infra/compose.yaml` (optional) |
| git | 2.47.3 | version control; `scripts/game-build.sh` reads the revision |
| bash, coreutils (`sha256sum`, `timeout`, `stat`, `mktemp`), findutils, `patch` | Debian 13 defaults | scripts |
| python3 | 3.13.5 | small host-side helpers (`analysis/xbe_header.py`, `scripts/bmp2png.py`, `scripts/fix-functions.py`); run with `-I` |
| gh (GitHub CLI) | 2.46.0 | pull requests; needs `gh auth login` with the `repo` scope |
| jq | 1.7 | optional, for inspecting JSON |

There is no host devkitPro, Ghidra, compiler toolchain or emulator. Those live in the images below.

On a fresh Debian host, installing these is a **host change**. Under AGENTS.md it needs the owner's explicit approval, and the project scripts never do it. For reference only:

```sh
sudo apt install docker.io docker-compose git python3 gh jq patch   # or Docker's own packages
```

## User and permissions

The working user must be able to use Docker without sudo, and should be able to open the GPU nodes:

```sh
id -nG        # reference host: ... video docker render ...
docker info   # must work without sudo
```

Being in the `docker` group is effectively root on the host. That is accepted on this single-user machine. Containers still run as uid 1000 with `no-new-privileges` and no network. The project uid/gid is `1000:1000` (`.env`: `UID`, `GID`), so outputs stay owned by the user. A host user with a different uid has to set these.

## Repository

```sh
git clone https://github.com/baezahuertaelias/manhunt1_nx
cd manhunt1_nx
cp .env.example .env
gh auth status            # must be logged in; never print the token
```

The repository is public and `main` is protected: work goes on a branch and merges through a pull request (see AGENTS.md).

## Inputs (never in Git)

Place the lawfully obtained game files in `assets/manhunt1_xbe/` (gitignored). The `DEFAULT.xbe` must have SHA-256 `af69e803695e29c3f99b38d6a759fac4c9300c9a540a2c244e4e7a07fb96afde`. Verify the whole set against `reports/ASSET_MANIFEST.sha256` (2.4 GiB, 836 files).

## Docker images

| Image | How to get it | Size |
|---|---|---|
| `devkitpro/devkita64@sha256:1fc388c3…0282` | pulled on first use (needs network once) | 2.0 GB |
| `manhunt-analysis:ghidra-xbe` | `scripts/build-analysis-image.sh` (downloads hash-checked Ghidra 12.1.4 and the XBE loader) | 1.4 GB |
| `manhunt-recompiler:xboxrecomp-f0f8a46` | `docker build -t manhunt-recompiler:xboxrecomp-f0f8a46 infra/recompiler` | 1.4 GB |
| `dd-eden:local` (ID `sha256:68d0f084…fecb`) | **not buildable from this repo.** It is a host-local image from another project (EMU-IMAGE-001) | 4.0 GB |

On a new host, the Eden image must be copied from the reference host (`docker save dd-eden:local | ssh newhost docker load`), or the emulator stages stay UNVERIFIED. `scripts/emu-run.sh` refuses any image whose ID differs.

The image builds need outbound HTTPS (Docker Hub, GitHub, Debian and PyPI mirrors). Every build and test container then runs with `--network none`.

## Disk budget

| Item | Size |
|---|---|
| Images above | about 9 GB |
| `assets/manhunt1_xbe/` | 2.4 GiB |
| `game/build/` (host and Switch builds, Eden data with the SD copy, frames) | 7.3 GiB measured |
| `tests/recomp-proof/build/` | about 100 MiB |
| Ghidra project (`ghidra-projects` volume) | about 50 MiB |

Allow at least 25 GiB free. `game/build/` and `tests/recomp-proof/build/` hold game-derived code, binaries and logs with XBE certificate keys. They are gitignored: never commit, share or upload them.

## Check that the host is ready

```sh
docker info --format '{{.ServerVersion}}'
docker image inspect --format '{{.Id}}' dd-eden:local   # must be sha256:68d0f0847e3f...
sha256sum assets/manhunt1_xbe/DEFAULT.xbe
docker compose -f infra/compose.yaml config -q
scripts/toolchain-proof.sh                              # milestone 1 gates; hardware stays UNVERIFIED
```

Record the results in [../reports/PREFLIGHT.md](../reports/PREFLIGHT.md) after any host change.

## Known host limits

- **Eden on the GPU:** `EMU_GPU=1` fails with no Vulkan present queue, so Eden runs on llvmpipe (software). Emulator speed is diagnostic only.
- **Other images:** the host also holds images and volumes from other projects (for example `dd-ghidra-mcp:local`, `downhill-domination_*`). Do not touch them.
- **Real Switch hardware:** not available. Every hardware result is UNVERIFIED.
