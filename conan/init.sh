#!/usr/bin/env bash
# Install our Conan configuration, profiles and the xrplf remote into CONAN_HOME.
# Safe to re-run; never deletes the Conan home.

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
CONAN_DIR="$(conan config home)"

echo "Installing Conan configuration into ${CONAN_DIR}"
conan config install "${SCRIPT_DIR}/global.conf"
conan config install "${SCRIPT_DIR}/profiles" -tf "${CONAN_DIR}/profiles"
# Modes are copied along with the files, and the source may be a read-only
# /nix/store path. Keep the installed configuration editable.
chmod -R u+w "${CONAN_DIR}/global.conf" "${CONAN_DIR}/profiles"

echo "Adding the xrplf Conan remote"
# --index 0: our patched recipes must win over Conan Center.
conan remote add --index 0 --force xrplf https://conan.xrplf.org/repository/conan/
