#!/usr/bin/env bash
# Build an RPM or Debian package from a pre-built xrpld binary.
#
# Usage: build_pkg.sh <pkg_type> <src_dir> <build_dir> [version] [pkg_release]
#   pkg_type    : rpm | deb
#   src_dir     : path to repository root
#   build_dir   : directory containing the pre-built xrpld binary
#   version     : package version string (e.g. 2.4.0-b1)
#   pkg_release : package release number (default: 1)

set -euo pipefail

PKG_TYPE="${1:?pkg_type required}"
SRC_DIR="$(cd "${2:?src_dir required}" && pwd)"
BUILD_DIR="$(cd "${3:?build_dir required}" && pwd)"
VERSION="${4:-1.0.0}"
PKG_RELEASE="${5:-1}"

SHARED="${SRC_DIR}/package/shared"

# Stage files common to both package types into a target directory.
stage_common() {
    local dest="$1"
    cp "${BUILD_DIR}/xrpld"                  "${dest}/xrpld"
    cp "${SRC_DIR}/cfg/xrpld-example.cfg"    "${dest}/xrpld.cfg"
    cp "${SRC_DIR}/cfg/validators-example.txt" "${dest}/validators.txt"
    cp "${SHARED}/xrpld.logrotate"           "${dest}/xrpld.logrotate"
    cp "${SHARED}/update-xrpld.sh"           "${dest}/update-xrpld.sh"
    cp "${SHARED}/update-xrpld-cron"         "${dest}/update-xrpld-cron"
}

build_rpm() {
    local topdir="${BUILD_DIR}/rpmbuild"
    mkdir -p "${topdir}"/{BUILD,BUILDROOT,RPMS,SOURCES,SPECS,SRPMS}

    cp "${BUILD_DIR}/package/rpm/xrpld.spec" "${topdir}/SPECS/xrpld.spec"

    stage_common "${topdir}/SOURCES"
    cp "${SHARED}/xrpld.service"  "${topdir}/SOURCES/xrpld.service"
    cp "${SHARED}/xrpld.sysusers" "${topdir}/SOURCES/xrpld.sysusers"
    cp "${SHARED}/xrpld.tmpfiles" "${topdir}/SOURCES/xrpld.tmpfiles"

    set -x
    rpmbuild -bb \
        --define "_topdir ${topdir}" \
        "${topdir}/SPECS/xrpld.spec"
}

build_deb() {
    local staging="${BUILD_DIR}/debbuild/source"
    rm -rf "${staging}"
    mkdir -p "${staging}"

    stage_common "${staging}"
    cp "${SRC_DIR}/README.md"  "${staging}/"
    cp "${SRC_DIR}/LICENSE.md" "${staging}/"

    # debian/ control files
    cp -r "${SRC_DIR}/package/deb/debian" "${staging}/debian"

    # Shared support files for dh_installsystemd / sysusers / tmpfiles
    cp "${SHARED}/xrpld.service"  "${staging}/debian/xrpld.service"
    cp "${SHARED}/xrpld.sysusers" "${staging}/debian/xrpld.sysusers"
    cp "${SHARED}/xrpld.tmpfiles" "${staging}/debian/xrpld.tmpfiles"

    # Generate debian/changelog (pre-release versions use ~ instead of -).
    local deb_version="${VERSION//-/\~}"
    # TODO: Add facility for generating the changelog
    cat > "${staging}/debian/changelog" <<EOF
xrpld (${deb_version}-${PKG_RELEASE}) unstable; urgency=medium

  * Release ${VERSION}.

 -- XRPL Foundation <contact@xrplf.org>  $(LC_ALL=C date -u -R)
EOF

    chmod +x "${staging}/debian/rules"

    set -x
    cd "${staging}"
    dpkg-buildpackage -b --no-sign -d
}

case "${PKG_TYPE}" in
    rpm) build_rpm ;;
    deb) build_deb ;;
    *)
        echo "Unknown package type: ${PKG_TYPE}" >&2
        exit 1
        ;;
esac
