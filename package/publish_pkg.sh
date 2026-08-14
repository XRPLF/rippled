#!/usr/bin/env bash
set -euo pipefail

# Publish the DEB and RPM packages built by build_pkg.sh to the XRPLF package
# repositories on Sonatype Nexus, which owns the repository indexes and signs
# them; nothing here does.
#
# Usage: publish_pkg.sh <channel> [package-dir]
#
#   channel      selects the repository pair; release_channel.sh derives it from
#                an xrpld version, except 'private'
#   package-dir  searched recursively for *.deb, *.ddeb and *.rpm ('build' by
#                default)
#
# NEXUS_USERNAME and NEXUS_PASSWORD are required. NEXUS_URL overrides the target
# instance, and DRY_RUN=1 lists the uploads without performing them.

channel="${1:-}"
pkg_dir="${2:-build}"
nexus_url="${NEXUS_URL:-https://deb.xrplf.org}"

case "${channel}" in
    stable)
        deb_repo="deb"
        rpm_repo="rpm"
        ;;
    unstable | experimental | develop | private)
        deb_repo="deb-${channel}"
        rpm_repo="rpm-${channel}"
        ;;
    *)
        echo "usage: publish_pkg.sh <stable|unstable|experimental|develop|private> [package-dir]" >&2
        exit 2
        ;;
esac

if [[ -z "${DRY_RUN:-}" ]]; then
    : "${NEXUS_USERNAME:?is required}" "${NEXUS_PASSWORD:?is required}"
fi

# Deliberate curl choices:
#
#   - no --fail, which would hide the response body where Nexus explains what it
#     rejected
#   - no --location, since curl downgrades a redirected POST to GET and turns an
#     upload into a no-op that still answers 200
#   - credentials on stdin, to keep them out of the process list
upload() {
    local url="$1"
    shift
    [[ -z "${DRY_RUN:-}" ]] || return 0

    local body code status=0
    body="$(mktemp)"
    code="$(
        printf 'user = %s:%s\n' "${NEXUS_USERNAME}" "${NEXUS_PASSWORD}" |
            curl \
                --config - \
                --silent \
                --show-error \
                --retry 3 \
                --retry-delay 5 \
                --retry-all-errors \
                --output "${body}" \
                --write-out '%{http_code}' \
                "$@" \
                "${url}"
    )" || status=$?

    if [[ ${status} -ne 0 || ! "${code}" =~ ^2[0-9][0-9]$ ]]; then
        echo "publish_pkg.sh: upload failed (curl ${status}, HTTP ${code}): ${url}" >&2
        cat "${body}" >&2
        echo >&2
        rm -f "${body}"
        exit 1
    fi

    rm -f "${body}"
}

echo "Publishing ${pkg_dir} to ${deb_repo} and ${rpm_repo} on ${nexus_url}:"

count=0
while IFS= read -r -d '' file; do
    name="${file##*/}"
    case "${name}" in
        # The apt distribution and component are configured on the repository, so
        # packages POST to its root.
        *.deb | *.ddeb)
            echo "  ${name} -> ${deb_repo}"
            upload "${nexus_url}/repository/${deb_repo}/" \
                --header 'Content-Type: multipart/form-data' \
                --data-binary "@${file}"
            ;;
        # yum repositories are addressed by path; the arch comes from the name.
        *.rpm)
            arch="${name%.rpm}"
            arch="${arch##*.}"
            echo "  ${name} -> ${rpm_repo}/${arch}"
            upload "${nexus_url}/repository/${rpm_repo}/${arch}/${name}" \
                --upload-file "${file}"
            ;;
    esac
    count=$((count + 1))
done < <(find "${pkg_dir}" -type f \( -name '*.deb' -o -name '*.ddeb' -o -name '*.rpm' \) -print0)

# Uploading nothing would otherwise look like a successful publish.
if [[ ${count} -eq 0 ]]; then
    echo "publish_pkg.sh: no packages found in ${pkg_dir}." >&2
    exit 1
fi

echo "${count} package(s) ${DRY_RUN:+would be }published."
