#!/usr/bin/env bash

set -ex

TEMP_DIR=$(mktemp -d)
trap "rm -rf $TEMP_DIR" EXIT

echo "Using temporary CONAN_HOME: $TEMP_DIR"

# We use a temporary Conan home to avoid polluting the user's existing Conan
# configuration and to not use local cache (which leads to non-reproducible lockfiles).
export CONAN_HOME="$TEMP_DIR"

# Ensure that the xrplf remote is the first to be consulted, so any recipes we
# patched are used. We also add it there to not created huge diff when the
# official Conan Center Index is updated.
conan remote add --force --index 0 xrplf https://conan.ripplex.io

# Delete any existing lockfile.
rm -f conan.lock

# Create a new lockfile that is compatible with Linux, macOS, and Windows. The
# first create command will create a new lockfile, while the subsequent create
# commands will merge any additional dependencies into the created lockfile.
conan lock create . \
  --options '&:jemalloc=True' \
  --options '&:rocksdb=True' \
  --profile:all=conan/lockfile/linux.profile
conan lock create . \
  --options '&:jemalloc=True' \
  --options '&:rocksdb=True' \
  --profile:all=conan/lockfile/macos.profile
conan lock create . \
  --options '&:jemalloc=True' \
  --options '&:rocksdb=True' \
  --profile:all=conan/lockfile/windows.profile
