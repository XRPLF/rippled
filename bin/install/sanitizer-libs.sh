#!/bin/bash
# Install sanitizer runtime libraries required to run binaries compiled with:
#   -fsanitize=address   → libasan.so.8
#   -fsanitize=thread    → libtsan.so.2
#   -fsanitize=undefined → libubsan.so.1
#
# The exact SONAMEs required depend on the compiler toolchain used to build the
# test binaries (see nix/ci-env.nix). If the toolchain is bumped and SONAMEs
# change, update the list below (or detect them from the binaries).
#
# Supported base images:
#   debian:bookworm
#   ubuntu:20.04
#   rhel:9
#   nixos/nix        — tests are skipped; this script is not called

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

function preinstall() {
    case "${ID}" in
        ubuntu)
            apt-get update -y
            apt-get install -y --no-install-recommends \
                gnupg \
                software-properties-common
            add-apt-repository -y ppa:ubuntu-toolchain-r/test
            ;;
    esac
}

function install() {
    case "${ID}" in
        debian | ubuntu)
            apt-get update -y
            apt-get install -y --no-install-recommends \
                libasan8 \
                libtsan2 \
                libubsan1
            ;;

        rhel | centos | rocky | almalinux)
            dnf install -y \
                libasan8 \
                libtsan2 \
                libubsan
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

function verify() {
    # Verify that every expected library is now resolvable by the dynamic linker.
    missing=0
    for lib in libasan.so.8 libtsan.so.2 libubsan.so.1; do
        if ldconfig -p | grep -q "${lib}"; then
            echo "OK: ${lib} found"
        else
            echo "ERROR: ${lib} not found after installation" >&2
            missing=$((missing + 1))
        fi
    done

    if [ "${missing}" -ne 0 ]; then
        echo "ERROR: ${missing} library/libraries missing" >&2
        exit 1
    fi
}

preinstall
install
postinstall
verify

echo "All sanitizer runtime libraries installed successfully."
