#!/usr/bin/env bash

# auto-update script for rippled daemon

# Check for sudo/root permissions
if [[ $(id -u) -ne 0 ]] ; then
   echo "This update script must be run as root or sudo"
   exit 1
fi

LOCKDIR=/tmp/rippleupdate.lock
UPDATELOG=/var/log/rippled/update.log

function cleanup {
  # If this directory isn't removed, future updates will fail.
  rmdir $LOCKDIR
}

# Use mkdir to check if process is already running. mkdir is atomic, as against file create.
if ! mkdir $LOCKDIR 2>/dev/null; then
  echo $(date -u) "lockdir exists - won't proceed." >> $UPDATELOG
  exit 1
fi
trap cleanup EXIT

source /etc/os-release
can_update=false

if [[ "$ID" == "ubuntu" || "$ID" == "debian" ]] ; then
  # Silent update
  apt-get update -qq

  # The next line is an "awk"ward way to check if the package needs to be updated.
  RIPPLE=$(apt-get install -s --only-upgrade rippled | awk '/^Inst/ { print $2 }')
  test "$RIPPLE" == "rippled" && can_update=true

  function apply_update {
    apt-get install rippled -qq
  }
elif [[ "$ID" == "fedora" || "$ID" == "centos" || "$ID" == "rhel" || "$ID" == "scientific" ]] ; then
  RIPPLE_REPO=${RIPPLE_REPO-stable}
  yum --disablerepo=* --enablerepo=ripple-$RIPPLE_REPO clean expire-cache

  yum check-update -q --enablerepo=ripple-$RIPPLE_REPO rippled || can_update=true

  function apply_update {
    yum update -y --enablerepo=ripple-$RIPPLE_REPO rippled
  }
else
  echo "unrecognized distro!"
  exit 1
fi

# Do the actual update and restart the service after reloading systemctl daemon.
if [ "$can_update" = true ] ; then
  exec 3>&1 1>>${UPDATELOG} 2>&1
  set -e
  apply_update
  systemctl daemon-reload
  systemctl restart rippled.service
  echo $(date -u) "rippled daemon updated."
else
  echo $(date -u) "no updates available" >> $UPDATELOG
fi

