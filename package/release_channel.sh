#!/usr/bin/env bash
set -euo pipefail

# Print the release channel for an xrpld version. The single definition of that
# mapping: build_pkg.sh writes the result as the Debian changelog distribution,
# and publish_pkg.sh turns it into a target repository pair.
#
# Usage: release_channel.sh <version>
#
#   3.4.0                -> stable
#   3.4.0-b0[+meta]      -> develop
#   3.4.0-b1, 3.4.0-rc1  -> unstable

version="${1:-}"
if [[ -z "${version}" ]]; then
    echo "usage: release_channel.sh <version>" >&2
    exit 2
fi

pre_release=""
if [[ "${version}" == *-* ]]; then
    pre_release="${version#*-}"
fi

if [[ -z "${pre_release}" ]]; then
    echo stable
elif [[ "${pre_release}" =~ ^b0(\+.*)?$ ]]; then
    echo develop
elif [[ "${pre_release}" =~ ^(b[1-9][0-9]*|rc[0-9]+)(\+.*)?$ ]]; then
    echo unstable
else
    echo "release_channel.sh: unsupported pre-release '${pre_release}'." >&2
    echo "Use bN or rcN, e.g. 3.2.0-b1 or 3.2.0-rc2." >&2
    exit 1
fi
