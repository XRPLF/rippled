#!/usr/bin/env sh
# Build libXRPLModel.{dylib,so} (the model + FFI + mathlib's objects) in one
# shared lib for the cross-validation tests. Run by the formal_verification CMake
# target, which sets LEAN_BINDIR, LEAN_LIBDIR, GMP_LIBDIR.
set -eu
cd "$(dirname "$0")/.."

lake="${LEAN_BINDIR}/lake"
cc="${LEAN_BINDIR}/clang"
libdir=.lake/build/lib
mkdir -p "${libdir}"

dep_targets="ProofWidgets:static ImportGraph:static LeanSearchClient:static \
Plausible:static Aesop:static Qq:static Cli:static Batteries:static Mathlib:static"

case "$(uname)" in
    Darwin) suffix=dylib ;;
    *) suffix=so ;;
esac
lib="${libdir}/libXRPLModel.${suffix}"
archive="${libdir}/libXRPL_XRPLModel.a"

if [ "$(find .lake/packages -name '*.c.o.export' ! -path '*/ir/Cache/*' | wc -l | tr -d ' ')" -lt 7000 ]; then
    "${lake}" build ${dep_targets} >/dev/null 2>&1 || true
fi
"${lake}" build XRPLModel:static

if [ -e "${lib}" ] && [ ! "${archive}" -nt "${lib}" ]; then
    exit 0
fi

raw=$(mktemp)
rsp=$(mktemp)
trap 'rm -f "${raw}" "${rsp}"' EXIT
find .lake/packages -name '*.c.o.export' ! -path '*/ir/Cache/*' >"${raw}"
objects=$(wc -l <"${raw}" | tr -d " ")
if [ "${objects}" -lt 7000 ]; then
    echo "build_lean.sh: only ${objects} dependency objects compiled" >&2
    exit 1
fi
echo "build_lean.sh: linking libXRPLModel.${suffix} (model + ${objects} objects)"
if [ "${suffix}" = dylib ]; then
    "${cc}" -dynamiclib -isysroot "${SDKROOT:-$(xcrun --show-sdk-path)}" \
        -install_name "@rpath/libXRPLModel.${suffix}" -o "${lib}" \
        -Wl,-force_load,"${archive}" -filelist "${raw}" \
        -L"${LEAN_LIBDIR}" -lleanshared -lLake_shared -L"${GMP_LIBDIR}" -lgmp
else
    sed 's/.*/"&"/' "${raw}" >"${rsp}"
    "${cc}" -shared -o "${lib}" \
        -Wl,--whole-archive "${archive}" -Wl,--no-whole-archive "@${rsp}" \
        -Wl,-soname,"libXRPLModel.${suffix}" -Wl,-rpath,"${LEAN_LIBDIR}" \
        -L"${LEAN_LIBDIR}" -lleanshared -lLake_shared -L"${GMP_LIBDIR}" -lgmp
fi
