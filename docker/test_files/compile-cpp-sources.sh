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

    echo "=== Compile ${name} with ${compiler} ==="
    cmd="${compiler} -std=c++23 -O1 -g \
        -pthread \
        -Wl,--dynamic-linker=${loader} \
        ${san_flag} \
        ${src} -o ${binary}"
    echo "Command: ${cmd}"
    eval "${cmd}"
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
