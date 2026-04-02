#!/usr/bin/env bash

# auto-update script for xrpld daemon

# Check for sudo/root permissions
if [[ $(id -u) -ne 0 ]] ; then
   echo "This update script must be run as root or sudo"
   exit 1
fi

LOCKDIR=/run/lock/xrpld-update.lock
UPDATELOG=/var/log/xrpld/update.log

function cleanup {
  # If this directory isn't removed, future updates will fail.
  rmdir "$LOCKDIR"
}

# Use mkdir to check if process is already running. mkdir is atomic, as against file create.
if ! mkdir "$LOCKDIR" 2>/dev/null; then
  echo "$(date -u) lockdir exists - won't proceed." >> "$UPDATELOG"
  exit 1
fi
trap cleanup EXIT

can_update=false

if command -v apt-get &>/dev/null; then
  apt-get update -qq

  if apt-get -s --only-upgrade install xrpld 2>/dev/null | grep -q '^Inst xrpld'; then
    can_update=true
  fi

  function apply_update {
    DEBIAN_FRONTEND=noninteractive apt-get install -y -qq xrpld
  }
elif command -v yum &>/dev/null; then
  REPO=${REPO:-stable}
  if [[ ! "$REPO" =~ ^(stable|unstable|nightly|develop)$ ]]; then
    echo "Invalid REPO value: ${REPO}" >&2
    exit 1
  fi
  yum --disablerepo=* --enablerepo="ripple-${REPO}" clean expire-cache

  # yum check-update exits 100 when updates are available, 0 for none, 1 for errors.
  yum check-update -q --enablerepo="ripple-${REPO}" xrpld
  rc=$?
  if [ $rc -eq 100 ]; then
    can_update=true
  elif [ $rc -ne 0 ]; then
    echo "yum check-update failed with exit code $rc"
    exit 1
  fi

  function apply_update {
    yum update -y --enablerepo="ripple-${REPO}" xrpld
  }
else
  echo "No supported package manager found (apt-get or yum)"
  exit 1
fi

# Do the actual update and restart the service after reloading systemctl daemon.
if [ "$can_update" = true ] ; then
  exec >>"${UPDATELOG}" 2>&1
  set -e
  apply_update
  systemctl daemon-reload
  systemctl restart xrpld.service || { echo "$(date -u) xrpld daemon restart FAILED"; exit 1; }
  echo "$(date -u) xrpld daemon updated."
else
  echo "$(date -u) no updates available" >> "$UPDATELOG"
fi
