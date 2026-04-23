#!/bin/bash

if [[ $# -ne 1 || "$1" == "--help" || "$1" == "-h" ]]
then
  name=$( basename $0 )
  cat <<- USAGE
  Usage: $name <username>

  Where <username> is the Github username of the upstream repo. e.g. XRPLF
USAGE
  exit 0
fi

# Create upstream remotes based on origin
shift
user="$1"
# Get the origin URL. Expect it be an SSH-style URL
origin=$( git remote get-url origin )
if [[ "${origin}" == "" ]]
then
  echo Invalid origin remote >&2
  exit 1
fi
# echo "Origin: ${origin}"
# Parse the origin
ifs_orig="${IFS}"
IFS=':' read remote originpath <<< "${origin}"
# echo "Remote: ${remote}, Originpath: ${originpath}"
IFS='@' read sshuser server <<< "${remote}"
# echo "SSHUser: ${sshuser}, Server: ${server}"
IFS='/' read originuser repo <<< "${originpath}"
# echo "Originuser: ${originuser}, Repo: ${repo}"
if [[ "${sshuser}" == "" || "${server}" == "" || "${originuser}" == ""
  || "${repo}" == "" ]]
then
  echo "Can't parse origin URL: ${origin}" >&2
  exit 1
fi
upstream="https://${server}/${user}/${repo}"
upstreampush="${remote}:${user}/${repo}"
upstreamgroup="upstream upstream-push"
current=$( git remote get-url upstream 2>/dev/null )
currentpush=$( git remote get-url upstream-push 2>/dev/null )
currentgroup=$( git config remotes.upstreams )
if [[ "${current}" == "${upstream}" ]]
then
  echo "Upstream already set up correctly. Skip"
elif [[ -n "${current}" && "${current}" != "${upstream}" &&
  "${current}" != "${upstreampush}" ]]
then
  echo "Upstream already set up as: ${current}. Skip"
else
  if [[ "${current}" == "${upstreampush}" ]]
  then
    echo "Upstream set to dangerous push URL. Update."
    _run git remote rename upstream upstream-push || \
    _run git remote remove upstream
    currentpush=$( git remote get-url upstream-push 2>/dev/null )
  fi
  _run git remote add upstream "${upstream}"
fi

if [[ "${currentpush}" == "${upstreampush}" ]]
then
  echo "upstream-push already set up correctly. Skip"
elif [[ -n "${currentpush}" && "${currentpush}" != "${upstreampush}" ]]
then
  echo "upstream-push already set up as: ${currentpush}. Skip"
else
  _run git remote add upstream-push "${upstreampush}"
fi

if [[ "${currentgroup}" == "${upstreamgroup}" ]]
then
  echo "Upstreams group already set up correctly. Skip"
elif [[ -n "${currentgroup}" && "${currentgroup}" != "${upstreamgroup}" ]]
then
  echo "Upstreams group already set up as: ${currentgroup}. Skip"
else
  _run git config --add remotes.upstreams "${upstreamgroup}"
fi

_run git fetch --jobs=$(nproc) upstreams

exit 0
