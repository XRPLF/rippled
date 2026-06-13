#!/usr/bin/env sh
# Build .lake/build/lib/libLeanDeps.a: one archive of the native objects of
# every Lean dependency (mathlib + transitive deps), for linking into xrpld.
#
# `lake exe cache get` ships no native objects, so we compile them from each
# dependency's Lake `:static` facet, then archive them from a file list (the
# object count would overflow ARG_MAX, and mathlib has a module name with an
# apostrophe that breaks ar's other input modes).
#
# Keyed on the pinned dependency set, so built once and skipped while it
# exists; delete it after a mathlib bump to rebuild. The target list must
# mirror lake-manifest.json, or xrpld fails to link on undefined
# initialize_<pkg>_* symbols.
set -eu
cd "$(dirname "$0")/.."

out=.lake/build/lib/libLeanDeps.a
if [ -e "${out}" ]; then
    exit 0
fi

dep_targets="ProofWidgets:static ImportGraph:static LeanSearchClient:static \
Plausible:static Aesop:static Qq:static Cli:static Batteries:static"

# shellcheck disable=SC2086 # word splitting of the target list is intended
lake build ${dep_targets}
# Its final ar step may fail on macOS (see above); the object compilation still
# completes, and the count check below catches a genuinely short build.
lake build Mathlib:static || true

filelist=$(mktemp)
trap 'rm -f "${filelist}"' EXIT
find .lake/packages -name '*.c.o.export' >"${filelist}"
objects=$(wc -l <"${filelist}" | tr -d " ")
if [ "${objects}" -lt 7000 ]; then
    echo "bundle_lean_deps.sh: dependency native objects incomplete" \
        "(${objects} found); see the lake build output above" >&2
    exit 1
fi
echo "bundle_lean_deps.sh: bundling ${objects} objects"

mkdir -p "$(dirname "${out}")"
if [ "$(uname)" = "Darwin" ]; then
    libtool -static -o "${out}" -filelist "${filelist}"
else
    # GNU ar here supports neither MRI apostrophe names nor @file. Append in
    # xargs-sized batches, newline-delimited so apostrophes and spaces in names
    # stay literal, then build the symbol index once.
    xargs -d '\n' -a "${filelist}" ar -qc "${out}"
    ar -s "${out}"
fi
echo "bundle_lean_deps.sh: created ${out}"
