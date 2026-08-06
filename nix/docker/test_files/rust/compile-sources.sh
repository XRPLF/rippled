#!/bin/bash
# Compile all Rust test binaries during the Docker image build.
# Each binary has the target system's ELF PT_INTERP (dynamic-linker path)
# baked in so it can run on the (potentially minimal) final BASE_IMAGE.

set -eo pipefail

src_dir="${1:?usage: $0 <src_dir> <dst_dir>}"
dst_dir="${2:?usage: $0 <src_dir> <dst_dir>}"

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

function compile_proc_macro() {
    local proj="${src_dir}/../proc_macro"

    echo "=== Building proc-macro workspace (cargo) ==="
    cargo build --manifest-path "${proj}/Cargo.toml" --offline

    local built="${proj}/target/debug/test_macro"
    if [ ! -f "${built}" ]; then
        echo "ERROR: built test_macro binary not found at ${built}" >&2
        exit 1
    fi

    local binary="${dst_dir}/proc_macro"
    cp "${built}" "${binary}"

    echo "=== Patching ${binary} to use ${loader} as PT_INTERP ==="
    patchelf --set-interpreter "${loader}" --remove-rpath "${binary}"

    rm -rf "${proj}/target"
}

compile_proc_macro

echo "=== All binaries compiled ==="

ls -la "${dst_dir}"
