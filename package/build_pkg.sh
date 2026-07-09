#!/usr/bin/env bash
set -euo pipefail

# Build an RPM or Debian package from a pre-built xrpld binary.
#
# Flags override env vars; env vars override defaults.

usage() {
    cat <<'EOF'
Usage: build_pkg.sh [options]

Options (each can also be set via the env var shown):
  --src-dir DIR            repo root                 [SRC_DIR;           default: ${PWD}]
  --build-dir DIR          directory holding xrpld   [BUILD_DIR;         default: ${PWD}/build]
  --pkg-release N          package release iteration [PKG_RELEASE;       default: 1]
  --source-date-epoch SECS reproducibility timestamp [SOURCE_DATE_EPOCH; latest git ctime; fallback: current time]
  -h, --help               show this help and exit
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
PKG_RELEASE="${PKG_RELEASE:-1}"
SOURCE_DATE_EPOCH="${SOURCE_DATE_EPOCH:-}"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --src-dir)
            need_arg "$@"
            SRC_DIR="$2"
            shift 2
            ;;
        --build-dir)
            need_arg "$@"
            BUILD_DIR="$2"
            shift 2
            ;;
        --pkg-release)
            need_arg "$@"
            PKG_RELEASE="$2"
            shift 2
            ;;
        --source-date-epoch)
            need_arg "$@"
            SOURCE_DATE_EPOCH="$2"
            shift 2
            ;;
        -h | --help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

SRC_DIR="$(cd "${SRC_DIR:-${PWD}}" && pwd)"
BUILD_DIR="${BUILD_DIR:-${PWD}/build}"
if [[ ! -d "${BUILD_DIR}" ]]; then
    echo "build_pkg.sh: build directory not found: ${BUILD_DIR}" >&2
    echo "Build xrpld before packaging, or set BUILD_DIR to the directory containing xrpld." >&2
    exit 1
fi
BUILD_DIR="$(cd "${BUILD_DIR}" && pwd)"

xrpld_binary="${BUILD_DIR}/xrpld"
if [[ ! -x "${xrpld_binary}" ]]; then
    echo "build_pkg.sh: expected executable xrpld binary at ${xrpld_binary}." >&2
    echo "Build xrpld before packaging, or set BUILD_DIR to the directory containing xrpld." >&2
    exit 1
fi

xrpld_version="$("${xrpld_binary}" --version | awk 'NR == 1 { print $3 }')"

if [[ -z "${xrpld_version}" ]]; then
    echo "build_pkg.sh: unable to derive xrpld version from ${xrpld_binary} --version." >&2
    exit 1
fi

# The version as the package formats consume it: identical to xrpld_version
# except a pre-release uses '~' (3.2.0-b1 -> 3.2.0~b1), which also sorts before
# the final 3.2.0; a no-op for a final release. Lowercase = derived internally,
# not an input (cf. pkg_type).
pkg_version="${xrpld_version}"
pre_release=""
if [[ "${xrpld_version}" == *-* ]]; then
    pre_release="${xrpld_version#*-}"
    pkg_version="${xrpld_version%%-*}~${pre_release}"
fi

# BuildInfo already SemVer-validates the binary's version. Packaging adds one
# narrower constraint: after pre-release normalization, the package version must
# not contain '-' because RPM forbids it in Version and Debian uses it as the
# upstream/revision separator.
if [[ "${pkg_version}" == *-* ]]; then
    echo "build_pkg.sh: unsupported xrpld version '${xrpld_version}'." >&2
    echo "Package version '${pkg_version}' cannot contain '-'." >&2
    echo "Use a single-token pre-release like 3.2.0-b1 or 3.2.0-rc2." >&2
    exit 1
fi

if [[ -z "${pre_release}" && "${xrpld_version}" == *+* ]]; then
    echo "build_pkg.sh: unsupported xrpld version '${xrpld_version}'." >&2
    echo "Build metadata is only supported on bN/rcN pre-releases." >&2
    exit 1
fi

if [[ -n "${pre_release}" && ! "${pre_release}" =~ ^(b0|b[1-9][0-9]*|rc[0-9]+)(\+.*)?$ ]]; then
    echo "build_pkg.sh: unsupported xrpld pre-release '${pre_release}'." >&2
    echo "Use bN or rcN, e.g. 3.2.0-b1 or 3.2.0-rc2." >&2
    exit 1
fi

if command -v apt-get >/dev/null 2>&1; then
    pkg_type=deb
elif command -v dnf >/dev/null 2>&1 || command -v yum >/dev/null 2>&1; then
    pkg_type=rpm
else
    echo "Cannot infer pkg_type: no apt-get, dnf, or yum on PATH." >&2
    exit 1
fi

if [[ -z "${SOURCE_DATE_EPOCH}" ]]; then
    if git -C "${SRC_DIR}" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
        SOURCE_DATE_EPOCH="$(git -C "${SRC_DIR}" log -1 --format=%ct)"
    else
        SOURCE_DATE_EPOCH="$(date +%s)"
    fi
fi

export SOURCE_DATE_EPOCH
CHANGELOG_DATE="$(date -u -R -d "@${SOURCE_DATE_EPOCH}")"

SHARED="${SRC_DIR}/package/shared"
DEBIAN_DIR="${SRC_DIR}/package/debian"

# Stage files that both packaging systems consume using the same filenames.
stage_common() {
    local dest="$1"
    mkdir -p "${dest}"

    cp "${BUILD_DIR}/xrpld" "${dest}/xrpld"
    cp "${SRC_DIR}/cfg/xrpld-example.cfg" "${dest}/xrpld.cfg"
    cp "${SRC_DIR}/cfg/validators-example.txt" "${dest}/validators.txt"
    cp "${SRC_DIR}/LICENSE.md" "${dest}/LICENSE.md"
    cp "${SRC_DIR}/README.md" "${dest}/README.md"

    cp "${SHARED}/xrpld.service" "${dest}/xrpld.service"
    cp "${SHARED}/xrpld.sysusers" "${dest}/xrpld.sysusers"
    cp "${SHARED}/xrpld.tmpfiles" "${dest}/xrpld.tmpfiles"
    cp "${SHARED}/xrpld.logrotate" "${dest}/xrpld.logrotate"
}

build_rpm() {
    local topdir="${BUILD_DIR}/rpmbuild"
    rm -rf "${topdir}"
    mkdir -p "${topdir}"/{BUILD,BUILDROOT,RPMS,SOURCES,SPECS,SRPMS}

    cp "${SRC_DIR}/package/rpm/xrpld.spec" "${topdir}/SPECS/xrpld.spec"
    stage_common "${topdir}/SOURCES"

    set -x
    rpmbuild -bb \
        --define "_topdir ${topdir}" \
        --define "pkg_version ${pkg_version}" \
        --define "pkg_release ${PKG_RELEASE}" \
        "${topdir}/SPECS/xrpld.spec"
}

build_deb() {
    local staging="${BUILD_DIR}/debbuild/source"
    rm -rf "${staging}"
    mkdir -p "${staging}"

    stage_common "${staging}"
    cp -r "${DEBIAN_DIR}" "${staging}/debian"

    cp "${staging}/xrpld.service" "${staging}/debian/xrpld.service"
    cp "${staging}/xrpld.sysusers" "${staging}/debian/xrpld.sysusers"
    cp "${staging}/xrpld.tmpfiles" "${staging}/debian/xrpld.tmpfiles"
    cp "${staging}/xrpld.logrotate" "${staging}/debian/xrpld.logrotate"

    # Choose the Debian repository component for this package.
    #   3.2.0 -> stable, *-b0[+metadata] -> develop,
    #   bN/rcN pre-releases -> unstable.
    local deb_component
    if [[ -z "${pre_release}" ]]; then
        deb_component="stable"
    elif [[ "${pre_release}" =~ ^b0(\+.*)?$ ]]; then
        deb_component="develop"
    elif [[ "${pre_release}" =~ ^(b[1-9][0-9]*|rc[0-9]+)(\+.*)?$ ]]; then
        deb_component="unstable"
    else
        echo "build_pkg.sh: unsupported xrpld pre-release '${pre_release}'." >&2
        echo "Use bN or rcN, e.g. 3.2.0-b1 or 3.2.0-rc2." >&2
        exit 1
    fi

    # Debian version is <upstream>[~<pre>]-<pkg release>.
    cat >"${staging}/debian/changelog" <<EOF
xrpld (${pkg_version}-${PKG_RELEASE}) ${deb_component}; urgency=medium
  * Release ${xrpld_version}.

 -- XRPL Foundation <contact@xrplf.org>  ${CHANGELOG_DATE}
EOF

    chmod +x "${staging}/debian/rules"

    set -x
    (cd "${staging}" && dpkg-buildpackage -b --no-sign -d)
}

"build_${pkg_type}"
