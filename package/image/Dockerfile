FROM ubuntu:26.04

RUN --mount=type=bind,target=/tmp/package \
    apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends /tmp/package/*.deb \
    && rm -rf /var/lib/apt/lists/* \
    && ln -sf /dev/stdout /var/log/xrpld/debug.log

USER xrpld
WORKDIR /var/lib/xrpld

EXPOSE 2459 5005 6006 50051

ENTRYPOINT ["/usr/bin/xrpld"]
CMD ["--net", "--silent", "--conf", "/etc/xrpld/xrpld.cfg"]
