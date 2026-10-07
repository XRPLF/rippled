# Runtime image for the perf/test xrpld build with all amendments Supported::Yes.
# Installs the .deb into ubuntu:jammy (matching rippleci/xrpld): gives
# /usr/bin/xrpld, /etc/xrpld/xrpld.cfg, and the xrpld user.
# NOT for production validators.
ARG BASE_IMAGE=ubuntu:jammy@sha256:b8b6ee6aa931ecd9d0d952abc34dc0e5f7c6a30c6bb71b079fe399fde0329c02
FROM ${BASE_IMAGE}

# Build context must contain the supported package as xrpld.deb.
COPY xrpld.deb /tmp/xrpld.deb

RUN set -eux; \
    apt-get update; \
    apt-get install -y --no-install-recommends ca-certificates jq /tmp/xrpld.deb; \
    rm -rf /var/lib/apt/lists/* /tmp/xrpld.deb

RUN set -eux; \
    id -u xrpld >/dev/null 2>&1 || \
        useradd --system --home-dir /var/lib/xrpld --shell /sbin/nologin --user-group xrpld

# Symlink for consumers that exec /opt/xrpld/bin/xrpld.
RUN set -eux; \
    mkdir -p /var/log/xrpld /var/lib/xrpld /opt/xrpld/bin; \
    chown -R xrpld:xrpld /var/log/xrpld /var/lib/xrpld; \
    ln -sf /usr/bin/xrpld /opt/xrpld/bin/xrpld

EXPOSE 2459/tcp 5005/tcp 6006/tcp
USER xrpld
ENTRYPOINT ["/usr/bin/xrpld"]
