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

# Primary FHS install
check -x /usr/bin/xrpld
check -f /etc/xrpld/xrpld.cfg
check -f /etc/xrpld/validators.txt
check -f /etc/logrotate.d/xrpld
check -x /usr/libexec/xrpld/update-xrpld.sh
check -f /usr/share/doc/xrpld/README.md
check -f /usr/share/doc/xrpld/LICENSE.md

# Legacy compat symlinks (remove next major release)
check -L /usr/local/bin/rippled
check_resolves_to /usr/local/bin/rippled /usr/bin/xrpld
check -L /etc/xrpld/rippled.cfg
check_resolves_to /etc/xrpld/rippled.cfg /etc/xrpld/xrpld.cfg

if systemctl is-system-running >/dev/null 2>&1; then
    # service file sanity check
    SERVICE=$(systemctl cat xrpld)
    echo "$SERVICE" | grep -q 'ExecStart=/usr/bin/xrpld' || { echo "FAIL: ExecStart wrong"; echo "$SERVICE"; exit 1; }
    echo "$SERVICE" | grep -q 'User=xrpld' || { echo "FAIL: User not xrpld"; echo "$SERVICE"; exit 1; }
fi

# Binary accessible via all expected paths
/usr/bin/xrpld         --version
/usr/local/bin/rippled --version
