#!/usr/bin/env bash
#
# check-tools.sh — verify the xrpld development tooling is present and runnable.
#
# Works on Linux, macOS, and Windows (Git Bash / MSYS). For every expected tool
# it runs a version probe, collecting anything that is missing or fails to run,
# and prints a summary at the end (exiting non-zero if anything is missing).
#
# The tool set is platform-aware:
#   - Linux:   the full Nix CI environment (see nix/packages.nix, nix/ci-env.nix),
#              with GCC, Clang and the sanitizer/coverage tooling. This script is
#              run during the Nix Docker image build (nix/docker/Dockerfile), so
#              the Linux list is kept in sync with that environment.
#   - macOS:   the same tooling, minus GCC/g++/gcov/mold
#   - Windows: the core build tools only (CMake, Conan, Git, Python).
#              MSVC is expected to be provided separately and is not checked here.
#
# Some tools (clang-format, doxygen, gcovr, gh, git-cliff, gpg, pre-commit,
# run-clang-tidy) are present in our Linux CI images and in local development
# setups, but not in the macOS CI environment. They are checked everywhere
# except when running in CI on macOS.
#
# Environment variables:
#   CI                      if set, skip the tools above when on macOS.
#   CHECK_TOOLS_SKIP_CLONE  if set, skip the git-over-HTTPS connectivity check.

set -uo pipefail

missing=()
checked=0

# check <name> [probe-command...]
# Runs the probe (default: "<name> --version") quietly. Records <name> as
# missing if the command is not found or exits non-zero.
check() {
    local name="$1"
    shift
    local -a probe=("$@")
    if [ "${#probe[@]}" -eq 0 ]; then
        probe=("${name}" --version)
    fi

    echo "Checking ${name}..."
    checked=$((checked + 1))
    if "${probe[@]}" | head -n 1; then
        printf '  [ ok ] %s\n' "${name}"
    else
        printf '  [MISS] %s\n' "${name}"
        missing+=("${name}")
    fi
}

case "$(uname -s)" in
    Linux*) os=linux ;;
    Darwin*) os=macos ;;
    MINGW* | MSYS* | CYGWIN*) os=windows ;;
    *)
        echo "Unknown OS: $(uname -s)" >&2
        exit 1
        ;;
esac

echo "Detected OS: ${os} ($(uname -s) $(uname -m))"
echo
echo "Core build tools:"
check cmake
check conan
check git
if [ "${os}" = "windows" ]; then
    check python python --version
else
    check python3
fi

# The full development toolchain. Available from Nix on Linux and macOS; on
# Windows these are typically not installed, so they are skipped.
if [ "${os}" = "linux" ] || [ "${os}" = "macos" ]; then
    echo
    echo "Development tooling:"
    check ccache
    check clang
    check clang++
    check ClangBuildAnalyzer
    check curl
    check file
    check less
    check make
    check netstat which netstat
    check ninja
    check perl
    check pkg-config
    check vim

    # These tools are present in our Linux CI images and in local development
    # setups, but not in the macOS CI environment. So check them everywhere
    # except when running in CI on macOS.
    if [ "${os}" = "linux" ] || [ -z "${CI:-}" ]; then
        check clang-format
        check doxygen
        check gcovr
        check gh
        check git-cliff
        check gpg
        # pre-commit, or its alternative implementation prek
        check pre-commit sh -c 'pre-commit --version || prek --version'
        check run-clang-tidy run-clang-tidy --help
    fi
fi

# GCC is the default compiler on Linux. macOS uses the system Apple Clang
# instead, so GCC/g++/gcov are not expected there.
if [ "${os}" = "linux" ]; then
    echo
    echo "GCC toolchain:"
    check gcc
    check g++
    check gcov

    echo
    echo "Mold:"
    check mold
fi

if [ "${os}" = "windows" ]; then
    echo
    echo "Note: on Windows the C++ compiler is MSVC, which is provided"
    echo "      separately (e.g. via Visual Studio) and is not checked here."
fi

# A simple test to verify that git can clone a repository over HTTPS
# (i.e. the CA bundle is wired up). Clone to a temp dir and clean up.
if [ -n "${CHECK_TOOLS_SKIP_CLONE:-}" ]; then
    echo
    echo "Skipping git-over-HTTPS check (CHECK_TOOLS_SKIP_CLONE is set)."
else
    echo
    echo "Connectivity check:"
    checked=$((checked + 1))
    tmp_clone="$(mktemp -d)"
    if git clone --depth 1 https://github.com/XRPLF/actions.git "${tmp_clone}/actions" >/dev/null 2>&1; then
        printf '  [ ok ] git clone over HTTPS\n'
    else
        printf '  [MISS] git clone over HTTPS\n'
        missing+=("git-https-clone")
    fi
    rm -rf "${tmp_clone}"
fi

echo
if [ "${#missing[@]}" -eq 0 ]; then
    echo "All ${checked} checked tools are present and runnable."
else
    echo "Missing or non-functional tools (${#missing[@]} of ${checked}):" >&2
    for tool in "${missing[@]}"; do
        echo "  - ${tool}" >&2
    done
    exit 1
fi
