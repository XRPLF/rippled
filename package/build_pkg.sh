#!/usr/bin/env bash
set -euo pipefail

# Build an RPM or Debian package from a pre-built xrpld binary.
#
# Usage: build_pkg.sh <src_dir> <build_dir> [version] [pkg_release]
#   src_dir     : path to repository root
#   build_dir   : directory containing the pre-built xrpld binary
#   version     : package version string (e.g. 3.2.0-b1)
#   pkg_release : package release number (default: 1)
#
# The package format is taken from the PKG_TYPE env var if set; otherwise it
# is inferred from the available package manager (apt-get -> deb, dnf/yum -> rpm).

SRC_DIR="$(cd "${1:?src_dir required}" && pwd)"
BUILD_DIR="$(cd "${2:?build_dir required}" && pwd)"
VERSION="${3:-$("${BUILD_DIR}/xrpld" --version | awk 'NR==1 {print $3}')}"
PKG_RELEASE="${4:-1}"

if [[ -z "${PKG_TYPE:-}" ]]; then
    if command -v apt-get >/dev/null 2>&1; then
        PKG_TYPE=deb
    elif command -v dnf >/dev/null 2>&1 || command -v yum >/dev/null 2>&1; then
        PKG_TYPE=rpm
    else
        echo "Cannot infer PKG_TYPE: no apt-get, dnf, or yum on PATH." >&2
        exit 1
    fi
fi

if [[ -z "${SOURCE_DATE_EPOCH:-}" ]]; then
    if git -C "$SRC_DIR" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
        SOURCE_DATE_EPOCH="$(git -C "$SRC_DIR" log -1 --format=%ct)"
    else
        SOURCE_DATE_EPOCH="$(date +%s)"
    fi
fi
export SOURCE_DATE_EPOCH
CHANGELOG_DATE="$(date -u -R -d "@$SOURCE_DATE_EPOCH")"

# Split VERSION at the first '-' into base and optional pre-release suffix.
# Examples: "3.2.0" -> ("3.2.0", ""); "3.2.0-b1" -> ("3.2.0", "b1").
VER_BASE="${VERSION%%-*}"
VER_SUFFIX="${VERSION#*-}"
[[ "${VER_SUFFIX}" == "${VERSION}" ]] && VER_SUFFIX=""

SHARED="${SRC_DIR}/package/shared"
DEBIAN_DIR="${SRC_DIR}/package/debian"

# Stage files that both packaging systems consume using the same filenames.
stage_common() {
    local dest="$1"
    mkdir -p "${dest}"

    cp "${BUILD_DIR}/xrpld"                       "${dest}/xrpld"
    cp "${SRC_DIR}/cfg/xrpld-example.cfg"         "${dest}/xrpld.cfg"
    cp "${SRC_DIR}/cfg/validators-example.txt"    "${dest}/validators.txt"
    cp "${SRC_DIR}/LICENSE.md"                    "${dest}/LICENSE.md"
    cp "${SRC_DIR}/README.md"                     "${dest}/README.md"

    cp "${SHARED}/xrpld.service"                  "${dest}/xrpld.service"
    cp "${SHARED}/xrpld.sysusers"                 "${dest}/xrpld.sysusers"
    cp "${SHARED}/xrpld.tmpfiles"                 "${dest}/xrpld.tmpfiles"
    cp "${SHARED}/xrpld.logrotate"                "${dest}/xrpld.logrotate"
    cp "${SHARED}/update-xrpld.sh"                "${dest}/update-xrpld.sh"
    cp "${SHARED}/update-xrpld-cron"              "${dest}/update-xrpld-cron"
    cp "${SHARED}/update-xrpld.service"           "${dest}/update-xrpld.service"
    cp "${SHARED}/update-xrpld.timer"             "${dest}/update-xrpld.timer"
    cp "${SHARED}/50-xrpld.preset"                "${dest}/50-xrpld.preset"
}

build_rpm() {
    local topdir="${BUILD_DIR}/rpmbuild"
    rm -rf "${topdir}"
    mkdir -p "${topdir}"/{BUILD,BUILDROOT,RPMS,SOURCES,SPECS,SRPMS}

    cp "${SRC_DIR}/package/rpm/xrpld.spec" "${topdir}/SPECS/xrpld.spec"
    stage_common "${topdir}/SOURCES"

    # RPM Version can't contain '-'. A pre-release goes in Release with a
    # leading "0." so 3.2.0-b1 sorts before the final 3.2.0-<pkg_release>.
    local rpm_release="${PKG_RELEASE}"
    [[ -n "${VER_SUFFIX}" ]] && rpm_release="0.${VER_SUFFIX}.${PKG_RELEASE}"

    set -x
    rpmbuild -bb \
        --define "_topdir ${topdir}" \
        --define "xrpld_version ${VER_BASE}" \
        --define "xrpld_release ${rpm_release}" \
        "${topdir}/SPECS/xrpld.spec"
}

build_deb() {
    local staging="${BUILD_DIR}/debbuild/source"
    rm -rf "${staging}"
    mkdir -p "${staging}"

    stage_common "${staging}"
    cp -r "${DEBIAN_DIR}" "${staging}/debian"

    # Debhelper auto-discovers these only from debian/.
    cp "${staging}/xrpld.service"        "${staging}/debian/xrpld.service"
    cp "${staging}/xrpld.sysusers"       "${staging}/debian/xrpld.sysusers"
    cp "${staging}/xrpld.tmpfiles"       "${staging}/debian/xrpld.tmpfiles"
    cp "${staging}/update-xrpld.service" "${staging}/debian/xrpld.update-xrpld.service"
    cp "${staging}/update-xrpld.timer"   "${staging}/debian/xrpld.update-xrpld.timer"
    # Debian '~' marks a pre-release; 3.2.0~b1 sorts before 3.2.0.
    local deb_full_version="${VER_BASE}${VER_SUFFIX:+~${VER_SUFFIX}}-${PKG_RELEASE}"

    # Derive release channel from the version suffix:
    #   (none)      -> stable    (tagged release)
    #   b0          -> develop   (develop-branch build)
    #   b<N>, rc<N> -> unstable  (pre-release)
    local deb_distribution
    case "${VER_SUFFIX}" in
        "")   deb_distribution="stable" ;;
        b0)   deb_distribution="develop" ;;
        *)    deb_distribution="unstable" ;;
    esac

    cat > "${staging}/debian/changelog" <<EOF
xrpld (${deb_full_version}) ${deb_distribution}; urgency=medium
  * Release ${VERSION}.

 -- XRPL Foundation <contact@xrplf.org>  ${CHANGELOG_DATE}
EOF

    chmod +x "${staging}/debian/rules"

    set -x
    ( cd "${staging}" && dpkg-buildpackage -b --no-sign -d )
}

"build_${PKG_TYPE}"
