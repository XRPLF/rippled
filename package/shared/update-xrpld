#!/usr/bin/env bash
set -euo pipefail

# Optional: also write logs to a legacy file in addition to journald.
# By default, this script logs to systemd/journald, viewable via:
#   journalctl -t update-xrpld
#
# Uncomment the line below if you need a flat file for compatibility with
# external tooling, manual inspection, or environments where journald logs
# are not persisted or easily accessible.
#
# Note: This duplicates all output (stdout/stderr) to both journald and the file.
# It is generally not needed on modern systems and may cause log file growth
# if left enabled long-term.
#
# Requires /var/log/xrpld/ to exist and be writable by the service (root).
#
# exec > >(tee -a /var/log/xrpld/update.log) 2>&1

PATH=/usr/sbin:/usr/bin:/sbin:/bin

PKG_NAME=${PKG_NAME:-xrpld}

log() {
# If running under systemd/journald, let it handle timestamps.
    if [[ -n "${JOURNAL_STREAM:-}" ]]; then
        printf '%s\n' "$*"
    else
        printf '%s %s\n' "$(date -u +'%Y-%m-%dT%H:%M:%SZ')" "$*"
    fi
}

require_root() {
    if [[ ${EUID:-$(id -u)} -ne 0 ]]; then
        log "RESULT: failed reason=not-root"
            exit 1
    fi
}

get_installed_version() {
    if command -v dpkg-query >/dev/null 2>&1; then
        dpkg-query -W -f='${Version}' "$PKG_NAME" 2>/dev/null || printf 'unknown'
    elif command -v rpm >/dev/null 2>&1; then
        rpm -q --qf '%{VERSION}-%{RELEASE}' "$PKG_NAME" 2>/dev/null || printf 'unknown'
    else
        printf 'unknown'
    fi
}

trap 'log "RESULT: failed reason=script-error exit_code=$?"' ERR

apt_can_update() {
    apt-get update -qq
    apt-get -s --only-upgrade install "$PKG_NAME" 2>/dev/null | grep -q "^Inst ${PKG_NAME}\b"
}

apt_apply_update() {
    DEBIAN_FRONTEND=noninteractive apt-get install -y -qq \
        -o Dpkg::Options::="--force-confdef" \
        -o Dpkg::Options::="--force-confold" \
        "$PKG_NAME"
}

get_rpm_pm() {
    if command -v dnf >/dev/null 2>&1; then
        printf 'dnf\n'
    elif command -v yum >/dev/null 2>&1; then
        printf 'yum\n'
    else
        return 1
    fi
}

rpm_refresh_metadata() {
    local pm=$1
    if [[ "$pm" == "dnf" ]]; then
        dnf makecache --refresh -q >/dev/null
    else
        yum clean expire-cache -q >/dev/null
    fi
}

rpm_can_update() {
    local pm=$1

    rpm_refresh_metadata "$pm"
    local rc=0
    set +e
    "$pm" check-update -q "$PKG_NAME" >/dev/null 2>&1
    rc=$?
    set -e

    if [[ $rc -eq 100 ]]; then
        return 0
    elif [[ $rc -eq 0 ]]; then
        return 1
    else
        log "$pm check-update failed with exit code ${rc}."
        exit 1
    fi
}

rpm_apply_update() {
    local pm=$1
    "$pm" update -y "$PKG_NAME"
}

restart_service() {
    # Preserve the operator's prior service state: if xrpld was intentionally
    # stopped before the update, don't bring it back up just because the
    # auto-update timer fired.
    if systemctl is-active --quiet "${PKG_NAME}.service"; then
        systemctl restart "${PKG_NAME}.service"
        log "${PKG_NAME} service restarted successfully."
    else
        log "${PKG_NAME} service was not running; skipping restart to preserve prior state."
    fi
}

main() {
    require_root
    if command -v apt-get >/dev/null 2>&1; then
        log "Checking for ${PKG_NAME} updates via apt"
        if apt_can_update; then
            log "Update available; installing."
            apt_apply_update
            restart_service
            log "RESULT: updated ${PKG_NAME}=$(get_installed_version)"
        else
            log "RESULT: no-update ${PKG_NAME}=$(get_installed_version)"
        fi
        return
    fi

    local rpm_pm=""
    if rpm_pm="$(get_rpm_pm)"; then
        log "Checking for ${PKG_NAME} updates via ${rpm_pm}"
        if rpm_can_update "$rpm_pm"; then
            log "Update available; installing"
            rpm_apply_update "$rpm_pm"
            restart_service
            log "RESULT: updated ${PKG_NAME}=$(get_installed_version)"
        else
            log "RESULT: no-update ${PKG_NAME}=$(get_installed_version)"
        fi
        return
    fi
    log "RESULT: failed reason=no-package-manager"
    exit 1
}

main "$@"
