#!/bin/bash
# Compile all C++ test binaries during the Docker image build.
# Each binary has the target system's ELF PT_INTERP (dynamic-linker path)
# baked in so it can run on the (potentially minimal) final BASE_IMAGE.

set -eo pipefail

src_dir="${1:?usage: $0 <src_dir> <dst_dir>}"
dst_dir="${2:?usage: $0 <src_dir> <dst_dir>}"

loader="$(/tmp/loader-path.sh)"

mkdir -p "${dst_dir}"

function compile() {
    local compiler="${1}"
    local name="${2}"
    local san_flag="${3:-}"

    local src="${src_dir}/${name}.cpp"
    local binary="${dst_dir}/${name}-${compiler}"

    echo "=== Compiling ${name} with ${compiler} ==="
    # Always statically link libstdc++ so the test binary does not depend on
    # the host's libstdc++.so.6 version.
    local compile_cmd="${compiler} -std=c++23 -O1 -g \
        -pthread \
        -static-libstdc++ \
        ${san_flag} \
        ${src} -o ${binary}"
    echo "Compile cmd: ${compile_cmd}"
    eval "${compile_cmd}"

    echo "=== Patching ${binary} to use ${loader} as PT_INTERP ==="
    local patch_cmd="patchelf --set-interpreter ${loader} --remove-rpath ${binary}"
    echo "Patch cmd: ${patch_cmd}"
    eval "${patch_cmd}"
}

declare -A sanitize=(
    [regular]=""

    [asan]="-fsanitize=address"
    [tsan]="-fsanitize=thread"
    [ubsan]="-fsanitize=undefined -fno-sanitize-recover=all"
)

for name in regular asan tsan ubsan; do
    san_flag="${sanitize[${name}]}"
    for compiler in g++ clang++; do
        compile "${compiler}" "${name}" "${san_flag}"
    done
done

echo "=== All binaries compiled ==="

ls -la "${dst_dir}"
