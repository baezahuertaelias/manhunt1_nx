# Headless Ghidra + XboxDev ghidra-xbe loader, built from official, hash-verified
# releases. The loader zip is built for one Ghidra release; versions must match.
ARG JDK_IMAGE=eclipse-temurin:21-jdk-noble@sha256:107215954359f1ab0be058a247d7c64ed6e69c71c1682d471d38ef751ec8e964

FROM ${JDK_IMAGE} AS fetch

ARG GHIDRA_VERSION=12.1.4
ARG GHIDRA_URL=https://github.com/NationalSecurityAgency/ghidra/releases/download/Ghidra_12.1.4_build/ghidra_12.1.4_PUBLIC_20260921.zip
ARG GHIDRA_SHA256=ddac49f903da9d5bac833e5cc79395098b9c33cfd3279be5f31bd00387d2d4db
ARG XBE_LOADER_URL=https://github.com/XboxDev/ghidra-xbe/releases/download/build-202610161023/ghidra_12.1.4_PUBLIC_20261016_ghidra-xbe.zip
ARG XBE_LOADER_SHA256=339b2f67563bbb8b5217740319b7f3eae9e9bbacf1f3a8aa29151f7ecb7d5152

RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends unzip \
    && rm -rf /var/lib/apt/lists/*

RUN curl -fsSL "$GHIDRA_URL" -o /tmp/ghidra.zip \
    && echo "$GHIDRA_SHA256  /tmp/ghidra.zip" | sha256sum -c - \
    && unzip -q /tmp/ghidra.zip -d /tmp/ghidra \
    && mv /tmp/ghidra/ghidra_* /opt/ghidra \
    && rm -rf /tmp/ghidra.zip /tmp/ghidra

RUN curl -fsSL "$XBE_LOADER_URL" -o /tmp/ghidra-xbe.zip \
    && echo "$XBE_LOADER_SHA256  /tmp/ghidra-xbe.zip" | sha256sum -c - \
    && unzip -q /tmp/ghidra-xbe.zip -d /opt/ghidra/Ghidra/Extensions \
    && rm /tmp/ghidra-xbe.zip

# Refuse a Ghidra/loader pairing that does not match the pinned version.
RUN grep -qx "application.version=${GHIDRA_VERSION}" /opt/ghidra/Ghidra/application.properties \
    && grep -qx "version=${GHIDRA_VERSION}" /opt/ghidra/Ghidra/Extensions/ghidra-xbe/extension.properties

FROM ${JDK_IMAGE}

# uid/gid 1000 is the image's existing "ubuntu" user; run as it, not root.
COPY --from=fetch --chown=1000:1000 /opt/ghidra /opt/ghidra
RUN install -d -o 1000 -g 1000 /projects

ENV GHIDRA_HOME=/opt/ghidra \
    JAVA_OPTS="-Xmx4g -XX:+UseG1GC"
USER 1000:1000
WORKDIR /projects
ENTRYPOINT ["/opt/ghidra/support/analyzeHeadless"]
