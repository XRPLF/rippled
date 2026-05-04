#!/bin/bash -e

set -o pipefail

_usage()
{
    self=$( basename -- $( echo $0 ) )
    cat << USAGE
Usage: $self release_branch develop_branch working_branch [new_version]

This script is intended to be used to synchronize changes between two
branches where the "release_branch" has a few insignificant changes (e.g.
version numbers) compared to the "develop_branch", which has active
ongoing development. It has not (yet) been tested in scenarios where the
branches have significantly diverged, or where one has been merged to
the other.

release_branch: Usually a pending release branch, this is the low
    activity branch which needs to be updated from develop_branch.

develop_branch: The active development branch.

working_branch: This branch will be created from the release_branch to
    have the new commits applied. If it exists, it will be assumed to have
    been created by an earlier invocation, and will be used for
    comparisons instead of release_branch.  When the script is done, this
    branch will NOT be pushed. The user must manually push the branch and
    create a pull request.

new_version: OPTIONAL. If provided, will update the version string in
    BuildInfo.cpp to this value, and create a version_setting commit.

USAGE
    if [[ $# -ne 0 ]]; then
        echo "Error: ${@}"
        echo
    fi

    exit 1
}

if [[ $# -lt 3 || $# -gt 4 ]]; then
    _usage
fi

release_branch="$1"
shift

develop_branch="$1"
shift

working_branch="$1"
shift

if [[ $# -ne 0 ]]; then
    new_version="$1"
    shift
fi

_run()
{
    echo
    echo "\$ ${@}"
    "${@}"
}

if git rev-parse --quiet --verify --end-of-options "refs/heads/${working_branch}" > /dev/null; then
    release_branch="${working_branch}"
    _run git checkout "${working_branch}"
else
    _run git checkout --no-track -B "${working_branch}" "${release_branch}"
fi


# Fetch if it hasn't been done in a while
if find "$( git rev-parse --git-dir )/FETCH_HEAD" -type f -mmin +10 | grep -q "FETCH"; then
    _run git fetch --all
fi

temp=$( mktemp )

# The "word-diff" option doesn't seem to have any meaningful effect on the output.
# This may make this script fragile across git versions with differing output formats.
_run git range-diff --word-diff=porcelain --no-patch --right-only \
    "${release_branch}"..."${develop_branch}" | tee "${temp}"

first_commit=$( cat "${temp}" | \
    awk '( $1 == "-:" && $2 ~ /-+/ && $3 == ">" ) { print $5; exit; }'  )

if [[ "${first_commit}" == "" && "${new_version}" == "" ]]; then
    _usage No divergence found
    exit 1
fi

if [[ "${first_commit}" == "" ]]; then
    echo -e "\nNo divergence found. Continue to update version number"
else
    echo -e "\nFound first unique commit in ${develop_branch}: ${first_commit}"

    first_commit=$( git rev-parse --quiet "${first_commit}" )

    echo "Full commit ID is: ${first_commit}"

    merge_base=$( git merge-base --octopus "${first_commit}" "${develop_branch}" )
    if [[ "${first_commit}" != "${merge_base}" ]]; then
        echo "Bad first commit: ${first_commit} is not an ancestor of ${develop_branch}"
        exit 1
    fi

    _run git diff "${first_commit}~" "${release_branch}"

    read -n 1 -s -p "Does the diff look reasonable? (y to continue / any other key to abort)" continue
    echo
    if [[ ! ( "${continue}" =~ [yY] ) ]]; then
        echo Abort
        exit 1
    fi

    # Dilemma: if the cherry-pick is interrupted, for example, because of a conflict, the script
    # will abort, and can not easily be resumed. This will prevent the final step(s) from running.
    if ! _run git cherry-pick --empty=drop "${first_commit}~..${develop_branch}"; then
        # Solution: if the cherry-pick is interrupted, run the command again.
        if [[ "${new_version}" != "" ]]; then
            echo
            echo "Cherry pick failed or was interrupted. Resolve issues, finish the"
            echo "  cherry pick, then run this script again to with the same parameters"
            echo "  to update the version number."
        fi
        exit 1
    fi

    if [[ "${new_version}" == "" ]]; then
        # If there is no version number change to make, we're done.
        exit 0
    fi
fi

# There should only be one matching file, but loop in case not.
buildfiles=( $( git ls-files | grep "BuildInfo.cpp" ) )

for build in "${buildfiles[@]}"; do
    sed 's/\(^.*versionString =\).*$/\1 "'${new_version}'"/' ${build} > version.cpp && \
    _run diff "${build}" version.cpp && exit 1 || \
    mv -vi version.cpp ${build}

    git add ${build}
done

_run git commit -S -m "Set version to ${new_version}"

_run git log --oneline --first-parent ${release_branch}^..

cat << PUSH

-------------------------------------------------------------------
This script will not push to remote. Verify everything is correct,
then push, and create a PR as described in CONTRIBUTING.md.
-------------------------------------------------------------------
PUSH
