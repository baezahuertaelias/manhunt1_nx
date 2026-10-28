# Optional presentation proof only; the base's source provenance is EMU-IMAGE-001.
FROM dd-eden:local
USER root
RUN apt-get update \
 && apt-get install --no-install-recommends -y weston=14.0.2-1 libweston-14-0=14.0.2-1 \
 && dpkg-query -W > /usr/local/share/manhunt-wayland-packages.txt \
 && rm -rf /var/lib/apt/lists/*
USER 1000:1000
