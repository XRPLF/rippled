#!/usr/bin/env sh
# Validate installed paths and compat symlinks for xrpld packages.

set -e
set -x
trap 'test $? -ne 0 && touch /tmp/test_failed' EXIT

check() { test $1 "$2" || { echo "FAIL: $1 $2"; exit 1; }; }
check_resolves_to() {
    actual=$(readlink -f "$1")
    [ "$actual" = "$2" ] || { echo "FAIL: $1 resolves to $actual, expected $2"; exit 1; }
}

# compat directory symlinks — existence and resolved target
check -L /opt/ripple
check_resolves_to /opt/ripple /opt/xrpld

check -L /etc/opt/xrpld
check_resolves_to /etc/opt/xrpld /opt/xrpld/etc

check -L /etc/opt/ripple
check_resolves_to /etc/opt/ripple /opt/xrpld/etc

# config accessible via all expected paths
check -f /opt/xrpld/etc/xrpld.cfg
check -f /opt/xrpld/etc/rippled.cfg
check -f /etc/opt/xrpld/xrpld.cfg
check -f /etc/opt/xrpld/rippled.cfg
check -f /etc/opt/ripple/xrpld.cfg
check -f /etc/opt/ripple/rippled.cfg

if systemctl is-system-running >/dev/null 2>&1; then
    # service file sanity check
    SERVICE=$(systemctl cat xrpld)
    echo "$SERVICE" | grep -q 'ExecStart=/opt/xrpld/bin/xrpld' || { echo "FAIL: ExecStart wrong"; echo "$SERVICE"; exit 1; }
    echo "$SERVICE" | grep -q 'User=xrpld' || { echo "FAIL: User not xrpld"; echo "$SERVICE"; exit 1; }
fi

# binary accessible via all expected paths
/opt/xrpld/bin/xrpld    --version
/opt/xrpld/bin/rippled   --version
/opt/ripple/bin/xrpld    --version
/opt/ripple/bin/rippled  --version
/usr/bin/xrpld           --version
/usr/local/bin/rippled   --version
