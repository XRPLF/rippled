FROM ubuntu:26.04 AS xrpld

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

# The same image with the Antithesis-instrumented binary, which loads
# libvoidstar.so; Antithesis reads the debug symbols from /symbols.
FROM xrpld AS voidstar

USER root

ADD --chmod=644 --checksum=sha256:d080cc85f1d8d96452bdaf0021a10fa3e4cc3c319840f1fadd2d33904e607095 \
    https://antithesis.com/assets/instrumentation/libvoidstar.so /usr/lib/libvoidstar.so

RUN --mount=type=bind,source=xrpld,target=/tmp/xrpld \
    install -m 755 /tmp/xrpld /usr/bin/xrpld \
    && mkdir /symbols \
    && ln -s /usr/bin/xrpld /symbols/xrpld

USER xrpld
