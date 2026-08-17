#!/bin/sh
# Install the two-node systemd units, filling host-specific values from an
# untracked .env.devbox alongside the telemetry configs.
#
# The templates carry placeholders rather than real values because this
# repository is public: an earlier commit already removed a personal home
# directory from shipped config, and nothing host-identifying should return to
# git. .env.devbox holds those values on the host only and is covered by the
# .env.* ignore rule.
#
# Usage, from the repository root:
#   sh docker/telemetry/systemd/install-units.sh
#
# Written in POSIX sh so it runs under bash, dash and ash alike.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
tel=$(dirname "$here")
env_file="$tel/.env.devbox"

if [ ! -f "$env_file" ]; then
  echo "ERROR: $env_file not found." >&2
  echo "       Copy $tel/.env.devbox.example to it and fill in the values." >&2
  exit 1
fi

# Refuse a world-readable env file: it names the account xrpld runs as, and this
# script is the only thing that should be reading it.
mode=$(stat -c %a "$env_file" 2>/dev/null || echo "")
case "$mode" in
  600|400) : ;;
  "") echo "WARN: could not read permissions of $env_file" >&2 ;;
  *)  echo "ERROR: $env_file is mode $mode; expected 600. Run: chmod 600 $env_file" >&2; exit 1 ;;
esac

# shellcheck disable=SC1090
. "$env_file"

for var in RUN_USER REPO_DIR DATA_MOUNT; do
  eval "val=\${$var:-}"
  if [ -z "$val" ]; then
    echo "ERROR: $var is empty in $env_file" >&2
    exit 1
  fi
done

# Fail early on values that would produce a unit systemd silently never starts.
id "$RUN_USER" >/dev/null 2>&1 || { echo "ERROR: user '$RUN_USER' does not exist" >&2; exit 1; }
[ -d "$REPO_DIR" ] || { echo "ERROR: REPO_DIR '$REPO_DIR' is not a directory" >&2; exit 1; }
[ -x "$REPO_DIR/.build/xrpld" ] || echo "WARN: $REPO_DIR/.build/xrpld not built yet; the unit will fail to start until it is" >&2
[ -d "$DATA_MOUNT" ] || echo "WARN: DATA_MOUNT '$DATA_MOUNT' does not exist yet; the unit will refuse to start until it is mounted" >&2

for unit in xrpld-mainnet xrpld-mainnet2; do
  tpl="$here/$unit.service.template"
  [ -f "$tpl" ] || { echo "ERROR: missing template $tpl" >&2; exit 1; }
  out=$(mktemp)
  sed -e "s|__RUN_USER__|$RUN_USER|g" \
      -e "s|__REPO_DIR__|$REPO_DIR|g" \
      -e "s|__DATA_MOUNT__|$DATA_MOUNT|g" \
      "$tpl" > "$out"

  if grep -q '__[A-Z_]*__' "$out"; then
    echo "ERROR: unsubstituted placeholder left in $unit:" >&2
    grep -o '__[A-Z_]*__' "$out" | sort -u | sed 's/^/  /' >&2
    rm -f "$out"
    exit 1
  fi

  sudo install -m 0644 "$out" "/etc/systemd/system/$unit.service"
  rm -f "$out"
  echo "installed /etc/systemd/system/$unit.service"
done

sudo systemctl daemon-reload
echo "done. Start them staggered:"
echo "  sudo systemctl start xrpld-mainnet     # wait for tracking/full"
echo "  sudo systemctl start xrpld-mainnet2"
