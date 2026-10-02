#!/bin/bash

# Exit the script as soon as an error occurs.
set -euo pipefail

# This script fails if <head> contains release or staging commits that <base> does not,
# i.e. if <head> merges a release back into <base>.
# Used in the merge queue, which squashes PRs and would drop the merge commit, see RELEASING.md.
# Usage: .github/scripts/releasing/check-no-merge-back.sh <base> <head>

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

RELEASE_COMMITS=$(git rev-list "${BRANCHES[@]}" --not "${BASE}" | sort)
HEAD_COMMITS=$(git rev-list "${HEAD}" --not "${BASE}" | sort)
MERGED=$(comm -12 <(echo "${RELEASE_COMMITS}") <(echo "${HEAD_COMMITS}"))
if [ -z "${MERGED}" ]; then
    echo "No release commits are merged back."
    exit 0
fi

echo "This PR merges $(wc -l <<<"${MERGED}" | tr -d ' ') release commits back, e.g.:"
head -5 <<<"${MERGED}" | xargs git log --no-walk --format='  %h %s'
echo
echo "Merge-backs must not go through the merge queue, which squashes them."
echo "Fast-forward develop to the PR branch instead, see RELEASING.md."
exit 1
