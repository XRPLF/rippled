# Runtime image for the perf/test xrpld build with all amendments Supported::Yes.
# Installs the .deb into ubuntu:jammy (matching rippleci/xrpld): gives
# /usr/bin/xrpld, /etc/xrpld/xrpld.cfg, and the xrpld user.
# NOT for production validators.
ARG BASE_IMAGE=ubuntu:jammy
FROM ${BASE_IMAGE}

# Build context must contain the supported package as xrpld.deb.
COPY xrpld.deb /tmp/xrpld.deb

RUN set -eux; \
    apt-get update; \
    apt-get install -y --no-install-recommends ca-certificates jq /tmp/xrpld.deb; \
    rm -rf /var/lib/apt/lists/* /tmp/xrpld.deb; \
    id -u xrpld >/dev/null 2>&1 || \
        useradd --system --home-dir /var/lib/xrpld --shell /sbin/nologin --user-group xrpld; \
    mkdir -p /var/log/xrpld /var/lib/xrpld; \
    chown -R xrpld:xrpld /var/log/xrpld /var/lib/xrpld; \
    # Symlink for consumers that exec /opt/xrpld/bin/xrpld.
    mkdir -p /opt/xrpld/bin; \
    ln -sf /usr/bin/xrpld /opt/xrpld/bin/xrpld

EXPOSE 2459/tcp 5005/tcp 6006/tcp
USER xrpld
ENTRYPOINT ["/usr/bin/xrpld"]
