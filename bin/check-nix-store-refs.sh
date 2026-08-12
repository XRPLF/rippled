#!/usr/bin/env bash
# Fail if a binary under <path> records a /nix/store path it resolves at run
# time. See docs/build/nix.md#prebuilt-packages for why that matters.
#
# <path> is a file or a directory. macOS: nothing may reference the store, so
# point it at whole trees. Linux: the toolchain always writes the store into
# PT_INTERP and RUNPATH, so only at what cmake/PatchNixBinary.cmake retargets.
#
# Only Mach-O / ELF is inspected. Static archives hold store paths in debug info
# alone; the scripts in a Conan cache are all git hook samples and autotools
# scratch, 36 false positives to 0 real.
#
# Usage: bin/check-nix-store-refs.sh <path>

set -euo pipefail

if [ "$#" -ne 1 ]; then
    echo "usage: $0 <path>" >&2
    exit 2
fi

if [ ! -e "$1" ]; then
    echo "$0: no such path: $1" >&2
    exit 2
fi

case "$(uname -s)" in
    Darwin)
        format=Mach-O
        recorded_paths=macho_recorded_paths
        tool=otool
        ;;
    Linux)
        format=ELF
        recorded_paths=elf_recorded_paths
        tool=readelf
        ;;
    *)
        echo "Unsupported OS - skipping the Nix store reference check."
        exit 0
        ;;
esac

# `pipefail` would catch this too, but only as a bare nonzero exit.
if ! command -v "${tool}" >/dev/null; then
    echo "$0: ${tool} not found; cannot inspect binaries" >&2
    exit 2
fi

# Both list what the file records. `ldd` would answer what this machine resolves
# now, which is wrong both ways: store paths for a correctly patched binary,
# silence for a store RUNPATH that resolves nowhere.

# `name` covers LC_ID_DYLIB and LC_LOAD*_DYLIB, `path` covers LC_RPATH.
macho_recorded_paths() {
    otool -l "$1" | sed -nE 's#^ *(name|path) ([^ ]*).*#\2#p'
}

# RPATH and RUNPATH are colon-separated.
elf_recorded_paths() {
    readelf -ldW "$1" |
        sed -nE \
            -e 's#.*program interpreter: ([^]]*)\].*#\1#p' \
            -e 's#.*\((RPATH|RUNPATH|NEEDED)\).*\[([^]]*)\].*#\2#p' |
        tr ':' '\n'
}

checked=0
skipped=0
leaked=0

while IFS= read -r file; do
    case "$(file -b "${file}" 2>/dev/null)" in
        *"${format}"*) ;;
        *)
            skipped=$((skipped + 1))
            continue
            ;;
    esac
    checked=$((checked + 1))

    # Filter after extracting, or a search path starting elsewhere ($ORIGIN)
    # hides the rest. `sed` not `grep`: grep calls "no matches" a failure, and
    # the `|| true` that would need masks a broken pipeline too.
    refs="$("${recorded_paths}" "${file}" | sed -n '\#^/nix/store/#p' | sort -u)"
    if [ -n "${refs}" ]; then
        leaked=$((leaked + 1))
        echo "::error file=${file}::references the Nix store at run time"
        echo "${file}"
        echo "${refs}" | sed 's/^/    /'
    fi
done < <(find "$1" -type f \( -perm -u+x -o -name '*.dylib' -o -name '*.so*' \))

echo "Checked ${checked} files in $1 (${skipped} skipped), ${leaked} of them reference the Nix store."

if [ "${leaked}" -ne 0 ]; then
    cat >&2 <<'EOF'

Fixes, in order of preference:
  - A Conan package built before this check existed: drop it
    (`conan remove '<name>/*'`) and rebuild.
  - A binary that should have been retargeted to the system loader: check that
    cmake/PatchNixBinary.cmake ran for it.
  - Link the macOS system library instead of the Nix one - see
    libresolvSystemStub in nix/darwin.nix.
  - No system library exists (libstdc++): link it statically.
  - None of the above: pin the toolchain into the package ID, following
    `user.package:libc_version` in conan/profiles/ci.
EOF
    exit 1
fi
