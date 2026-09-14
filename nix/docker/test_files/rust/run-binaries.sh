#!/bin/bash
# Run pre-compiled Rust binaries and confirm each emits its expected diagnostic.
# Binaries must already exist in <bins_dir> as <name> for name in
# {hello,panic,overflow,proc_macro}.

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
    [hello]="Hello from main thread"
    [panic]="explicit panic from test"
    [overflow]="attempt to add with overflow"
    [proc_macro]="proc-macro answer = 42"
)

for name in hello panic overflow proc_macro; do
    binary="${bins_dir}/${name}"

    if [ "${name}" = "hello" ] || [ "${name}" = "proc_macro" ]; then
        expected_rc=0
    else
        expected_rc=nonzero
    fi
    run "${binary}" "${expect[$name]}" "${expected_rc}"
done

if [ "${#failed_binaries[@]}" -gt 0 ]; then
    echo "ERROR: the following binaries failed:" >&2
    printf '  %s\n' "${failed_binaries[@]}" >&2
    exit 1
fi
