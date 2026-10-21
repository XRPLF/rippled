#!/bin/bash

set -euo pipefail

if [ ! -f /etc/os-release ]; then
    echo "ERROR: /etc/os-release not found; cannot detect OS" >&2
    exit 1
fi

# shellcheck source=/dev/null
. /etc/os-release

echo "Detected OS: ${ID} ${VERSION_ID:-}"

case "${ID}" in
    ubuntu | debian | rhel | centos | rocky | almalinux)
        echo "Supported OS detected: ${ID}"
        ;;
    *)
        echo "ERROR: unsupported OS '${ID}'. Supported: debian, ubuntu, rhel-family" >&2
        exit 1
        ;;
esac

# Packaging runs in a vanilla distro image, so the tooling comes from the distro's
# archive rather than from nixpkgs:
#
#   - debhelper and dpkg-dev build the DEB, and lintian checks it
#   - binutils gives debian/rules the readelf its glibc-floor check runs; it
#     already arrives via dpkg-dev, but that tool is called directly
#   - rpm-build builds the RPM, with systemd-rpm-macros and redhat-rpm-config
#     supplying the systemd and find-debuginfo macros the spec uses
#   - rpm-sign and gnupg2 sign the built RPM
#   - python3 runs the packaging scripts
#   - git gives build_pkg.py the commit timestamp it stamps files with
#   - ca-certificates lets git and the packaging scripts verify TLS
function install() {
    case "${ID}" in
        debian | ubuntu)
            apt-get update -y
            apt-get install -y --no-install-recommends \
                binutils \
                ca-certificates \
                debhelper \
                debhelper-compat \
                dpkg-dev \
                git \
                lintian \
                python3
            ;;

        rhel | centos | rocky | almalinux)
            dnf install -y --setopt=install_weak_deps=False \
                git \
                gnupg2 \
                python3 \
                redhat-rpm-config \
                rpm-build \
                rpm-sign \
                systemd-rpm-macros
            ;;
    esac
}

# Distrusts the roots of the compromised eMudhra intermediates until every
# distro ships a fixed ca-certificates; see https://emudhra.we-have-a-problem.com/.
function distrust_compromised_roots() {
    case "${ID}" in
        debian | ubuntu)
            sed -i \
                -e 's|^mozilla/emSign_ECC_Root_CA_-_G3.crt|!&|' \
                -e 's|^mozilla/Comodo_AAA_Services_root.crt|!&|' \
                /etc/ca-certificates.conf
            update-ca-certificates
            ;;

        rhel | centos | rocky | almalinux)
            local blocklist=/etc/pki/ca-trust/source/blocklist
            local root
            for root in "Comodo AAA Services root" "emSign ECC Root CA - G3"; do
                trust extract --overwrite --format=pem-bundle \
                    --filter="pkcs11:object=${root// /%20};type=cert" \
                    "${blocklist}/${root// /_}.pem"
            done
            update-ca-trust
            ;;
    esac
}

function postinstall() {
    # Don't clear cache in non-CI environments
    if [ -z "${CI:-}" ]; then
        echo "Not running in CI environment; skipping cache cleanup"
        return
    fi

    case "${ID}" in
        debian | ubuntu)
            apt-get clean
            rm -rf /var/lib/apt/lists/*
            ;;

        rhel | centos | rocky | almalinux)
            dnf clean -y all
            rm -rf /var/cache/dnf/*
            ;;
    esac
}

install
distrust_compromised_roots
postinstall
