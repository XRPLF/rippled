ARG BASE_IMAGE=debian:bookworm

FROM ${BASE_IMAGE}

COPY bin/install-packaging-tools.sh /tmp/install-packaging-tools.sh

RUN /tmp/install-packaging-tools.sh

# See ../README.md, "Publishing from other repositories".
COPY package/docker/publish_pkg.py /usr/local/bin/publish_pkg.py
