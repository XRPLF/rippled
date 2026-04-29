#!/usr/bin/env bash
# Integration test for rippled OpenTelemetry instrumentation.
#
# Launches a 6-node xrpld consensus network with telemetry enabled,
# exercises RPC / transaction / consensus code paths, then verifies
# that the expected spans and metrics appear in Tempo and Prometheus.
#
# Usage:
#   bash docker/telemetry/integration-test.sh
#
# Prerequisites:
#   - .build/xrpld built with telemetry=ON
#   - docker compose (v2)
#   - curl, jq
#
# The script leaves the observability stack and xrpld nodes running
# so you can manually inspect Tempo (localhost:3200) and Grafana
# (localhost:3000). Run with --cleanup to tear down instead.

set -euo pipefail

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
XRPLD="$REPO_ROOT/.build/xrpld"
COMPOSE_FILE="$SCRIPT_DIR/docker-compose.yml"
STANDALONE_CFG="$SCRIPT_DIR/xrpld-telemetry.cfg"
WORKDIR="/tmp/xrpld-integration"
NUM_NODES=6
PEER_PORT_BASE=51235
RPC_PORT_BASE=5005
CONSENSUS_TIMEOUT=120
GENESIS_ACCOUNT="rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh"
GENESIS_SEED="snoPBrXtMeMyMHUVTgbuqAfg1SUTb"
DEST_ACCOUNT=""  # Generated dynamically via wallet_propose
TEMPO="http://localhost:3200"
PROM="http://localhost:9090"

# Counters for pass/fail
PASS=0
FAIL=0

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
log()  { printf "\033[1;34m[INFO]\033[0m  %s\n" "$*"; }
ok()   { printf "\033[1;32m[PASS]\033[0m  %s\n" "$*"; PASS=$((PASS + 1)); }
fail() { printf "\033[1;31m[FAIL]\033[0m  %s\n" "$*"; FAIL=$((FAIL + 1)); }
die()  { printf "\033[1;31m[ERROR]\033[0m %s\n" "$*" >&2; exit 1; }

check_span() {
    local op="$1"
    local count
    count=$(curl -sf "$TEMPO/api/search" \
        --data-urlencode "q={resource.service.name=\"rippled\" && name=\"$op\"}" \
        --data-urlencode "limit=5" \
        | jq '.traces | length' 2>/dev/null || echo 0)
    if [ "$count" -gt 0 ]; then
        ok "$op  ($count traces)"
    else
        fail "$op  (0 traces)"
    fi
}

# Phase 8: Verify trace_id injection in rippled log output.
# Greps all node debug.log files for the "trace_id=<hex> span_id=<hex>"
# pattern that Logs::format() injects when an active OTel span exists.
# Also cross-checks that a trace_id found in logs matches a trace in Jaeger.
check_log_correlation() {
    log "Checking log-trace correlation..."

    local total_matches=0
    local sample_trace_id=""

    for i in $(seq 1 "$NUM_NODES"); do
        local logfile="$WORKDIR/node$i/debug.log"
        if [ ! -f "$logfile" ]; then
            continue
        fi
        local matches
        matches=$(grep -c 'trace_id=[a-f0-9]\{32\} span_id=[a-f0-9]\{16\}' "$logfile" 2>/dev/null || echo 0)
        total_matches=$((total_matches + matches))
        # Capture the first trace_id we find for cross-referencing with Jaeger
        if [ -z "$sample_trace_id" ] && [ "$matches" -gt 0 ]; then
            sample_trace_id=$(grep -o 'trace_id=[a-f0-9]\{32\}' "$logfile" | head -1 | cut -d= -f2)
        fi
    done

    if [ "$total_matches" -gt 0 ]; then
        ok "Log correlation: found $total_matches log lines with trace_id"
    else
        fail "Log correlation: no trace_id found in any node debug.log"
    fi

    # Cross-check: verify the sample trace_id exists in Jaeger
    if [ -n "$sample_trace_id" ]; then
        local trace_found
        trace_found=$(curl -sf "$JAEGER/api/traces/$sample_trace_id" \
            | jq '.data | length' 2>/dev/null || echo 0)
        if [ "$trace_found" -gt 0 ]; then
            ok "Log-Jaeger cross-check: trace_id=$sample_trace_id found in Jaeger"
        else
            fail "Log-Jaeger cross-check: trace_id=$sample_trace_id NOT found in Jaeger"
        fi
    fi
}

cleanup() {
    log "Cleaning up..."
    # Kill xrpld nodes
    for i in $(seq 1 "$NUM_NODES"); do
        local pidfile="$WORKDIR/node$i/xrpld.pid"
        if [ -f "$pidfile" ]; then
            kill "$(cat "$pidfile")" 2>/dev/null || true
            rm -f "$pidfile"
        fi
    done
    # Also kill any straggling xrpld processes from our workdir
    pkill -f "$WORKDIR" 2>/dev/null || true
    # Stop docker stack
    docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true
    # Remove workdir
    rm -rf "$WORKDIR"
    log "Cleanup complete."
}

# Handle --cleanup flag
if [ "${1:-}" = "--cleanup" ]; then
    cleanup
    exit 0
fi

# ---------------------------------------------------------------------------
# Step 0: Prerequisites
# ---------------------------------------------------------------------------
log "Checking prerequisites..."

command -v docker >/dev/null 2>&1 || die "docker not found"
docker compose version >/dev/null 2>&1 || die "docker compose (v2) not found"
command -v curl >/dev/null 2>&1 || die "curl not found"
command -v jq >/dev/null 2>&1 || die "jq not found"
[ -x "$XRPLD" ] || die "xrpld binary not found at $XRPLD (build with telemetry=ON)"
[ -f "$COMPOSE_FILE" ] || die "docker-compose.yml not found at $COMPOSE_FILE"
[ -f "$STANDALONE_CFG" ] || die "xrpld-telemetry.cfg not found at $STANDALONE_CFG"

log "All prerequisites met."

# ---------------------------------------------------------------------------
# Step 1: Clean previous run
# ---------------------------------------------------------------------------
log "Cleaning previous run data..."
for i in $(seq 1 "$NUM_NODES"); do
    pidfile="$WORKDIR/node$i/xrpld.pid"
    if [ -f "$pidfile" ]; then
        kill "$(cat "$pidfile")" 2>/dev/null || true
    fi
done
pkill -f "$WORKDIR" 2>/dev/null || true
# Kill any xrpld using the standalone config (from key generation)
pkill -f "xrpld-telemetry.cfg" 2>/dev/null || true
sleep 2
rm -rf "$WORKDIR"
mkdir -p "$WORKDIR"

# ---------------------------------------------------------------------------
# Step 2: Start observability stack
# ---------------------------------------------------------------------------
log "Starting observability stack..."
docker compose -f "$COMPOSE_FILE" up -d

log "Waiting for otel-collector to be ready..."
for attempt in $(seq 1 30); do
    # The OTLP HTTP endpoint returns 405 for GET (expects POST), which
    # means it is listening.  curl -sf would fail on 405, so we check
    # the HTTP status code explicitly.
    status=$(curl -so /dev/null -w '%{http_code}' http://localhost:4318/ 2>/dev/null || echo 000)
    if [ "$status" != "000" ]; then
        log "otel-collector ready (attempt $attempt, HTTP $status)."
        break
    fi
    if [ "$attempt" -eq 30 ]; then
        die "otel-collector not ready after 30s"
    fi
    sleep 1
done

log "Waiting for Tempo to be ready..."
for attempt in $(seq 1 30); do
    if curl -sf "$TEMPO/ready" >/dev/null 2>&1; then
        log "Tempo ready (attempt $attempt)."
        break
    fi
    if [ "$attempt" -eq 30 ]; then
        die "Tempo not ready after 30s"
    fi
    sleep 1
done

# ---------------------------------------------------------------------------
# Step 3: Generate validator keys
# ---------------------------------------------------------------------------
log "Generating $NUM_NODES validator key pairs..."

# Start a temporary standalone xrpld for key generation
TEMP_DATA="$WORKDIR/temp-keygen"
mkdir -p "$TEMP_DATA"

# Create a minimal temp config for key generation
TEMP_CFG="$TEMP_DATA/xrpld.cfg"
cat > "$TEMP_CFG" <<EOCFG
[server]
port_rpc_temp

[port_rpc_temp]
port = 5099
ip = 127.0.0.1
admin = 127.0.0.1
protocol = http

[node_db]
type=NuDB
path=$TEMP_DATA/nudb
online_delete=256

[database_path]
$TEMP_DATA/db

[debug_logfile]
$TEMP_DATA/debug.log

[ssl_verify]
0
EOCFG

"$XRPLD" --conf "$TEMP_CFG" -a --start > "$TEMP_DATA/stdout.log" 2>&1 &
TEMP_PID=$!
log "Temporary xrpld started (PID $TEMP_PID), waiting for RPC..."

for attempt in $(seq 1 30); do
    if curl -sf http://localhost:5099 -d '{"method":"server_info"}' >/dev/null 2>&1; then
        log "Temporary xrpld RPC ready (attempt $attempt)."
        break
    fi
    if [ "$attempt" -eq 30 ]; then
        kill "$TEMP_PID" 2>/dev/null || true
        die "Temporary xrpld RPC not ready after 30s"
    fi
    sleep 1
done

declare -a SEEDS
declare -a PUBKEYS

for i in $(seq 1 "$NUM_NODES"); do
    result=$(curl -sf http://localhost:5099 -d '{"method":"validation_create"}')
    seed=$(echo "$result" | jq -r '.result.validation_seed')
    pubkey=$(echo "$result" | jq -r '.result.validation_public_key')
    if [ -z "$seed" ] || [ "$seed" = "null" ]; then
        kill "$TEMP_PID" 2>/dev/null || true
        die "Failed to generate key pair $i"
    fi
    SEEDS+=("$seed")
    PUBKEYS+=("$pubkey")
    log "  Node $i: $pubkey"
done

kill "$TEMP_PID" 2>/dev/null || true
wait "$TEMP_PID" 2>/dev/null || true
rm -rf "$TEMP_DATA"
log "Key generation complete."

# ---------------------------------------------------------------------------
# Step 4: Generate node configs and validators.txt
# ---------------------------------------------------------------------------
log "Generating node configs..."

# Create shared validators.txt
VALIDATORS_FILE="$WORKDIR/validators.txt"
{
    echo "[validators]"
    for i in $(seq 0 $((NUM_NODES - 1))); do
        echo "${PUBKEYS[$i]}"
    done
} > "$VALIDATORS_FILE"

# Create per-node configs
for i in $(seq 1 "$NUM_NODES"); do
    NODE_DIR="$WORKDIR/node$i"
    mkdir -p "$NODE_DIR/nudb" "$NODE_DIR/db"

    RPC_PORT=$((RPC_PORT_BASE + i - 1))
    PEER_PORT=$((PEER_PORT_BASE + i - 1))
    SEED="${SEEDS[$((i - 1))]}"

    # Build ips_fixed list (all peers except self)
    IPS_FIXED=""
    for j in $(seq 1 "$NUM_NODES"); do
        if [ "$j" -ne "$i" ]; then
            IPS_FIXED="${IPS_FIXED}127.0.0.1 $((PEER_PORT_BASE + j - 1))
"
        fi
    done

    cat > "$NODE_DIR/xrpld.cfg" <<EOCFG
[server]
port_rpc
port_peer

[port_rpc]
port = $RPC_PORT
ip = 127.0.0.1
admin = 127.0.0.1
protocol = http

[port_peer]
port = $PEER_PORT
ip = 0.0.0.0
protocol = peer

[node_db]
type=NuDB
path=$NODE_DIR/nudb
online_delete=256

[database_path]
$NODE_DIR/db

[debug_logfile]
$NODE_DIR/debug.log

[validation_seed]
$SEED

[validators_file]
$VALIDATORS_FILE

[ips_fixed]
${IPS_FIXED}
[peer_private]
1

[telemetry]
enabled=1
service_instance_id=Node-${i}
endpoint=http://localhost:4318/v1/traces
exporter=otlp_http
sampling_ratio=1.0
batch_size=512
batch_delay_ms=2000
max_queue_size=2048
trace_rpc=1
trace_transactions=1
trace_consensus=1
trace_peer=1
trace_ledger=1
metrics_endpoint=http://localhost:4318/v1/metrics

[insight]
server=statsd
address=127.0.0.1:8125
prefix=rippled

[rpc_startup]
{ "command": "log_level", "severity": "warning" }

[ssl_verify]
0
EOCFG

    log "  Node $i config: RPC=$RPC_PORT, Peer=$PEER_PORT"
done

# ---------------------------------------------------------------------------
# Step 5: Start all 6 nodes
# ---------------------------------------------------------------------------
log "Starting $NUM_NODES xrpld nodes..."

for i in $(seq 1 "$NUM_NODES"); do
    NODE_DIR="$WORKDIR/node$i"
    "$XRPLD" --conf "$NODE_DIR/xrpld.cfg" --start > "$NODE_DIR/stdout.log" 2>&1 &
    echo $! > "$NODE_DIR/xrpld.pid"
    log "  Node $i started (PID $(cat "$NODE_DIR/xrpld.pid"))"
done

# Give nodes a moment to initialize
sleep 5

# ---------------------------------------------------------------------------
# Step 6: Wait for consensus
# ---------------------------------------------------------------------------
log "Waiting for nodes to reach 'proposing' state (timeout: ${CONSENSUS_TIMEOUT}s)..."

start_time=$(date +%s)
nodes_ready=0

while [ "$nodes_ready" -lt "$NUM_NODES" ]; do
    elapsed=$(( $(date +%s) - start_time ))
    if [ "$elapsed" -ge "$CONSENSUS_TIMEOUT" ]; then
        fail "Consensus timeout after ${CONSENSUS_TIMEOUT}s ($nodes_ready/$NUM_NODES nodes ready)"
        log "Continuing with partial consensus..."
        break
    fi

    nodes_ready=0
    for i in $(seq 1 "$NUM_NODES"); do
        RPC_PORT=$((RPC_PORT_BASE + i - 1))
        state=$(curl -sf "http://localhost:$RPC_PORT" \
            -d '{"method":"server_info"}' 2>/dev/null \
            | jq -r '.result.info.server_state' 2>/dev/null || echo "unreachable")
        if [ "$state" = "proposing" ]; then
            nodes_ready=$((nodes_ready + 1))
        fi
    done
    printf "\r  %d/%d nodes proposing (%ds elapsed)..." "$nodes_ready" "$NUM_NODES" "$elapsed"
    if [ "$nodes_ready" -lt "$NUM_NODES" ]; then
        sleep 3
    fi
done
echo ""

if [ "$nodes_ready" -eq "$NUM_NODES" ]; then
    ok "All $NUM_NODES nodes reached 'proposing' state"
else
    fail "Only $nodes_ready/$NUM_NODES nodes reached 'proposing' state"
fi

# ---------------------------------------------------------------------------
# Step 6b: Wait for validated ledger
# ---------------------------------------------------------------------------
log "Waiting for first validated ledger..."
for attempt in $(seq 1 60); do
    val_seq=$(curl -sf "http://localhost:$RPC_PORT_BASE" \
        -d '{"method":"server_info"}' 2>/dev/null \
        | jq -r '.result.info.validated_ledger.seq // 0' 2>/dev/null || echo 0)
    if [ "$val_seq" -gt 2 ] 2>/dev/null; then
        ok "First validated ledger: seq $val_seq"
        break
    fi
    if [ "$attempt" -eq 60 ]; then
        fail "No validated ledger after 60s"
    fi
    sleep 1
done

# ---------------------------------------------------------------------------
# Step 7: Exercise RPC spans (Phase 2)
# ---------------------------------------------------------------------------
log "Exercising RPC spans..."

curl -sf "http://localhost:$RPC_PORT_BASE" \
    -d '{"method":"server_info"}' > /dev/null
curl -sf "http://localhost:$RPC_PORT_BASE" \
    -d '{"method":"server_state"}' > /dev/null
curl -sf "http://localhost:$RPC_PORT_BASE" \
    -d '{"method":"ledger","params":[{"ledger_index":"current"}]}' > /dev/null

log "RPC commands sent. Waiting 5s for batch export..."
sleep 5

# ---------------------------------------------------------------------------
# Step 8: Submit transaction (Phase 3)
# ---------------------------------------------------------------------------
log "Submitting Payment transaction..."

# Generate a destination wallet
log "  Generating destination wallet..."
wallet_result=$(curl -sf "http://localhost:$RPC_PORT_BASE" \
    -d '{"method":"wallet_propose"}')
DEST_ACCOUNT=$(echo "$wallet_result" | jq -r '.result.account_id' 2>/dev/null)
if [ -z "$DEST_ACCOUNT" ] || [ "$DEST_ACCOUNT" = "null" ]; then
    fail "Could not generate destination wallet"
    DEST_ACCOUNT="rrrrrrrrrrrrrrrrrrrrrhoLvTp"  # ACCOUNT_ZERO fallback
fi
log "  Destination: $DEST_ACCOUNT"

# Get genesis account info
acct_result=$(curl -sf "http://localhost:$RPC_PORT_BASE" \
    -d "{\"method\":\"account_info\",\"params\":[{\"account\":\"$GENESIS_ACCOUNT\"}]}")
seq_num=$(echo "$acct_result" | jq -r '.result.account_data.Sequence' 2>/dev/null || echo "unknown")
log "  Genesis account sequence: $seq_num"

# Submit payment
submit_result=$(curl -sf "http://localhost:$RPC_PORT_BASE" \
    -d "{\"method\":\"submit\",\"params\":[{\"secret\":\"$GENESIS_SEED\",\"tx_json\":{\"TransactionType\":\"Payment\",\"Account\":\"$GENESIS_ACCOUNT\",\"Destination\":\"$DEST_ACCOUNT\",\"Amount\":\"10000000\"}}]}")

engine_result=$(echo "$submit_result" | jq -r '.result.engine_result' 2>/dev/null || echo "unknown")
tx_hash=$(echo "$submit_result" | jq -r '.result.tx_json.hash' 2>/dev/null || echo "unknown")

if [ "$engine_result" = "tesSUCCESS" ] || [ "$engine_result" = "terQUEUED" ]; then
    ok "Transaction submitted: $engine_result (hash: ${tx_hash:0:16}...)"
else
    fail "Transaction submission: $engine_result"
    log "  Full response: $(echo "$submit_result" | jq -c .result 2>/dev/null)"
fi

log "Waiting 15s for consensus round + batch export..."
sleep 15

# ---------------------------------------------------------------------------
# Step 9: Verify Tempo traces
# ---------------------------------------------------------------------------
log "Verifying spans in Tempo..."

# Check service registration
services=$(curl -sf "$TEMPO/api/v2/search/tag/resource.service.name/values" \
    | jq -r '.tagValues[].value' 2>/dev/null || echo "")
if echo "$services" | grep -q "rippled"; then
    ok "Service 'rippled' registered in Tempo"
else
    fail "Service 'rippled' NOT found in Tempo (found: $services)"
fi

log ""
log "--- Phase 2: RPC Spans ---"
check_span "rpc.request"
check_span "rpc.process"
check_span "rpc.command.server_info"
check_span "rpc.command.server_state"
check_span "rpc.command.ledger"

log ""
log "--- Phase 3: Transaction Spans ---"
check_span "tx.process"
check_span "tx.receive"
check_span "tx.apply"

log ""
log "--- Phase 4: Consensus Spans ---"
check_span "consensus.proposal.send"
check_span "consensus.ledger_close"
check_span "consensus.accept"
check_span "consensus.validation.send"

log ""
log "--- Phase 5: Ledger Spans ---"
check_span "ledger.build"
check_span "ledger.validate"
check_span "ledger.store"

log ""
log "--- Phase 5: Peer Spans (trace_peer=1) ---"
check_span "peer.proposal.receive"
check_span "peer.validation.receive"

# ---------------------------------------------------------------------------
# Step 9b: Verify log-trace correlation (Phase 8)
# ---------------------------------------------------------------------------
log ""
log "--- Phase 8: Log-Trace Correlation ---"
check_log_correlation

# ---------------------------------------------------------------------------
# Step 10: Verify Prometheus spanmetrics
# ---------------------------------------------------------------------------
log ""
log "--- Phase 5: Spanmetrics ---"
log "Waiting 20s for Prometheus scrape cycle..."
sleep 20

calls_count=$(curl -sf "$PROM/api/v1/query?query=traces_span_metrics_calls_total" \
    | jq '.data.result | length' 2>/dev/null || echo 0)
if [ "$calls_count" -gt 0 ]; then
    ok "Prometheus: traces_span_metrics_calls_total ($calls_count series)"
else
    fail "Prometheus: traces_span_metrics_calls_total (0 series)"
fi

duration_count=$(curl -sf "$PROM/api/v1/query?query=traces_span_metrics_duration_milliseconds_count" \
    | jq '.data.result | length' 2>/dev/null || echo 0)
if [ "$duration_count" -gt 0 ]; then
    ok "Prometheus: duration histogram ($duration_count series)"
else
    fail "Prometheus: duration histogram (0 series)"
fi

# Check Grafana
if curl -sf http://localhost:3000/api/health > /dev/null 2>&1; then
    ok "Grafana: healthy at localhost:3000"
else
    fail "Grafana: not reachable at localhost:3000"
fi

# ---------------------------------------------------------------------------
# Step 10b: Verify StatsD metrics in Prometheus
# ---------------------------------------------------------------------------
log ""
log "--- Phase 6: StatsD Metrics (beast::insight) ---"
log "Waiting 20s for StatsD aggregation + Prometheus scrape..."
sleep 20

check_statsd_metric() {
    local metric_name="$1"
    local result
    result=$(curl -sf "$PROM/api/v1/query?query=$metric_name" \
        | jq '.data.result | length' 2>/dev/null || echo 0)
    if [ "$result" -gt 0 ]; then
        ok "StatsD: $metric_name ($result series)"
    else
        fail "StatsD: $metric_name (0 series)"
    fi
}

# Node health gauges
check_statsd_metric "rippled_LedgerMaster_Validated_Ledger_Age"
check_statsd_metric "rippled_LedgerMaster_Published_Ledger_Age"
check_statsd_metric "rippled_job_count"

# State accounting
check_statsd_metric "rippled_State_Accounting_Full_duration"

# Peer finder
check_statsd_metric "rippled_Peer_Finder_Active_Inbound_Peers"
check_statsd_metric "rippled_Peer_Finder_Active_Outbound_Peers"

# RPC counters (only if RPC was exercised — should be true from Steps 5-8)
check_statsd_metric "rippled_rpc_requests"

# Overlay traffic
check_statsd_metric "rippled_total_Bytes_In"

# ---------------------------------------------------------------------------
# Step 10c: Verify Phase 9 OTel SDK Metrics
# ---------------------------------------------------------------------------
log ""
log "--- Phase 9: OTel SDK Metrics (MetricsRegistry) ---"
log "Waiting 15s for OTel metric export + Prometheus scrape..."
sleep 15

check_otel_metric() {
    local metric_name="$1"
    local result
    result=$(curl -sf "$PROM/api/v1/query?query=$metric_name" \
        | jq '.data.result | length' 2>/dev/null || echo 0)
    if [ "$result" -gt 0 ]; then
        ok "OTel: $metric_name ($result series)"
    else
        fail "OTel: $metric_name (0 series)"
    fi
}

# Task 9.1: NodeStore I/O
check_otel_metric 'rippled_nodestore_state{metric="node_reads_total"}'
check_otel_metric 'rippled_nodestore_state{metric="write_load"}'

# Task 9.2: Cache hit rates
check_otel_metric 'rippled_cache_metrics{metric="SLE_hit_rate"}'
check_otel_metric 'rippled_cache_metrics{metric="treenode_cache_size"}'

# Task 9.3: TxQ metrics
check_otel_metric 'rippled_txq_metrics{metric="txq_count"}'
check_otel_metric 'rippled_txq_metrics{metric="txq_reference_fee_level"}'

# Task 9.4: Per-RPC metrics
check_otel_metric "rippled_rpc_method_started_total"
check_otel_metric "rippled_rpc_method_finished_total"

# Task 9.5: Per-job metrics
check_otel_metric "rippled_job_queued_total"
check_otel_metric "rippled_job_finished_total"

# Task 9.6: Counted object instances
check_otel_metric "rippled_object_count"

# Task 9.7: Load factor breakdown
check_otel_metric 'rippled_load_factor_metrics{metric="load_factor"}'
check_otel_metric 'rippled_load_factor_metrics{metric="load_factor_server"}'

# ---------------------------------------------------------------------------
# Step 11: Summary
# ---------------------------------------------------------------------------
echo ""
echo "==========================================================="
echo "  INTEGRATION TEST RESULTS"
echo "==========================================================="
printf "  \033[1;32mPASSED: %d\033[0m\n" "$PASS"
printf "  \033[1;31mFAILED: %d\033[0m\n" "$FAIL"
echo "==========================================================="
echo ""
echo "  Observability stack is running:"
echo ""
echo "    Tempo:         http://localhost:3200"
echo "    Grafana:       http://localhost:3000"
echo "    Prometheus:    http://localhost:9090"
echo "    Loki:          http://localhost:3100"
echo ""
echo "  xrpld nodes (6) are running:"
for i in $(seq 1 "$NUM_NODES"); do
    RPC_PORT=$((RPC_PORT_BASE + i - 1))
    PEER_PORT=$((PEER_PORT_BASE + i - 1))
    echo "    Node $i: RPC=localhost:$RPC_PORT  Peer=:$PEER_PORT  PID=$(cat "$WORKDIR/node$i/xrpld.pid" 2>/dev/null || echo 'unknown')"
done
echo ""
echo "  To tear down:"
echo "    bash docker/telemetry/integration-test.sh --cleanup"
echo ""
echo "==========================================================="

if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
