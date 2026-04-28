#!/usr/bin/env bash
# Install a locally-built package and run basic verification.
#
# Usage: smoketest.sh local
#   Expects packages in build/{dpkg,rpm}/packages/ or build/debbuild/ / build/rpmbuild/RPMS/

set -o pipefail
set -x
rm -f /tmp/test_failed /tmp/unittest_results
trap 'test $? -ne 0 && touch /tmp/test_failed' EXIT

install_from=$1

. /etc/os-release
case ${ID} in
    ubuntu|debian)
        pkgtype="dpkg"
        ;;
    fedora|centos|rhel|rocky|almalinux)
        pkgtype="rpm"
        ;;
    *)
        echo "unrecognized distro!"
        exit 1
        ;;
esac

if [ "${install_from}" != "local" ]; then
    echo "only 'local' install mode is supported"
    exit 1
fi

# Install the package
if [ "${pkgtype}" = "dpkg" ] ; then
    apt-get -y update
    # Find .deb files — check both possible output locations
    mapfile -t debs < <(find build/debbuild/ build/dpkg/packages/ -name '*.deb' ! -name '*dbgsym*' 2>/dev/null)
    if [ ${#debs[@]} -eq 0 ]; then
        echo "No .deb files found"
        exit 1
    fi
    dpkg --no-debsig -i "${debs[@]}" || apt-get -y install -f || { echo "DEB install failed"; exit 1; }
elif [ "${pkgtype}" = "rpm" ] ; then
    # Find .rpm files — check both possible output locations
    mapfile -t rpms < <(find build/rpmbuild/RPMS/ build/rpm/packages/ -name '*.rpm' \
        ! -name '*debug*' ! -name '*devel*' ! -name '*.src.rpm' 2>/dev/null)
    if [ ${#rpms[@]} -eq 0 ]; then
        echo "No .rpm files found"
        exit 1
    fi
    rpm -i "${rpms[@]}" || { echo "RPM install failed"; exit 1; }
fi

# Verify installed version
if ! VERSION_OUTPUT=$(/opt/xrpld/bin/xrpld --version); then
    echo "xrpld --version failed; binary not installed correctly"
    exit 1
fi
INSTALLED=$(echo "$VERSION_OUTPUT" | head -1 | awk '{print $NF}')
echo "Installed version: ${INSTALLED}"

# Run unit tests
if [ -n "${CI:-}" ]; then
    unittest_jobs=$(nproc)
else
    unittest_jobs=16
fi

(
    cd /tmp
    /opt/xrpld/bin/xrpld --unittest --unittest-jobs "${unittest_jobs}" > /tmp/unittest_results || true
)

if [ ! -s /tmp/unittest_results ]; then
    echo "Unit test results file is empty — xrpld may have crashed"
    exit 1
fi

num_failures=$(tail /tmp/unittest_results -n1 | grep -oP '\d+(?= failures)')
if [ -z "$num_failures" ]; then
    echo "Could not parse unit test results — expected summary line not found"
    exit 1
fi
if [ "${num_failures}" -ne 0 ]; then
    echo "$num_failures unit test(s) failed:"
    grep 'failed:' /tmp/unittest_results
    exit 1
fi

# Compat path checks
"$(dirname "${BASH_SOURCE[0]}")/check_install_paths.sh"
