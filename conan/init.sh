#!/usr/bin/env bash
# Install our Conan configuration, profiles and the xrplf remote into CONAN_HOME.
# Safe to re-run; never deletes the Conan home.

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
CONAN_DIR="$(conan config home)"

echo "Installing Conan configuration into ${CONAN_DIR}"
conan config install "${SCRIPT_DIR}/global.conf"
conan config install "${SCRIPT_DIR}/profiles" -tf "${CONAN_DIR}/profiles"
# This script manages these files, so make them read-only - Conan does not
# preserve the source mode. Only the files: the directories must stay writable
# for `conan config install` to replace them.
chmod a-w "${CONAN_DIR}/global.conf"
find "${CONAN_DIR}/profiles" -type f -exec chmod a-w {} +

echo "Adding the xrplf Conan remote"
# --index 0: our patched recipes must win over Conan Center.
conan remote add --index 0 --force xrplf https://conan.xrplf.org/repository/conan/
