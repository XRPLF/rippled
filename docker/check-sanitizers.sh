#!/bin/bash

# Sanity-check that the sanitizer runtimes shipped with g++/clang++ work
# end-to-end against the system loader: compile each example with both
# compilers, run it, and confirm the expected diagnostic is emitted.

set -eo pipefail

cpp_files_dir="${1:?usage: $0 <cpp_files_dir>}"

case "$(uname -m)" in
    x86_64) loader=/lib64/ld-linux-x86-64.so.2 ;;
    aarch64) loader=/lib/ld-linux-aarch64.so.1 ;;
    *)
        echo "Unsupported arch: $(uname -m)" >&2
        exit 1
        ;;
esac

declare -A sanitize=(
    [asan]="-fsanitize=address"
    [tsan]="-fsanitize=thread"
    [ubsan]="-fsanitize=undefined"
)
declare -A expect=(
    [asan]="heap-use-after-free"
    [tsan]="data race"
    [ubsan]="signed integer overflow"
)

for compiler in g++ clang++; do
    for name in asan tsan ubsan; do
        bin="/tmp/${name}-${compiler}"
        echo "=== Build ${name} with ${compiler} ==="
        "$compiler" -std=c++20 -O1 -g ${sanitize[$name]} \
            -Wl,--dynamic-linker=$loader \
            "${cpp_files_dir}/${name}.cpp" -o "$bin"
        echo "=== Run ${name}-${compiler} ==="
        output=$("$bin" 2>&1) || true
        echo "$output"
        echo "$output" | grep -q "${expect[$name]}" ||
            {
                echo "expected '${expect[$name]}' from $bin"
                exit 1
            }
        rm -f "$bin"
    done
done
