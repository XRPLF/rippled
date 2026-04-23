#!/bin/bash

if [[ $# -lt 3 || "$1" == "--help" || "$1" = "-h" ]]
then
  name=$( basename $0 )
  cat <<- USAGE
  Usage: $name workbranch base/branch user/branch [user/branch [...]]

  * workbranch will be created locally from base/branch
  * base/branch and user/branch may be specified as user:branch to allow
    easy copying from Github PRs
  * Remotes for each user must already be set up
USAGE
exit 0
fi

work="$1"
shift

branches=( $( echo "${@}" | sed "s/:/\//" ) )
base="${branches[0]}"
unset branches[0]

set -e

users=()
for b in "${branches[@]}"
do
  users+=( $( echo $b | cut -d/ -f1 ) )
done

users=( $( printf '%s\n' "${users[@]}" | sort -u ) )

git fetch --multiple upstreams "${users[@]}"
git checkout -B "$work" --no-track "$base"

for b in "${branches[@]}"
do
  git merge --squash "${b}"
  git commit -S # Use the commit message provided on the PR
done

# Make sure the commits look right
git log --show-signature "$base..HEAD"

parts=( $( echo $base | sed "s/\// /" ) )
repo="${parts[0]}"
b="${parts[1]}"
push=$repo
if [[ "$push" == "upstream" ]]
then
  push="upstream-push"
fi
if [[ "$repo" == "upstream" ]]
then
  repo="upstreams"
fi
cat << PUSH

-------------------------------------------------------------------
This script will not push. Verify everything is correct, then push
to your repo, and create a PR if necessary. Once the PR is approved,
run:

git push $push HEAD:$b
git fetch $repo
-------------------------------------------------------------------
PUSH
