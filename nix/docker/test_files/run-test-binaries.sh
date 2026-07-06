#!/bin/bash
# Run pre-compiled sanitizer binaries and confirm each emits its expected diagnostic.
# Binaries must already exist in <bins_dir> with the layout:
# <name>-g++ and <name>-clang++ for name in {regular,asan,tsan,ubsan}

set -eo pipefail

bins_dir="${1:?usage: $0 <bins_dir>}"

failed_binaries=()

# Run a binary and verify its exit code and output.
# Usage: run <binary> <expected_output> <expected_rc>
function run() {
    local binary="${1}"
    local expected_output="${2}"
    local expected_rc="${3}"

    local out_file
    out_file="$(mktemp)"

    echo "=== Run ${binary} ==="
    set +e
    "${binary}" >"${out_file}" 2>&1
    local rc=$?
    set -e

    cat "${out_file}"

    local failed=0
    if [ "${expected_rc}" = "nonzero" ]; then
        if [ "${rc}" -eq 0 ]; then
            echo "ERROR: expected non-zero exit code from ${binary}, got ${rc}" >&2
            failed=1
        fi
    elif [ "${rc}" -ne "${expected_rc}" ]; then
        echo "ERROR: expected exit code ${expected_rc} from ${binary}, got ${rc}" >&2
        failed=1
    fi

    if ! grep -q "${expected_output}" "${out_file}"; then
        echo "ERROR: expected '${expected_output}' from ${binary}" >&2
        failed=1
    fi

    if [ "${failed}" -eq 0 ]; then
        echo "OK: '${expected_output}' detected"
    else
        failed_binaries+=("${binary}")
    fi
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

        if [ "${name}" = "tsan" ] && [ "${compiler}" = "g++" ] &&
            grep -qi 'debian' /etc/os-release 2>/dev/null &&
            [ "$(uname -m)" = "aarch64" ]; then
            echo "=== Skipping ${binary} (tsan-g++ unsupported on Debian ARM64) ==="
            echo "    NOTE: to enable it, add --security-opt seccomp=unconfined to your docker run command"
            continue
        fi

        if [ "${name}" = "regular" ]; then
            expected_rc=0
        else
            expected_rc=nonzero
        fi
        run "${binary}" "${expect[$name]}" "${expected_rc}"
    done
done

if [ "${#failed_binaries[@]}" -gt 0 ]; then
    echo "ERROR: the following binaries failed:" >&2
    printf '  %s\n' "${failed_binaries[@]}" >&2
    exit 1
fi
