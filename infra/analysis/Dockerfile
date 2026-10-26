# Base is a host-local image with no registry digest; scripts/build-analysis-image.sh
# verifies its image ID before building. curl/unzip are provided by the base.
ARG BASE_IMAGE=dd-ghidra-mcp:local
FROM ${BASE_IMAGE}

USER root

ARG GHIDRA_VERSION=12.1.4
ARG XBE_LOADER_URL=https://github.com/XboxDev/ghidra-xbe/releases/download/build-202610161023/ghidra_12.1.4_PUBLIC_20261016_ghidra-xbe.zip
ARG XBE_LOADER_SHA256=339b2f67563bbb8b5217740319b7f3eae9e9bbacf1f3a8aa29151f7ecb7d5152

# The loader zip is built for one Ghidra release; refuse a mismatched base.
RUN grep -qx "application.version=${GHIDRA_VERSION}" /opt/ghidra/Ghidra/application.properties \
    || { echo "base Ghidra is not ${GHIDRA_VERSION}:" >&2; grep application.version /opt/ghidra/Ghidra/application.properties >&2; exit 1; }

RUN curl -fsSL "$XBE_LOADER_URL" -o /tmp/ghidra-xbe.zip \
    && echo "$XBE_LOADER_SHA256  /tmp/ghidra-xbe.zip" | sha256sum -c - \
    && unzip -q /tmp/ghidra-xbe.zip -d /opt/ghidra/Ghidra/Extensions \
    && rm /tmp/ghidra-xbe.zip

USER ghidra
