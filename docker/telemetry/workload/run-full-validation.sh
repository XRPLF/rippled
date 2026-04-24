#!/usr/bin/env bash
# run-full-validation.sh — Orchestrates the full telemetry validation pipeline.
#
# Sequence:
#   1. Start the observability stack (OTel Collector, Tempo, Prometheus, Loki, Grafana)
#   2. Start a multi-node rippled cluster with full telemetry enabled
#   3. Wait for consensus
#   4. Run workload orchestrator (RPC load, TX submission, propagation wait)
#   5. Run the telemetry validation suite
#   6. Capture OTel timings and compare against committed baseline
#   7. (Optional) Run the performance overhead benchmark
#
# Usage:
#   ./run-full-validation.sh --xrpld /path/to/xrpld
#   ./run-full-validation.sh --xrpld /path/to/xrpld --with-benchmark
#   ./run-full-validation.sh --xrpld /path/to/xrpld --skip-regression
#   ./run-full-validation.sh --cleanup
#
# Exit codes:
#   0 — All validation checks and the regression gate passed
#   1 — Validation checks failed OR the regression gate detected a regression
#   2 — Infrastructure error (cluster/stack failed to start, timing capture failed)

set -euo pipefail

# ---------------------------------------------------------------------------
# Colored output helpers
# ---------------------------------------------------------------------------
log()   { printf "\033[1;34m[VALIDATE]\033[0m %s\n" "$*"; }
ok()    { printf "\033[1;32m[VALIDATE]\033[0m %s\n" "$*"; }
warn()  { printf "\033[1;33m[VALIDATE]\033[0m %s\n" "$*"; }
fail()  { printf "\033[1;31m[VALIDATE]\033[0m %s\n" "$*"; }
die()   { printf "\033[1;31m[VALIDATE]\033[0m %s\n" "$*" >&2; exit 2; }

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TELEMETRY_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
REPO_ROOT="$(cd "$TELEMETRY_DIR/../.." && pwd)"
COMPOSE_FILE="$TELEMETRY_DIR/docker-compose.workload.yaml"
WORKDIR="/tmp/xrpld-validation"

XRPLD="${XRPLD:-$REPO_ROOT/.build/xrpld}"
NUM_NODES=5
RPC_PORT_BASE=5005
WS_PORT_BASE=6006
PEER_PORT_BASE=51235
RPC_RATE=50
RPC_DURATION=120
TX_TPS=5
TX_DURATION=120
WITH_BENCHMARK=false
SKIP_LOKI=false
SKIP_REGRESSION=false
WORKLOAD_PROFILE="full-validation"
REPORT_DIR="$WORKDIR/reports"
# Rate window handed to Prometheus `rate()` when capturing timings. Keep
# this close to the active workload duration so histogram buckets cover
# the measurement window; longer windows dilute short-lived regressions.
REGRESSION_WINDOW="${REGRESSION_WINDOW:-3m}"
BASELINE_FILE="${BASELINE_FILE:-$SCRIPT_DIR/baselines/baseline-timings.json}"
THRESHOLDS_FILE="${THRESHOLDS_FILE:-$SCRIPT_DIR/regression-thresholds.json}"
METRICS_FILE="${METRICS_FILE:-$SCRIPT_DIR/regression-metrics.json}"

GENESIS_ACCOUNT="rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh"
GENESIS_SEED="snoPBrXtMeMyMHUVTgbuqAfg1SUTb"

# ---------------------------------------------------------------------------
# Argument parsing
# ---------------------------------------------------------------------------
usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  --xrpld PATH         Path to xrpld binary"
    echo "  --nodes NUM          Number of validator nodes (default: 5)"
    echo "  --rpc-rate RPS       RPC load rate (default: 50)"
    echo "  --rpc-duration SECS  RPC load duration (default: 120)"
    echo "  --tx-tps TPS         Transaction submit rate (default: 5)"
    echo "  --tx-duration SECS   Transaction submit duration (default: 120)"
    echo "  --profile NAME       Workload profile (default: full-validation)"
    echo "  --with-benchmark     Also run performance overhead benchmark (telemetry off vs on)"
    echo "  --skip-loki          Skip Loki log-trace correlation checks"
    echo "  --skip-regression    Skip the OTel-baseline regression gate"
    echo "  --cleanup            Tear down everything and exit"
    echo "  -h, --help           Show this help"
    exit 0
}

while [ $# -gt 0 ]; do
    case "$1" in
        --xrpld)         XRPLD="$2"; shift 2 ;;
        --nodes)         NUM_NODES="$2"; shift 2 ;;
        --rpc-rate)      RPC_RATE="$2"; shift 2 ;;
        --rpc-duration)  RPC_DURATION="$2"; shift 2 ;;
        --tx-tps)        TX_TPS="$2"; shift 2 ;;
        --tx-duration)   TX_DURATION="$2"; shift 2 ;;
        --profile)       WORKLOAD_PROFILE="$2"; shift 2 ;;
        --with-benchmark) WITH_BENCHMARK=true; shift ;;
        --skip-loki)     SKIP_LOKI=true; shift ;;
        --skip-regression) SKIP_REGRESSION=true; shift ;;
        --cleanup)       # Cleanup mode
            log "Cleaning up..."
            pkill -f "$WORKDIR" 2>/dev/null || true
            docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true
            rm -rf "$WORKDIR"
            ok "Cleanup complete."
            exit 0
            ;;
        -h|--help)       usage ;;
        *)               die "Unknown option: $1" ;;
    esac
done

# ---------------------------------------------------------------------------
# Prerequisites
# ---------------------------------------------------------------------------
log "Checking prerequisites..."
[ -x "$XRPLD" ] || die "xrpld binary not found: $XRPLD"
command -v docker >/dev/null 2>&1 || die "docker not found"
docker compose version >/dev/null 2>&1 || die "docker compose (v2) not found"
command -v python3 >/dev/null 2>&1 || die "python3 not found"
command -v curl >/dev/null 2>&1 || die "curl not found"
command -v jq >/dev/null 2>&1 || die "jq not found"
[ -f "$COMPOSE_FILE" ] || die "docker-compose.workload.yaml not found"

# Install Python dependencies.
log "Installing Python dependencies..."
pip3 install -q -r "$SCRIPT_DIR/requirements.txt" 2>/dev/null || \
    pip install -q -r "$SCRIPT_DIR/requirements.txt" 2>/dev/null || \
    warn "Could not install Python dependencies — they may already be present"

ok "Prerequisites verified."

# ---------------------------------------------------------------------------
# Cleanup previous run
# ---------------------------------------------------------------------------
log "Cleaning up previous run..."
pkill -f "$WORKDIR" 2>/dev/null || true
sleep 2
rm -rf "$WORKDIR"
mkdir -p "$WORKDIR" "$REPORT_DIR"

# ---------------------------------------------------------------------------
# Step 1: Start observability stack
# ---------------------------------------------------------------------------
log "Step 1: Starting observability stack..."
docker compose -f "$COMPOSE_FILE" up -d

log "Waiting for OTel Collector..."
for attempt in $(seq 1 30); do
    status=$(curl -so /dev/null -w '%{http_code}' http://localhost:4318/ 2>/dev/null || echo 000)
    if [ "$status" != "000" ]; then
        ok "OTel Collector ready (attempt $attempt)"
        break
    fi
    [ "$attempt" -eq 30 ] && die "OTel Collector not ready after 30s"
    sleep 1
done

log "Waiting for Tempo..."
for attempt in $(seq 1 30); do
    if curl -sf "http://localhost:3200/ready" >/dev/null 2>&1; then
        ok "Tempo ready (attempt $attempt)"
        break
    fi
    [ "$attempt" -eq 30 ] && die "Tempo not ready after 30s"
    sleep 1
done

log "Waiting for Prometheus..."
for attempt in $(seq 1 30); do
    if curl -sf "http://localhost:9090/-/healthy" >/dev/null 2>&1; then
        ok "Prometheus ready (attempt $attempt)"
        break
    fi
    [ "$attempt" -eq 30 ] && die "Prometheus not ready after 30s"
    sleep 1
done

# ---------------------------------------------------------------------------
# Step 2: Generate validator keys and start cluster
# ---------------------------------------------------------------------------
log "Step 2: Starting $NUM_NODES-node validator cluster..."

bash "$SCRIPT_DIR/generate-validator-keys.sh" "$XRPLD" "$NUM_NODES" "$WORKDIR"

for i in $(seq 1 "$NUM_NODES"); do
    NODE_DIR="$WORKDIR/node$i"
    mkdir -p "$NODE_DIR/nudb" "$NODE_DIR/db"

    RPC_PORT=$((RPC_PORT_BASE + i - 1))
    WS_PORT=$((WS_PORT_BASE + i - 1))
    PEER_PORT=$((PEER_PORT_BASE + i - 1))
    SEED=$(jq -r ".[$((i-1))].seed" "$WORKDIR/validator-keys.json")

    # Build ips_fixed.
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
port_ws
port_peer

[port_rpc]
port = $RPC_PORT
ip = 127.0.0.1
admin = 127.0.0.1
protocol = http

[port_ws]
port = $WS_PORT
ip = 127.0.0.1
admin = 127.0.0.1
protocol = ws

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
$WORKDIR/validators.txt

[ips]
${IPS_FIXED}

[telemetry]
enabled=1
service_instance_id=validator-${i}
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

[insight]
server=statsd
address=127.0.0.1:8125
prefix=rippled

[rpc_startup]
{ "command": "log_level", "severity": "warning" }

[signing_support]
true

[ssl_verify]
0
EOCFG

    "$XRPLD" --conf "$NODE_DIR/xrpld.cfg" --start > "$NODE_DIR/stdout.log" 2>&1 &
    echo $! > "$NODE_DIR/xrpld.pid"
    log "  Node $i: RPC=$RPC_PORT WS=$WS_PORT Peer=$PEER_PORT PID=$!"
done

# ---------------------------------------------------------------------------
# Step 3: Wait for consensus
# ---------------------------------------------------------------------------
log "Step 3: Waiting for consensus..."
for attempt in $(seq 1 120); do
    ready=0
    for i in $(seq 1 "$NUM_NODES"); do
        port=$((RPC_PORT_BASE + i - 1))
        state=$(curl -sf "http://localhost:$port" \
            -d '{"method":"server_info"}' 2>/dev/null \
            | jq -r '.result.info.server_state' 2>/dev/null || echo "")
        if [ "$state" = "proposing" ]; then
            ready=$((ready + 1))
        fi
    done
    if [ "$ready" -ge "$NUM_NODES" ]; then
        ok "All $NUM_NODES nodes proposing (attempt $attempt)"
        break
    fi
    if [ "$attempt" -eq 120 ]; then
        warn "Consensus timeout — $ready/$NUM_NODES nodes ready"
    fi
    printf "\r  %d/%d nodes proposing..." "$ready" "$NUM_NODES"
    sleep 1
done
echo ""

# Wait for first validated ledger.
log "Waiting for validated ledger..."
for attempt in $(seq 1 60); do
    val_seq=$(curl -sf "http://localhost:$RPC_PORT_BASE" \
        -d '{"method":"server_info"}' 2>/dev/null \
        | jq -r '.result.info.validated_ledger.seq // 0' 2>/dev/null || echo 0)
    if [ "$val_seq" -gt 2 ] 2>/dev/null; then
        ok "Validated ledger: seq $val_seq"
        break
    fi
    [ "$attempt" -eq 60 ] && warn "No validated ledger after 60s"
    sleep 1
done

# ---------------------------------------------------------------------------
# Step 4: Run workload orchestrator
# ---------------------------------------------------------------------------
log "Step 4: Running workload orchestrator (profile: $WORKLOAD_PROFILE)..."

WS_ENDPOINTS=""
for i in $(seq 1 "$NUM_NODES"); do
    WS_ENDPOINTS="$WS_ENDPOINTS ws://localhost:$((WS_PORT_BASE + i - 1))"
done

python3 "$SCRIPT_DIR/workload_orchestrator.py" \
    --profile "$WORKLOAD_PROFILE" \
    --endpoints $WS_ENDPOINTS \
    --report "$REPORT_DIR/workload-report.json" \
    --report-dir "$REPORT_DIR" || \
    warn "Workload orchestrator returned non-zero exit"

ok "Workload orchestration complete."

# ---------------------------------------------------------------------------
# Step 5: Run telemetry validation suite
# ---------------------------------------------------------------------------
log "Step 5: Running telemetry validation suite..."

VALIDATION_ARGS="--report $REPORT_DIR/validation-report.json"
if [ "$SKIP_LOKI" = true ]; then
    VALIDATION_ARGS="$VALIDATION_ARGS --skip-loki"
fi

VALIDATION_EXIT=0
python3 "$SCRIPT_DIR/validate_telemetry.py" $VALIDATION_ARGS || VALIDATION_EXIT=$?

if [ "$VALIDATION_EXIT" -eq 0 ]; then
    ok "All telemetry validation checks passed!"
else
    fail "Some telemetry validation checks failed (exit $VALIDATION_EXIT)"
fi

# ---------------------------------------------------------------------------
# Step 6: Capture OTel timings and run the regression comparison
# ---------------------------------------------------------------------------
# This step ALWAYS captures timings (so CI always has an artifact from which
# to bootstrap/refresh the committed baseline). The comparator then either:
#   - prints the paste-me JSON when the baseline is a placeholder, or
#   - enforces thresholds and fails the run on regression.
# Use --skip-regression to opt out (e.g. for ad-hoc local exploration).
TIMINGS_FILE="$REPORT_DIR/timings.json"
REGRESSION_REPORT="$REPORT_DIR/regression-report.json"
REGRESSION_EXIT=0

if [ "$SKIP_REGRESSION" != true ]; then
    log "Step 6: Capturing OTel timings from Prometheus..."
    if python3 "$SCRIPT_DIR/capture_timings.py" \
        --prometheus "http://localhost:9090" \
        --metrics "$METRICS_FILE" \
        --output "$TIMINGS_FILE" \
        --window "$REGRESSION_WINDOW" \
        --profile "$WORKLOAD_PROFILE"
    then
        ok "Timings captured: $TIMINGS_FILE"
    else
        fail "Failed to capture timings — skipping regression comparison."
        REGRESSION_EXIT=2
        SKIP_REGRESSION=true
    fi
fi

if [ "$SKIP_REGRESSION" != true ]; then
    log "Comparing against baseline $BASELINE_FILE..."
    python3 "$SCRIPT_DIR/compare_to_baseline.py" \
        --timings "$TIMINGS_FILE" \
        --baseline "$BASELINE_FILE" \
        --thresholds "$THRESHOLDS_FILE" \
        --report "$REGRESSION_REPORT" || REGRESSION_EXIT=$?
    if [ "$REGRESSION_EXIT" -eq 0 ]; then
        ok "Regression gate passed (or baseline placeholder — paste JSON printed above)."
    elif [ "$REGRESSION_EXIT" -eq 1 ]; then
        fail "Regression detected — see $REGRESSION_REPORT"
    else
        fail "Regression comparator internal error (exit $REGRESSION_EXIT)"
    fi
else
    warn "Regression gate skipped."
fi

# ---------------------------------------------------------------------------
# Step 7: (Optional) Run overhead benchmark
# ---------------------------------------------------------------------------
if [ "$WITH_BENCHMARK" = true ]; then
    log "Step 7: Running performance benchmark..."
    bash "$SCRIPT_DIR/benchmark.sh" \
        --xrpld "$XRPLD" \
        --duration 120 \
        --nodes 3 \
        --output "$REPORT_DIR" || \
        warn "Benchmark returned non-zero exit"
fi

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
echo ""
echo "==========================================================="
echo "  FULL VALIDATION RESULTS"
echo "==========================================================="
echo ""
echo "  Reports directory: $REPORT_DIR"
echo ""
ls -la "$REPORT_DIR/" 2>/dev/null || true
echo ""
echo "  Observability stack is running:"
echo "    Tempo:         http://localhost:3200"
echo "    Grafana:       http://localhost:3000"
echo "    Prometheus:    http://localhost:9090"
echo ""
echo "  xrpld nodes ($NUM_NODES) are running:"
for i in $(seq 1 "$NUM_NODES"); do
    rpc=$((RPC_PORT_BASE + i - 1))
    ws=$((WS_PORT_BASE + i - 1))
    pid=$(cat "$WORKDIR/node$i/xrpld.pid" 2>/dev/null || echo 'unknown')
    echo "    Node $i: RPC=$rpc WS=$ws PID=$pid"
done
echo ""
echo "  To tear down:"
echo "    $0 --cleanup"
echo ""
echo "==========================================================="

# Fail the run if EITHER validation or the regression gate failed. The
# `[ "$VAR" -gt N ]` comparison works here because exit codes are numeric.
FINAL_EXIT=0
if [ "$VALIDATION_EXIT" -ne 0 ]; then
    FINAL_EXIT="$VALIDATION_EXIT"
fi
if [ "$REGRESSION_EXIT" -ne 0 ] && [ "$FINAL_EXIT" -eq 0 ]; then
    FINAL_EXIT="$REGRESSION_EXIT"
fi
exit "$FINAL_EXIT"
