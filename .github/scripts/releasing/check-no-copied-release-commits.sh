#!/bin/bash

# Exit the script as soon as an error occurs.
set -euo pipefail

# This script fails if the commits in <base>..<head> copy commits from a release or staging branch
# (e.g. by rebasing or cherry-picking them) instead of merging that branch, see RELEASING.md.
# Commits are compared by patch-id,
# and only against release and staging commits that <head> does not already contain.
# Usage: .github/scripts/releasing/check-no-copied-release-commits.sh <base> <head>

if [ "$#" -ne 2 ]; then
    echo "Usage: $0 <base> <head>"
    exit 1
fi

BASE=$1
HEAD=$2

patch_ids() {
    git log --no-merges --patch --no-color --no-ext-diff "$@" | git patch-id --stable | sort
}

SCRIPT_DIR=$(dirname "${BASH_SOURCE[0]}")
# shellcheck source=.github/scripts/releasing/common.sh
source "${SCRIPT_DIR}/common.sh"
load_release_branches

# Each line is "<patch-id> <commit>".
RELEASE_PATCHES=$(patch_ids "${BRANCHES[@]}" --not "${HEAD}")
PR_PATCHES=$(patch_ids "${BASE}..${HEAD}")

# Each line is "<patch-id> <release commit> <PR commit>".
COPIES=$(join <(echo "${RELEASE_PATCHES}") <(echo "${PR_PATCHES}"))
if [ -z "${COPIES}" ]; then
    echo "No copied release commits found."
    exit 0
fi

echo "These commits copy release commits instead of merging them:"
while read -r _ RELEASE_COMMIT PR_COMMIT; do
    echo "  $(git log -1 --format='%h %s' "${PR_COMMIT}") (copies ${RELEASE_COMMIT:0:10})"
done <<<"${COPIES}"
echo
echo "Merge the release tag (or branch) instead, see RELEASING.md."
exit 1
