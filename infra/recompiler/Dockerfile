# xboxrecomp lifter (x86 XBE -> C) plus the tools for the gate (a)/(b)
# differential proof: Unicorn as the x86-32 reference, host and AArch64 gcc,
# and qemu-user to run the AArch64 build. See reports/RECOMP_EVAL.md.
FROM python:3.12-slim-bookworm@sha256:6a0e06f5ba0b373d051aabd3fffb9b873f27f9a2a3a14bcbe37dee6f0e3bbc70

ARG XBOXRECOMP_REPO=https://github.com/sp00nznet/xboxrecomp.git
ARG XBOXRECOMP_COMMIT=f0f8a461496a4f23d9b0aeaa284b8d70ba7ab82b

RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
       git ca-certificates make gcc libc6-dev \
       gcc-aarch64-linux-gnu libc6-dev-arm64-cross qemu-user \
    && rm -rf /var/lib/apt/lists/*

# Milestone 2: build and run the full game against the xboxrecomp POSIX
# runtime (its tools/linux/install_deps.sh list), headless under Xvfb/llvmpipe.
RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
       cmake ninja-build build-essential pkg-config gdb \
       libsdl2-dev libgl1-mesa-dev libepoxy-dev libssl-dev \
       libavcodec-dev libavformat-dev libavutil-dev libswscale-dev \
       xvfb xauth libgl1-mesa-dri mesa-utils \
    && rm -rf /var/lib/apt/lists/*

RUN pip install --no-cache-dir capstone==5.0.9 unicorn==2.1.4 pytest==9.1.1

# The commit SHA pins the content; a mismatch fails the checkout.
RUN git init -q /opt/xboxrecomp \
    && git -C /opt/xboxrecomp fetch -q --depth 1 "$XBOXRECOMP_REPO" "$XBOXRECOMP_COMMIT" \
    && git -C /opt/xboxrecomp checkout -q FETCH_HEAD \
    && test "$(git -C /opt/xboxrecomp rev-parse HEAD)" = "$XBOXRECOMP_COMMIT" \
    && chown -R 1000:1000 /opt/xboxrecomp

RUN useradd --uid 1000 --create-home recomp
USER 1000:1000
WORKDIR /opt/xboxrecomp
