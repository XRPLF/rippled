#!/usr/bin/env bash
set -euo pipefail

# Sign the RPMs built by build_pkg.sh. Nexus cannot sign hosted yum metadata, so
# the packages carry the signature themselves and rpm clients verify them with
# gpgcheck=1.
#
# Usage: sign_rpm.sh [package-dir]
#
#   package-dir  searched recursively for *.rpm ('build' by default)
#
# PKG_SIGNING_KEY must hold an armoured PGP private key. It has no flag, to keep
# the key out of the process list.
#
# There is no DEB equivalent: apt trusts the repository metadata, which Nexus
# signs, rather than the packages themselves.

pkg_dir="${1:-build}"

mapfile -d '' rpms < <(find "${pkg_dir}" -type f -name '*.rpm' -print0)

# Signing nothing would otherwise look like a successful signing.
if [[ ${#rpms[@]} -eq 0 ]]; then
    echo "sign_rpm.sh: no RPMs found in ${pkg_dir}." >&2
    exit 1
fi

: "${PKG_SIGNING_KEY:?is required}"

# Global, and expanded by the trap when it fires: the keyring holds an
# unencrypted private key, so it must go even if signing fails.
signing_home="$(mktemp -d)"
trap 'rm -rf "${signing_home}"' EXIT
export GNUPGHOME="${signing_home}"

printf '%s' "${PKG_SIGNING_KEY}" | gpg --batch --quiet --import

# Exactly one secret key, so that picking the first below is not a guess between
# several.
secrets="$(gpg --list-secret-keys --with-colons | grep -c '^sec:' || true)"
if [[ "${secrets}" -ne 1 ]]; then
    echo "sign_rpm.sh: PKG_SIGNING_KEY must hold exactly one secret key, found ${secrets}." >&2
    exit 1
fi

key="$(gpg --list-secret-keys --with-colons | awk -F: '/^fpr:/ { print $10; exit }')"
echo "Signing ${#rpms[@]} RPM(s) with ${key}."

# Loopback pinentry: the key is unattended, so there is no tty to prompt on.
rpmsign \
    --define "_gpg_name ${key}" \
    --define "_gpg_sign_cmd_extra_args --pinentry-mode loopback --batch --yes" \
    --addsign "${rpms[@]}"

# rpmsign can exit 0 having attached nothing, and an unsigned package is only
# rejected later, on the installing machine. Both header tags are checked
# because an RSA signature lands in RSAHEADER and a DSA or EdDSA one in
# DSAHEADER.
for pkg in "${rpms[@]}"; do
    signature="$(rpm --query --queryformat '%{RSAHEADER:pgpsig}%{DSAHEADER:pgpsig}' --package "${pkg}")"
    if [[ "${signature}" == "(none)(none)" ]]; then
        echo "sign_rpm.sh: ${pkg} is unsigned after rpmsign." >&2
        exit 1
    fi
done
