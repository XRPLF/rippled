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

function install() {
    case "${ID}" in
        debian | ubuntu)
            apt-get update -y
            apt-get install -y --no-install-recommends \
                ca-certificates \
                debhelper \
                debhelper-compat \
                dpkg-dev \
                git
            ;;

        rhel | centos | rocky | almalinux)
            dnf install -y --setopt=install_weak_deps=False \
                git \
                rpm-build \
                redhat-rpm-config \
                systemd-rpm-macros
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
postinstall
