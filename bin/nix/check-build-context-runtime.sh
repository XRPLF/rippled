#!/usr/bin/env bash
# Fail if a binary of a build-context Conan package loads anything from the Nix
# store other than glibc, or cannot resolve a library at all.
#
# Only binaries linked by the Nix toolchain are checked, i.e. those recording a
# store path as their interpreter or RUNPATH. Prebuilt upstream binaries (such
# as the ones the cmake package ships) use the system loader instead.
#
# Build-context packages provide the tools that run during the build (protoc,
# grpc_cpp_plugin, ...). Their package ID does not change when a Nix toolchain
# update moves the GCC runtime to a new store path, so a cached binary has to
# get by with the pinned glibc alone. See docs/build/nix.md.
#
# Usage: bin/nix/check-build-context-runtime.sh <graph.json>
#   <graph.json> is the output of `conan install --format=json`.

set -euo pipefail

if [ "$#" -ne 1 ]; then
    echo "usage: $0 <graph.json>" >&2
    exit 2
fi

if [ "$(uname -s)" != "Linux" ]; then
    echo "$0: Linux only" >&2
    exit 2
fi

folders="$(jq -r '.graph.nodes[] | select(.context == "build" and .package_folder) | .package_folder' "$1" | sort -u)"

checked=0
failed=0

while IFS= read -r file; do
    case "$(file -b "${file}")" in
        ELF*) ;;
        *) continue ;;
    esac
    [[ "$(readelf -ldW "${file}")" == */nix/store/* ]] || continue
    checked=$((checked + 1))

    # `ldd` lists the interpreter and every library as the loader resolves them.
    if deps="$(ldd "${file}" 2>&1)"; then
        bad="$(printf '%s\n' "${deps}" |
            grep -E 'not found|/nix/store/' |
            grep -vE '/nix/store/[^/]+-glibc-[^/]+/' || true)"
    else
        case "${deps}" in
            *"not a dynamic executable"*) continue ;;
        esac
        bad="${deps}"
    fi
    if [ -n "${bad}" ]; then
        failed=$((failed + 1))
        echo "::error file=${file}::loads a library from the Nix store other than glibc"
        echo "${file}"
        echo "${bad}" | sed 's/^/    /'
    fi
done < <(
    # shellcheck disable=SC2086 # one folder per line, no spaces in Conan paths
    [ -z "${folders}" ] || find ${folders} -type f \( -perm -u+x -o -name '*.so*' \)
)

echo "Build-context packages: checked ${checked} binaries, ${failed} failed."
[ "${failed}" -eq 0 ]
