#!/bin/bash

if [[ $# -ne 3 || "$1" == "--help" || "$1" = "-h" ]]
then
  name=$( basename $0 )
  cat <<- USAGE
  Usage: $name pr "title" "description"

  * All commits in the specified PR will be squashed and a new commit prepared
    with the provided title and description as commit message.
  * This script will not push the new commit. You will need to do so yourself
    by force-pushing, since you will be rewriting history. You must be the
    author of the PR or a maintainer of the repository in order to perform this
    operation.
  * The 'gh' CLI tool must be installed and authenticated.
  * To write a multiline description, you can use "\$(cat <<EOF
line 1
line 2
EOF
)" to pass it as a single argument.
USAGE
exit 0
fi

pr="$1"
shift

title=$1
shift

description=$1
shift

set -e

#echo "Checking workspace."
#diff=$(git status --porcelain)
#if [ -n "${diff}" ]; then
#  echo "Error: Workspace is not clean. Please commit or stash your changes."
#  exit 1
#fi

echo "Checking out PR ${pr}."
gh pr checkout "${pr}"

echo "Getting the current branch of the PR."
current=$(git branch --show-current)

echo "Getting the base branch of the PR."
base=$(gh pr view --json "baseRefName" --jq '.baseRefName')
if [ -z "${base}" ]; then
  echo "Error: Could not determine base branch of PR ${pr}."
  exit 1
fi

echo "Ensuring the PR branch '${current}' is up to date with the base branch '${base}'."
git checkout ${base}
git pull --rebase
git checkout ${current}
git merge --ff-only ${base}

echo "Squashing commits in the PR."
git reset --soft $(git merge-base ${base} HEAD)
git commit -S -m "${title}" -m "${description}"

cat << EOF
----------------------------------------------------------------------
This script will not push. Verify everything is correct, then force
push to the branch associated with the PR using the following command:

git push --force-with-lease origin ${current}
----------------------------------------------------------------------
EOF
