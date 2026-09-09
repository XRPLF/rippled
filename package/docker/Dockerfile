ARG BASE_IMAGE=debian:trixie

FROM ${BASE_IMAGE}

# Bind-mounted rather than copied in, so the installer never lands in a layer.
RUN --mount=type=bind,source=bin/install-packaging-tools.sh,target=/install-packaging-tools.sh \
    /install-packaging-tools.sh

# See ../README.md, "Publishing from other repositories".
COPY package/docker/publish_pkg.py /usr/local/bin/publish_pkg.py
