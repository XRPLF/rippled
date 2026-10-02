#!/bin/bash

# Exit the script as soon as an error occurs.
set -euo pipefail

# This script fails if <head> merges a release back into <base>,
# but also adds commits that aren't on a release or staging branch, see RELEASING.md.
# Merge commits are allowed.
# Usage: .github/scripts/releasing/check-merge-back-commits.sh <base> <head>

if [ "$#" -ne 2 ]; then
    echo "Usage: $0 <base> <head>"
    exit 1
fi

BASE=$1
HEAD=$2

mapfile -t BRANCHES < <(git for-each-ref --format='%(refname)' 'refs/remotes/*/release/*' 'refs/remotes/*/staging/*')
if [ "${#BRANCHES[@]}" -eq 0 ]; then
    echo "Error: No release or staging branches found."
    exit 1
fi

# A PR is a merge-back if some of its commits are on a release or staging branch.
PR_COUNT=$(git rev-list --no-merges --count "${BASE}..${HEAD}")
NEW_COUNT=$(git rev-list --no-merges --count "${BASE}..${HEAD}" --not "${BRANCHES[@]}")
if ((NEW_COUNT == PR_COUNT)); then
    echo "This PR doesn't merge a release back."
    exit 0
fi
if ((NEW_COUNT == 0)); then
    echo "This merge-back adds no commits of its own."
    exit 0
fi

echo "This PR merges a release back, but also adds commits that aren't on a release or staging branch:"
git log --no-merges --format='  %h %s' "${BASE}..${HEAD}" --not "${BRANCHES[@]}"
echo
echo "Make these changes in a separate PR, see RELEASING.md."
exit 1
