#!/bin/bash
# Compile all Rust test binaries during the Docker image build.
# Each binary has the target system's ELF PT_INTERP (dynamic-linker path)
# baked in so it can run on the (potentially minimal) final BASE_IMAGE.

set -eo pipefail

src_dir="${1:?usage: $0 <src_dir> <dst_dir>}"
dst_dir="${2:?usage: $0 <src_dir> <dst_dir>}"

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

loader="$(/tmp/loader-path.sh)"

mkdir -p "${dst_dir}"

function compile() {
    local name="${1}"
    local extra_flags="${2:-}"

    local src="${src_dir}/${name}.rs"
    local binary="${dst_dir}/${name}"

    echo "=== Compiling ${name} with rustc ==="
    # -O optimizes (opt-level 2); Rust statically links its own std, so the
    # only dynamic dependency left is the system glibc (+ libgcc_s), exactly
    # like the C++ binaries.
    local compile_cmd="rustc --edition 2021 -O -g ${extra_flags} \
        ${src} -o ${binary}"
    echo "Compile cmd: ${compile_cmd}"
    eval "${compile_cmd}"

    echo "=== Patching ${binary} to use ${loader} as PT_INTERP ==="
    local patch_cmd="patchelf --set-interpreter ${loader} --remove-rpath ${binary}"
    echo "Patch cmd: ${patch_cmd}"
    eval "${patch_cmd}"
}

# `-O` disables overflow checks by default, so `overflow` re-enables them
# explicitly to exercise the runtime overflow check.
compile hello
compile panic
compile overflow "-C overflow-checks=on"

# Build a Cargo workspace whose binary crate (`answer_user`) uses a function-like
# proc macro defined by a sibling proc-macro crate (`answer_macro`). Compiling
# the user crate forces rustc to *load* the proc-macro dylib at build time —
# exactly the step that regresses to "E0463 can't find crate for <proc-macro>"
# when the toolchain's libstd isn't resolvable for proc-macro dylibs. The plain
# `rustc` sources above only build executables (static std), so they never
# exercise this path; the wasmi C-API build does, which is why it broke.
# We mirror wasmi's invocation shape: an explicit `--target` (so cargo splits
# host vs. target artifacts, and the proc-macro is built as a host tool) and
# `--profile bench`. Only path deps + the built-in `proc_macro` crate, so
# `--offline` needs no network.
function compile_proc_macro() {
    local proj="${script_dir}/proc_macro"
    local host_triple
    host_triple="$(rustc -vV | sed -n 's/^host: //p')"
    local target_dir
    target_dir="$(mktemp -d)"

    echo "=== Building proc-macro workspace (cargo, target=${host_triple}, profile=bench) ==="
    cargo build \
        --manifest-path "${proj}/Cargo.toml" \
        --target "${host_triple}" \
        --profile bench \
        --offline \
        --target-dir "${target_dir}"

    # The built-in `bench` profile shares the `release/` output directory.
    local built="${target_dir}/${host_triple}/release/answer_user"
    local binary="${dst_dir}/proc_macro"
    cp "${built}" "${binary}"

    echo "=== Patching ${binary} to use ${loader} as PT_INTERP ==="
    patchelf --set-interpreter "${loader}" --remove-rpath "${binary}"
}

compile_proc_macro

echo "=== All binaries compiled ==="

ls -la "${dst_dir}"
