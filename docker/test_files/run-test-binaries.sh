#!/bin/bash
# Run pre-compiled sanitizer binaries and confirm each emits its expected diagnostic.
# Binaries must already exist in <bins_dir> with the layout:
# <name>-g++ and <name>-clang++ for name in {regular,asan,tsan,ubsan}

set -eo pipefail

bins_dir="${1:?usage: $0 <bins_dir>}"

# Run a binary and verify its exit code and output.
# Usage: run <binary> <expected_output> <expected_rc>
function run() {
    local binary="${1}"
    local expected_output="${2}"
    local expected_rc="${3}"

    local out_file
    out_file="$(mktemp)"

    echo "=== Run ${binary} ==="
    local rc=0
    "${binary}" >"${out_file}" 2>&1 || rc=$?

    cat "${out_file}"

    if [ "${expected_rc}" = "nonzero" ]; then
        if [ "${rc}" -eq 0 ]; then
            echo "ERROR: expected non-zero exit code from ${binary}, got ${rc}" >&2
            exit 1
        fi
    elif [ "${rc}" -ne "${expected_rc}" ]; then
        echo "ERROR: expected exit code ${expected_rc} from ${binary}, got ${rc}" >&2
        exit 1
    fi

    grep -q "${expected_output}" "${out_file}" ||
        {
            echo "ERROR: expected '${expected_output}' from ${binary}" >&2
            exit 1
        }
    echo "OK: '${expected_output}' detected"
}

declare -A expect=(
    [regular]="Hello from main thread"

    [asan]="heap-use-after-free"
    [tsan]="data race"
    [ubsan]="signed integer overflow"
)

for compiler in g++ clang++; do
    for name in regular asan tsan ubsan; do
        binary="${bins_dir}/${name}-${compiler}"
        if [ "${name}" = "regular" ]; then
            expected_rc=0
        else
            expected_rc=nonzero
        fi
        run "${binary}" "${expect[$name]}" "${expected_rc}"
    done
done
