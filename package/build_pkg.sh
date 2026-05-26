#!/usr/bin/env bash
set -euo pipefail

# Build an RPM or Debian package from a pre-built xrpld binary.
#
# Flags override env vars; env vars override defaults. Env vars are intended
# for CMake/systemd/CI integration; flags are for explicit invocation.

usage() {
    cat <<'EOF'
Usage: build_pkg.sh [options]

Options (each can also be set via the env var shown):
  --src-dir DIR             repo root                  [SRC_DIR;           default: $PWD]
  --build-dir DIR           directory holding xrpld    [BUILD_DIR;         default: $PWD/build]
  --pkg-version STR         version, e.g. 3.2.0-b1     [PKG_VERSION;       default: parsed from xrpld --version]
  --pkg-release N           package release number     [PKG_RELEASE;       default: 1]
  --source-date-epoch SECS  reproducibility timestamp  [SOURCE_DATE_EPOCH; default: latest git commit ctime]
  -h, --help                show this help and exit
EOF
}

need_arg() {
    if [[ $# -lt 2 || "$2" == --* ]]; then
        echo "Missing value for $1" >&2
        exit 2
    fi
}

# Seed from env. CLI parsing below overrides these directly.
SRC_DIR="${SRC_DIR:-}"
BUILD_DIR="${BUILD_DIR:-}"
PKG_VERSION="${PKG_VERSION:-}"
PKG_RELEASE="${PKG_RELEASE:-}"
SOURCE_DATE_EPOCH="${SOURCE_DATE_EPOCH:-}"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --src-dir)            need_arg "$@"; SRC_DIR="$2";           shift 2 ;;
        --build-dir)          need_arg "$@"; BUILD_DIR="$2";         shift 2 ;;
        --pkg-version)        need_arg "$@"; PKG_VERSION="$2";       shift 2 ;;
        --pkg-release)        need_arg "$@"; PKG_RELEASE="$2";       shift 2 ;;
        --source-date-epoch)  need_arg "$@"; SOURCE_DATE_EPOCH="$2"; shift 2 ;;
        -h|--help)            usage; exit 0 ;;
        *)
            echo "Unknown argument: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

SRC_DIR="$(cd "${SRC_DIR:-${PWD}}" && pwd)"
BUILD_DIR="$(cd "${BUILD_DIR:-${PWD}/build}" && pwd)"
PKG_RELEASE="${PKG_RELEASE:-1}"

if [[ -z "${PKG_VERSION}" ]]; then
    PKG_VERSION="$("${BUILD_DIR}/xrpld" --version | awk 'NR==1 {print $3; exit}')"
fi

if [[ -z "${PKG_VERSION}" ]]; then
    echo "PKG_VERSION is empty (not provided and could not be derived)." >&2
    exit 1
fi

VERSION="${PKG_VERSION}"

if command -v apt-get >/dev/null 2>&1; then
    pkg_type=deb
elif command -v dnf >/dev/null 2>&1 || command -v yum >/dev/null 2>&1; then
    pkg_type=rpm
else
    echo "Cannot infer pkg_type: no apt-get, dnf, or yum on PATH." >&2
    exit 1
fi

if [[ -z "${SOURCE_DATE_EPOCH}" ]]; then
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

# Reject multi-segment suffixes (e.g. "beta-1", "rc1-15-gabc123"). The RPM
# Release field forbids '-', and the convention here is single-token suffixes
# like b1 or rc2. Fail early with a clear message rather than letting either
# rpmbuild blow up or silently mangling dashes into dots.
if [[ "${VER_SUFFIX}" == *-* ]]; then
    echo "build_pkg.sh: multi-segment pre-release in VERSION='${VERSION}' (suffix '${VER_SUFFIX}')." >&2
    echo "Use single-token suffixes like 3.2.0-b1 or 3.2.0-rc2." >&2
    exit 1
fi

SHARED="${SRC_DIR}/package/shared"
DEBIAN_DIR="${SRC_DIR}/package/debian"

# Stage files that both packaging systems consume using the same filenames.
stage_common() {
    local dest="$1"
    mkdir -p "${dest}"

    cp "${BUILD_DIR}/xrpld"                     "${dest}/xrpld"
    cp "${SRC_DIR}/cfg/xrpld-example.cfg"       "${dest}/xrpld.cfg"
    cp "${SRC_DIR}/cfg/validators-example.txt"  "${dest}/validators.txt"
    cp "${SRC_DIR}/LICENSE.md"                  "${dest}/LICENSE.md"
    cp "${SRC_DIR}/README.md"                   "${dest}/README.md"

    cp "${SHARED}/xrpld.service"                "${dest}/xrpld.service"
    cp "${SHARED}/xrpld.sysusers"               "${dest}/xrpld.sysusers"
    cp "${SHARED}/xrpld.tmpfiles"               "${dest}/xrpld.tmpfiles"
    cp "${SHARED}/xrpld.logrotate"              "${dest}/xrpld.logrotate"
    cp "${SHARED}/update-xrpld"                 "${dest}/update-xrpld"
    cp "${SHARED}/update-xrpld.service"         "${dest}/update-xrpld.service"
    cp "${SHARED}/update-xrpld.timer"           "${dest}/update-xrpld.timer"
    cp "${SHARED}/50-xrpld.preset"              "${dest}/50-xrpld.preset"
}

build_rpm() {
    local topdir="${BUILD_DIR}/rpmbuild"
    rm -rf "${topdir}"
    mkdir -p "${topdir}"/{BUILD,BUILDROOT,RPMS,SOURCES,SPECS,SRPMS}

    cp "${SRC_DIR}/package/rpm/xrpld.spec" "${topdir}/SPECS/xrpld.spec"
    stage_common "${topdir}/SOURCES"

    # RPM Version can't contain '-'. A pre-release goes in Release with a
    # leading "0." so 3.2.0-b1 sorts before the final 3.2.0-<pkg_release>.
    # The order is "0.<pkg_release>.<suffix>" (e.g. 0.1.b6) — the Fedora/EPEL
    # convention. Reversing to "0.<suffix>.<pkg_release>" (e.g. 0.b6.1) breaks
    # rpmvercmp against the former because numeric segments outrank alphabetic
    # ones, so "0.1.b5" would sort newer than "0.b6.1".
    local rpm_release="${PKG_RELEASE}"
    [[ -n "${VER_SUFFIX}" ]] && rpm_release="0.${PKG_RELEASE}.${VER_SUFFIX}"

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
    cp "${staging}/xrpld.logrotate"      "${staging}/debian/xrpld.logrotate"
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

"build_${pkg_type}"
