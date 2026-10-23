#!/bin/bash
#
# check-distrusted-roots.sh — fail if a CA bundle trusts a root of the
# compromised eMudhra intermediates; see https://emudhra.we-have-a-problem.com/.
#
# Usage: check-distrusted-roots.sh [ca-bundle]
# Defaults to the system bundle (Debian or RHEL path).
#
# Roots are matched by SHA-256 fingerprint, needing only bash, base64 and
# sha256sum (nixos/nix has no awk). Only plain CERTIFICATE blocks are checked:
# OpenSSL's TRUSTED CERTIFICATE blocks carry their own trust settings, which is
# how nixpkgs' cacert marks a blacklisted root as rejected.

set -euo pipefail

declare -A ROOTS=(
    [d7a7a0fb5d7e2731d771e9484ebcdef71d5f0c3e0a2948782bc83ee0ea699ef4]="AAA Certificate Services"
    [86a1ecba089c4a8d3bbe2734c612ba341d813e043cf9e8a862cd5c57a36bbe6b]="emSign ECC Root CA - G3"
)

BUNDLE="${1:-}"
if [ -z "${BUNDLE}" ]; then
    for CANDIDATE in /etc/ssl/certs/ca-certificates.crt /etc/pki/tls/certs/ca-bundle.crt; do
        if [ -f "${CANDIDATE}" ]; then
            BUNDLE="${CANDIDATE}"
            break
        fi
    done
fi

STATUS=0
COUNT=0
PEM=""
while IFS= read -r LINE || [ -n "${LINE}" ]; do
    case "${LINE}" in
        "-----BEGIN CERTIFICATE-----") PEM="" ;;
        "-----END CERTIFICATE-----")
            COUNT=$((COUNT + 1))
            FINGERPRINT="$(base64 -d <<<"${PEM}" | sha256sum)" || {
                echo "ERROR: certificate ${COUNT} in ${BUNDLE} is not valid base64" >&2
                exit 1
            }
            ROOT="${ROOTS[${FINGERPRINT%% *}]:-}"
            if [ -n "${ROOT}" ]; then
                echo "ERROR: ${BUNDLE} trusts '${ROOT}'" >&2
                STATUS=1
            fi
            ;;
        # Lines outside a CERTIFICATE block only collect until the next BEGIN.
        *) PEM+="${LINE}" ;;
    esac
done <"${BUNDLE:?no system CA bundle found}"

# An empty or unparsable bundle would otherwise pass.
if [ "${COUNT}" -eq 0 ]; then
    echo "ERROR: ${BUNDLE} has no certificates" >&2
    exit 1
fi
echo "${BUNDLE}: checked ${COUNT} certificates"
exit "${STATUS}"
