#!/bin/bash

# Exit the script as soon as an error occurs.
set -euo pipefail

# This script fails if a final release (a tag like 3.4.0) on a release branch
# is not merged back into <develop> within a few days, see RELEASING.md.
# Usage: .github/scripts/releasing/check-releases-merged.sh <develop>

if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <develop>"
    exit 1
fi

DEVELOP=$1
GRACE_DAYS=3

mapfile -t MERGED_ARGS < <(git for-each-ref --format='--merged=%(refname)' 'refs/remotes/*/release/*')
if [ "${#MERGED_ARGS[@]}" -eq 0 ]; then
    echo "Error: No release branches found."
    exit 1
fi

# Tags on a release branch that <develop> does not contain.
TAGS=$(git for-each-ref --format='%(refname:short) %(creatordate:unix)' \
    "${MERGED_ARGS[@]}" --no-merged="${DEVELOP}" 'refs/tags/[0-9]*')

MISSING=0
while read -r TAG CREATED; do
    if ! [[ "${TAG}" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
        continue
    fi
    if (($(date +%s) - CREATED < GRACE_DAYS * 86400)); then
        echo "${TAG}: not merged yet, still within the ${GRACE_DAYS}-day grace period."
    else
        echo "${TAG}: not merged into develop."
        MISSING=1
    fi
done <<<"${TAGS}"

if [ "${MISSING}" -ne 0 ]; then
    echo
    echo "Merge the missing releases back into develop, see RELEASING.md."
    exit 1
fi
echo "No releases past the grace period are missing from develop."
