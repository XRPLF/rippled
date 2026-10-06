ARG BASE_IMAGE=debian:trixie

FROM ${BASE_IMAGE}

RUN --mount=type=bind,source=bin/install-packaging-tools.sh,target=/install-packaging-tools.sh \
    /install-packaging-tools.sh

# See package/README.md, "Publishing from other repositories".
COPY package/images/packaging/publish_pkg.py /usr/local/bin/publish_pkg.py
