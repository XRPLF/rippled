#!/usr/bin/env bash
# Rebuild a packaged config's binaries from source, the way an outside party
# checks a release: a fresh copy of this checkout's commit at a new path, a new
# Conan home, every dependency built from source, and no CI environment.
#
# Run it inside the config's build image (build_image in the packaging matrix):
#   bin/rebuild-release.sh <config_name> <work_dir>
# It writes xrpld, validator-keys and validator-keys-LICENSE to <work_dir>/out,
# which must match the binaries CI built for the same commit byte for byte.

set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: $0 <config_name> <work_dir>" >&2
    exit 2
fi
CONFIG_NAME="$1"
WORK_DIR="$(realpath -m "$2")"
REPO_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"

# The config's compiler and CMake arguments, from the matrix CI builds from.
read -r COMPILER CMAKE_ARGS < <(
    python3 "${REPO_DIR}/.github/scripts/strategy-matrix/generate.py" --packaging |
        sed -n 's/^matrix=//p' |
        python3 -c '
import json, sys
name = sys.argv[1]
entries = [e for e in json.load(sys.stdin)["include"] if e["config_name"] == name]
if not entries:
    sys.exit(f"no packaged config named {name}")
print(entries[0]["compiler"], entries[0]["cmake_args"])
' "${CONFIG_NAME}"
)
if [[ -z "${COMPILER}" ]]; then
    echo "no packaged config named ${CONFIG_NAME}" >&2
    exit 2
fi

SOURCE_DIR="${WORK_DIR}/source/xrpld"
BUILD_DIR="${SOURCE_DIR}/build"
OUT_DIR="${WORK_DIR}/out"
mkdir -p "${WORK_DIR}" "${OUT_DIR}"

# The same commit at a new path; a checkout on a branch keeps the branch name,
# and a detached tag checkout gets the tag, so the embedded branch matches CI.
git clone --quiet --no-hardlinks "${REPO_DIR}" "${SOURCE_DIR}"
COMMIT="$(git -C "${REPO_DIR}" rev-parse HEAD)"
if BRANCH="$(git -C "${REPO_DIR}" symbolic-ref --quiet --short HEAD)"; then
    git -C "${SOURCE_DIR}" checkout --quiet -B "${BRANCH}" "${COMMIT}"
else
    git -C "${SOURCE_DIR}" checkout --quiet --detach "${COMMIT}"
fi

# Nothing from a CI run may reach the build. The compiler is named the way
# CI's set-compiler-env names it, so Conan detects it the same way.
unset CI GITHUB_BRANCH_NAME GITHUB_HEAD_SHA FORCE_XRPLD_VERSION
case "${COMPILER}" in
    gcc) export CC=gcc CXX=g++ ;;
    clang) export CC=clang CXX=clang++ ;;
    *)
        echo "unknown compiler ${COMPILER}" >&2
        exit 2
        ;;
esac
export CONAN_HOME="${WORK_DIR}/conan"
export CCACHE_DISABLE=1

cd "${SOURCE_DIR}"
./conan/init.sh
conan install \
    --profile:all ci \
    --build='*' \
    --options:host='&:tests=True' \
    --options:host='&:xrpld=True' \
    --settings:all build_type=Release \
    --conf:all tools.build:jobs="$(nproc)" \
    .

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"
read -r -a CMAKE_ARGV <<<"${CMAKE_ARGS}"
cmake \
    -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE:FILEPATH=build/generators/conan_toolchain.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    "${CMAKE_ARGV[@]}" \
    ..
cmake --build . --config Release --parallel "$(nproc)" --target all

cp xrpld validator-keys validator-keys-LICENSE "${OUT_DIR}/"
sha256sum "${OUT_DIR}"/*
