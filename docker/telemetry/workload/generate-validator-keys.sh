#!/usr/bin/env bash
# generate-validator-keys.sh — Generate validator key pairs for the workload harness.
#
# Uses a temporary standalone xrpld instance to call `validation_create` RPC
# for each node. Outputs a JSON file mapping node index to seed + public key.
#
# Production does this differently, and deliberately not the way this script
# does. There, `validator-keys-tool` runs `create_keys` once to make a master
# key that never leaves the operator's custody, then `create_token` to mint a
# revocable `[validator_token]` for the node; rotating a node means minting a
# new token, not moving the master key. This harness uses `validation_create`
# instead, which puts the seed itself in `[validation_seed]` on the node.
#
# That is safe here only because the cluster is disposable: the keys are made on
# the same machine that runs the nodes, into a temp workdir that the next run
# deletes, so there is no custody boundary for the two-step split to protect.
# The tool is also not present on this path -- it is built only with
# `-Dvalidator_keys=ON`, which the telemetry CI job does not pass. Do not copy
# this script's approach to a node holding a key you care about.
#
# Usage:
#   ./generate-validator-keys.sh <xrpld_binary> <num_nodes> <output_dir>
#
# Output:
#   <output_dir>/validator-keys.json — JSON array of {index, seed, public_key}
#   <output_dir>/validators.txt     — [validators] section for xrpld.cfg

set -euo pipefail

# ---------------------------------------------------------------------------
# Colored output helpers
# ---------------------------------------------------------------------------
log() { printf "\033[1;34m[KEYGEN]\033[0m %s\n" "$*"; }
ok() { printf "\033[1;32m[KEYGEN]\033[0m %s\n" "$*"; }
die() {
    printf "\033[1;31m[KEYGEN]\033[0m %s\n" "$*" >&2
    exit 1
}

# ---------------------------------------------------------------------------
# Argument parsing
# ---------------------------------------------------------------------------
usage() {
    echo "Usage: $0 <xrpld_binary> <num_nodes> <output_dir>"
    echo ""
    echo "Arguments:"
    echo "  xrpld_binary  Path to xrpld binary (built with telemetry=ON)"
    echo "  num_nodes     Number of validator key pairs to generate (1-20)"
    echo "  output_dir    Directory to write validator-keys.json and validators.txt"
    exit 1
}

if [ $# -lt 3 ]; then
    usage
fi

XRPLD="$1"
NUM_NODES="$2"
OUTPUT_DIR="$3"

# Validate arguments
[ -x "$XRPLD" ] || die "xrpld binary not found or not executable: $XRPLD"
[[ "$NUM_NODES" =~ ^[0-9]+$ ]] || die "num_nodes must be a positive integer"
[ "$NUM_NODES" -ge 1 ] && [ "$NUM_NODES" -le 20 ] || die "num_nodes must be between 1 and 20"

mkdir -p "$OUTPUT_DIR"

# ---------------------------------------------------------------------------
# Start a temporary standalone xrpld for key generation
# ---------------------------------------------------------------------------
TEMP_DIR="$(mktemp -d)"
TEMP_PORT=5099
TEMP_CFG="$TEMP_DIR/xrpld.cfg"

# Hard ceiling on every RPC probe below. curl applies no overall timeout of its
# own, so a node that accepts the connection and then stops answering parks the
# poll loop for the rest of the run. The loops here count attempts, not seconds,
# so without this their stated timeouts are not bounds at all.
CURL_MAX_TIME="${CURL_MAX_TIME:-5}"

log "Starting temporary xrpld for key generation (port $TEMP_PORT)..."

cat >"$TEMP_CFG" <<EOCFG
[server]
port_rpc_keygen

[port_rpc_keygen]
port = $TEMP_PORT
ip = 127.0.0.1
admin = 127.0.0.1
protocol = http

[node_db]
type=NuDB
path=$TEMP_DIR/nudb
online_delete=256

[database_path]
$TEMP_DIR/db

[debug_logfile]
$TEMP_DIR/debug.log

[ssl_verify]
0
EOCFG

"$XRPLD" --conf "$TEMP_CFG" -a --start >"$TEMP_DIR/stdout.log" 2>&1 &
TEMP_PID=$!

# Ensure cleanup on exit
cleanup_temp() {
    kill "$TEMP_PID" 2>/dev/null || true
    wait "$TEMP_PID" 2>/dev/null || true
    rm -rf "$TEMP_DIR"
}
trap cleanup_temp EXIT

# Wait for RPC to become available
for attempt in $(seq 1 30); do
    if curl -sf --max-time "$CURL_MAX_TIME" "http://localhost:$TEMP_PORT" \
        -d '{"method":"server_info"}' >/dev/null 2>&1; then
        log "Temporary xrpld RPC ready (attempt $attempt)."
        break
    fi
    if [ "$attempt" -eq 30 ]; then
        die "Temporary xrpld RPC not ready after 30s"
    fi
    sleep 1
done

# ---------------------------------------------------------------------------
# Generate key pairs
# ---------------------------------------------------------------------------
log "Generating $NUM_NODES validator key pairs..."

KEYS_JSON="["
VALIDATORS_TXT="[validators]"

for i in $(seq 1 "$NUM_NODES"); do
    result=$(curl -sf --max-time "$CURL_MAX_TIME" "http://localhost:$TEMP_PORT" \
        -d '{"method":"validation_create"}')
    seed=$(echo "$result" | jq -r '.result.validation_seed')
    pubkey=$(echo "$result" | jq -r '.result.validation_public_key')

    # Both fields must be present. jq -r prints the literal string "null" for
    # a missing field, so an unvalidated pubkey would be written verbatim into
    # validators.txt and xrpld would reject the file at startup.
    if [ -z "$seed" ] || [ "$seed" = "null" ]; then
        die "Failed to generate key pair for node $i: no validation_seed in response"
    fi
    if [ -z "$pubkey" ] || [ "$pubkey" = "null" ]; then
        die "Failed to generate key pair for node $i: no validation_public_key in response"
    fi

    log "  Node $i: ${pubkey:0:20}..."

    # Build JSON entry
    entry="{\"index\": $i, \"seed\": \"$seed\", \"public_key\": \"$pubkey\"}"
    if [ "$i" -gt 1 ]; then
        KEYS_JSON="$KEYS_JSON,"
    fi
    KEYS_JSON="$KEYS_JSON$entry"

    VALIDATORS_TXT="$VALIDATORS_TXT
$pubkey"
done

KEYS_JSON="$KEYS_JSON]"

# ---------------------------------------------------------------------------
# Write output files
# ---------------------------------------------------------------------------
echo "$KEYS_JSON" | jq '.' >"$OUTPUT_DIR/validator-keys.json"
echo "$VALIDATORS_TXT" >"$OUTPUT_DIR/validators.txt"

ok "Generated $NUM_NODES key pairs:"
ok "  Keys:       $OUTPUT_DIR/validator-keys.json"
ok "  Validators: $OUTPUT_DIR/validators.txt"
