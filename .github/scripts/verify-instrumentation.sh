#!/usr/bin/env bash

# Exit the script as soon as an error occurs.
set -euo pipefail

# This script fails if the build in the current directory lacks the
# instrumentation that its configuration enables. It reads the environment
# variables VOIDSTAR_ENABLED, SANITIZERS_ENABLED, ASAN_ENABLED, TSAN_ENABLED and
# UBSAN_ENABLED, each 'true' or 'false', and inspects ./xrpld and the *.o files
# under the current directory.
# An undefined reference to the sanitizer runtime in an object file proves the
# -fsanitize flags reached the compiler. The version string cannot, as cmake
# sets the SANITIZERS macro separately. Nor can the linked binary, as a static
# runtime defines the symbols whether or not any code calls them.
# Usage: run this script from the build directory, without arguments.

version="$(./xrpld --version 2>&1)" || {
    echo "${version}"
    exit 1
}
echo "${version}"
status=0

# The arguments are the name of the enabled feature and the pattern that the
# version string must match.
check_version_names() {
    if grep -q "${2}" <<<"${version}"; then
        echo "${1}: the version string names '${2}'."
    else
        echo "${1} is enabled, but the version string does not name '${2}'."
        status=1
    fi
}

# The arguments are the name of the enabled sanitizer and the extended regular
# expression that an undefined symbol of an object file must match.
# The grep stops at the first match and find then dies of SIGPIPE, which
# pipefail must not turn into a failure.
check_objects_reference() {
    if { find . -name '*.o' -exec nm -u {} + || true; } | grep -qE "${2}"; then
        echo "${1}: the objects reference the runtime, matching '${2}'."
    else
        echo "${1} is enabled, but no object references '${2}'."
        status=1
    fi
}

if [ "${SANITIZERS_ENABLED}" = 'true' ]; then
    if [ "${ASAN_ENABLED}" = 'true' ]; then
        check_objects_reference address '__asan_report|__asan_memcpy|__asan_stack_malloc'
    fi
    if [ "${TSAN_ENABLED}" = 'true' ]; then
        check_objects_reference thread '__tsan_read|__tsan_write|__tsan_atomic'
    fi
    if [ "${UBSAN_ENABLED}" = 'true' ]; then
        check_objects_reference undefinedbehavior '__ubsan_handle'
    fi
fi

if [ "${VOIDSTAR_ENABLED}" = 'true' ]; then
    check_version_names voidstar libvoidstar
fi
if [ "${ASAN_ENABLED}" = 'true' ]; then
    check_version_names address ASAN
fi
if [ "${TSAN_ENABLED}" = 'true' ]; then
    check_version_names thread TSAN
fi
if [ "${UBSAN_ENABLED}" = 'true' ]; then
    check_version_names undefinedbehavior UBSAN
fi

exit "${status}"
