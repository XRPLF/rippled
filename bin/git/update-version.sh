#!/bin/bash

if [[ $# -ne 3 || "$1" == "--help" || "$1" = "-h" ]]
then
  name=$( basename $0 )
  cat <<- USAGE
  Usage: $name workbranch base/branch version

  * workbranch will be created locally from base/branch. If it exists,
    it will be reused, so make sure you don't overwrite any work.
  * base/branch may be specified as user:branch to allow easy copying
    from Github PRs.
USAGE
exit 0
fi

work="$1"
shift

base=$( echo "$1" | sed "s/:/\//" )
shift

version=$1
shift

set -e

git fetch upstreams

git checkout -B "${work}" --no-track "${base}"

push=$( git rev-parse --abbrev-ref --symbolic-full-name '@{push}' \
              2>/dev/null ) || true
if [[ "${push}" != "" ]]
then
  echo "Warning: ${push} may already exist."
fi

build=$( find -name BuildInfo.cpp )
sed 's/\(^.*versionString =\).*$/\1 "'${version}'"/' ${build} > version.cpp && \
diff "${build}" version.cpp && exit 1 || \
mv -vi version.cpp ${build}

git diff

git add ${build}

git commit -S -m "Set version to ${version}"

git log --oneline --first-parent ${base}^..

cat << PUSH

-------------------------------------------------------------------
This script will not push. Verify everything is correct, then push
to your repo, and create a PR as described in CONTRIBUTING.md.
-------------------------------------------------------------------
PUSH
