#!/usr/bin/env bash
# Install a locally-built package and run basic verification.

set -euo pipefail
set -x

XRPLD_BIN="${XRPLD_BIN:-/usr/bin/xrpld}"

rm -f /tmp/test_failed /tmp/unittest_results
trap 'status=$?; if [ "${status}" -ne 0 ]; then touch /tmp/test_failed; fi' EXIT

install_from="${1:-}"

. /etc/os-release

case "${ID:-} ${ID_LIKE:-}" in
    *debian*)
        package_format="deb"
        ;;
    *fedora*|*rhel*)
        package_format="rpm"
        ;;
    *)
        echo "Unsupported distro: ID=${ID:-unknown} (ID_LIKE=${ID_LIKE:-})." >&2
        exit 1
        ;;
esac

if [ "${install_from}" != "local" ]; then
    echo "Only 'local' install mode is supported." >&2
    exit 1
fi

if [ "${package_format}" = "deb" ]; then
    apt-get -y update

    mapfile -t debs < <(
        find build/debbuild/ build/dpkg/packages/ \
            -name '*.deb' ! -name '*dbgsym*' 2>/dev/null
    )

    if [ "${#debs[@]}" -eq 0 ]; then
        echo "No .deb files found." >&2
        exit 1
    fi

    if ! apt-get -y install "${debs[@]}"; then
        dpkg --no-debsig -i "${debs[@]}"
        apt-get -y install -f
    fi

elif [ "${package_format}" = "rpm" ]; then
    mapfile -t rpms < <(
        find build/rpmbuild/RPMS/ build/rpm/packages/ \
            -name '*.rpm' \
            ! -name '*debug*' ! -name '*devel*' ! -name '*.src.rpm' \
            2>/dev/null
    )

    if [ "${#rpms[@]}" -eq 0 ]; then
        echo "No .rpm files found." >&2
        exit 1
    fi

    if command -v dnf >/dev/null 2>&1; then
        dnf -y install "${rpms[@]}"
    else
        rpm -i "${rpms[@]}"
    fi
fi

if [ ! -x "${XRPLD_BIN}" ]; then
    echo "xrpld binary not found or not executable: ${XRPLD_BIN}." >&2
    exit 1
fi

if ! version_output=$("${XRPLD_BIN}" --version); then
    echo "xrpld --version failed; binary not installed correctly." >&2
    exit 1
fi

installed=$(awk 'NR == 1 {print $NF}' <<<"${version_output}")
echo "Installed version: ${installed}."

cmd=( "${XRPLD_BIN}" --unittest )

unittest_jobs=$(( $(nproc) / 2 ))
if (( unittest_jobs > 1 )); then
    cmd+=( --unittest-jobs "${unittest_jobs}" )
fi

unittest_status=0
(
    cd /tmp
    "${cmd[@]}" > /tmp/unittest_results
) || unittest_status=$?

if [ ! -s /tmp/unittest_results ]; then
    echo "Unit test results file is empty. The xrpld binary may have crashed." >&2
    exit 1
fi

num_failures=$(
    sed -nE 's/.* ([0-9]+) failures.*/\1/p' /tmp/unittest_results | tail -n1
)

if [ -z "${num_failures}" ]; then
    echo "Could not parse unit test results — expected summary line not found." >&2
    exit 1
fi

if [ "${num_failures}" -ne 0 ]; then
    echo "${num_failures} unit test(s) failed." >&2
    grep 'failed:' /tmp/unittest_results || true
    exit 1
fi

if [ "${unittest_status}" -ne 0 ]; then
    echo "The xrpld unit tests exited with status ${unittest_status}." >&2
    exit "${unittest_status}"
fi

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
"${script_dir}/check_install_paths.sh"
