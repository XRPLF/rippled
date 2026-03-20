#!/usr/bin/env bash
# benchmark.sh — Performance benchmark for rippled telemetry overhead.
#
# Runs two identical workloads against a rippled cluster:
#   1. Baseline: telemetry disabled ([telemetry] enabled=0)
#   2. Telemetry: full telemetry enabled (traces + StatsD + all categories)
#
# Compares CPU, memory, RPC latency, TPS, and consensus round time.
# Outputs a Markdown table with pass/fail against configured thresholds.
#
# Usage:
#   ./benchmark.sh --xrpld /path/to/xrpld --duration 300
#
# Thresholds (configurable via environment variables):
#   BENCH_CPU_OVERHEAD_PCT=3       CPU overhead < 3%
#   BENCH_MEM_OVERHEAD_MB=5        Memory overhead < 5MB
#   BENCH_RPC_LATENCY_IMPACT_MS=2  RPC p99 latency impact < 2ms
#   BENCH_TPS_IMPACT_PCT=5         Throughput impact < 5%
#   BENCH_CONSENSUS_IMPACT_PCT=1   Consensus round time impact < 1%

set -euo pipefail

# ---------------------------------------------------------------------------
# Colored output helpers
# ---------------------------------------------------------------------------
log()   { printf "\033[1;34m[BENCH]\033[0m  %s\n" "$*"; }
ok()    { printf "\033[1;32m[BENCH]\033[0m  %s\n" "$*"; }
warn()  { printf "\033[1;33m[BENCH]\033[0m  %s\n" "$*"; }
fail()  { printf "\033[1;31m[BENCH]\033[0m  %s\n" "$*"; }
die()   { printf "\033[1;31m[BENCH]\033[0m  %s\n" "$*" >&2; exit 1; }

# ---------------------------------------------------------------------------
# Defaults and thresholds
# ---------------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

# Configurable thresholds via environment variables.
CPU_THRESHOLD="${BENCH_CPU_OVERHEAD_PCT:-3}"
MEM_THRESHOLD="${BENCH_MEM_OVERHEAD_MB:-5}"
RPC_THRESHOLD="${BENCH_RPC_LATENCY_IMPACT_MS:-2}"
TPS_THRESHOLD="${BENCH_TPS_IMPACT_PCT:-5}"
CONSENSUS_THRESHOLD="${BENCH_CONSENSUS_IMPACT_PCT:-1}"

XRPLD="${BENCH_XRPLD:-$REPO_ROOT/.build/xrpld}"
DURATION=300
NUM_NODES=3
WORKDIR="/tmp/xrpld-benchmark"
RESULTS_DIR="$SCRIPT_DIR/benchmark-results"
RPC_PORT_BASE=5020
PEER_PORT_BASE=51250

# ---------------------------------------------------------------------------
# Argument parsing
# ---------------------------------------------------------------------------
usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  --xrpld PATH      Path to xrpld binary (default: \$REPO_ROOT/.build/xrpld)"
    echo "  --duration SECS   Benchmark duration per run (default: 300)"
    echo "  --nodes NUM       Number of validator nodes (default: 3)"
    echo "  --output DIR      Results output directory"
    echo "  -h, --help        Show this help"
    exit 0
}

while [ $# -gt 0 ]; do
    case "$1" in
        --xrpld)   XRPLD="$2"; shift 2 ;;
        --duration) DURATION="$2"; shift 2 ;;
        --nodes)   NUM_NODES="$2"; shift 2 ;;
        --output)  RESULTS_DIR="$2"; shift 2 ;;
        -h|--help) usage ;;
        *) die "Unknown option: $1" ;;
    esac
done

# Validate prerequisites.
[ -x "$XRPLD" ] || die "xrpld not found at $XRPLD"
command -v jq >/dev/null 2>&1 || die "jq not found"
command -v bc >/dev/null 2>&1 || die "bc not found"
command -v curl >/dev/null 2>&1 || die "curl not found"

mkdir -p "$RESULTS_DIR"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)

# ---------------------------------------------------------------------------
# Node cluster management
# ---------------------------------------------------------------------------
start_cluster() {
    local telemetry_enabled="$1"
    local label="$2"

    log "Starting $NUM_NODES-node cluster ($label, telemetry=$telemetry_enabled)..."

    rm -rf "$WORKDIR"
    mkdir -p "$WORKDIR"

    # Generate keys using first node.
    bash "$SCRIPT_DIR/generate-validator-keys.sh" "$XRPLD" "$NUM_NODES" "$WORKDIR"

    # Build per-node configs.
    for i in $(seq 1 "$NUM_NODES"); do
        local node_dir="$WORKDIR/node$i"
        mkdir -p "$node_dir/nudb" "$node_dir/db"

        local rpc_port=$((RPC_PORT_BASE + i - 1))
        local peer_port=$((PEER_PORT_BASE + i - 1))
        local seed
        seed=$(jq -r ".[$((i-1))].seed" "$WORKDIR/validator-keys.json")

        # Build ips_fixed list.
        local ips_fixed=""
        for j in $(seq 1 "$NUM_NODES"); do
            if [ "$j" -ne "$i" ]; then
                ips_fixed="${ips_fixed}127.0.0.1 $((PEER_PORT_BASE + j - 1))
"
            fi
        done

        # Build telemetry section.
        local telemetry_section=""
        if [ "$telemetry_enabled" = "1" ]; then
            telemetry_section="
[telemetry]
enabled=1
service_instance_id=bench-node-${i}
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
prefix=rippled"
        else
            telemetry_section="
[telemetry]
enabled=0"
        fi

        cat > "$node_dir/xrpld.cfg" <<EOCFG
[server]
port_rpc
port_peer

[port_rpc]
port = $rpc_port
ip = 127.0.0.1
admin = 127.0.0.1
protocol = http

[port_peer]
port = $peer_port
ip = 0.0.0.0
protocol = peer

[node_db]
type=NuDB
path=$node_dir/nudb
online_delete=256

[database_path]
$node_dir/db

[debug_logfile]
$node_dir/debug.log

[validation_seed]
$seed

[validators_file]
$WORKDIR/validators.txt

[ips_fixed]
${ips_fixed}
[peer_private]
1
${telemetry_section}

[rpc_startup]
{ "command": "log_level", "severity": "warning" }

[ssl_verify]
0
EOCFG

        "$XRPLD" --conf "$node_dir/xrpld.cfg" --start > "$node_dir/stdout.log" 2>&1 &
        echo $! > "$node_dir/xrpld.pid"
    done

    # Wait for consensus.
    log "Waiting for consensus..."
    for attempt in $(seq 1 120); do
        local ready=0
        for i in $(seq 1 "$NUM_NODES"); do
            local port=$((RPC_PORT_BASE + i - 1))
            local state
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
        sleep 1
    done

    # Let the cluster stabilize.
    sleep 5
}

stop_cluster() {
    log "Stopping cluster..."
    for i in $(seq 1 "$NUM_NODES"); do
        local pidfile="$WORKDIR/node$i/xrpld.pid"
        if [ -f "$pidfile" ]; then
            kill "$(cat "$pidfile")" 2>/dev/null || true
        fi
    done
    pkill -f "$WORKDIR" 2>/dev/null || true
    sleep 3
}

# Build RPC ports CSV string.
rpc_ports_csv() {
    local ports=""
    for i in $(seq 1 "$NUM_NODES"); do
        [ -n "$ports" ] && ports="$ports,"
        ports="$ports$((RPC_PORT_BASE + i - 1))"
    done
    echo "$ports"
}

# ---------------------------------------------------------------------------
# Run benchmark
# ---------------------------------------------------------------------------
log "="
log "  rippled Telemetry Performance Benchmark"
log "  Nodes: $NUM_NODES | Duration: ${DURATION}s | Binary: $XRPLD"
log "="

# --- Baseline run ---
BASELINE_FILE="$RESULTS_DIR/baseline-${TIMESTAMP}.json"
start_cluster "0" "baseline"
bash "$SCRIPT_DIR/collect_system_metrics.sh" "$(rpc_ports_csv)" "$DURATION" "$BASELINE_FILE"
stop_cluster

# --- Telemetry run ---
TELEMETRY_FILE="$RESULTS_DIR/telemetry-${TIMESTAMP}.json"
start_cluster "1" "telemetry"
bash "$SCRIPT_DIR/collect_system_metrics.sh" "$(rpc_ports_csv)" "$DURATION" "$TELEMETRY_FILE"
stop_cluster

# ---------------------------------------------------------------------------
# Compare results
# ---------------------------------------------------------------------------
log "Comparing results..."

read_metric() {
    local file="$1"
    local key="$2"
    jq -r ".$key // 0" "$file"
}

BASE_CPU=$(read_metric "$BASELINE_FILE" "cpu_pct_avg")
TELE_CPU=$(read_metric "$TELEMETRY_FILE" "cpu_pct_avg")
CPU_DELTA=$(echo "scale=2; $TELE_CPU - $BASE_CPU" | bc 2>/dev/null || echo "0")

BASE_MEM=$(read_metric "$BASELINE_FILE" "memory_rss_mb_peak")
TELE_MEM=$(read_metric "$TELEMETRY_FILE" "memory_rss_mb_peak")
MEM_DELTA=$(echo "scale=2; $TELE_MEM - $BASE_MEM" | bc 2>/dev/null || echo "0")

BASE_RPC=$(read_metric "$BASELINE_FILE" "rpc_p99_ms")
TELE_RPC=$(read_metric "$TELEMETRY_FILE" "rpc_p99_ms")
RPC_DELTA=$(echo "scale=2; $TELE_RPC - $BASE_RPC" | bc 2>/dev/null || echo "0")

BASE_TPS=$(read_metric "$BASELINE_FILE" "tps")
TELE_TPS=$(read_metric "$TELEMETRY_FILE" "tps")
if [ "$(echo "$BASE_TPS > 0" | bc 2>/dev/null)" = "1" ]; then
    TPS_IMPACT=$(echo "scale=2; ($BASE_TPS - $TELE_TPS) / $BASE_TPS * 100" | bc 2>/dev/null || echo "0")
else
    TPS_IMPACT="0"
fi

BASE_CONS=$(read_metric "$BASELINE_FILE" "consensus_round_p95_ms")
TELE_CONS=$(read_metric "$TELEMETRY_FILE" "consensus_round_p95_ms")
if [ "$(echo "$BASE_CONS > 0" | bc 2>/dev/null)" = "1" ]; then
    CONS_IMPACT=$(echo "scale=2; ($TELE_CONS - $BASE_CONS) / $BASE_CONS * 100" | bc 2>/dev/null || echo "0")
else
    CONS_IMPACT="0"
fi

# ---------------------------------------------------------------------------
# Pass/fail checks
# ---------------------------------------------------------------------------
PASS_COUNT=0
FAIL_COUNT=0

check_threshold() {
    local name="$1"
    local actual="$2"
    local threshold="$3"
    local unit="$4"

    # Compare: actual <= threshold
    if [ "$(echo "$actual <= $threshold" | bc 2>/dev/null)" = "1" ]; then
        ok "$name: ${actual}${unit} <= ${threshold}${unit} PASS"
        PASS_COUNT=$((PASS_COUNT + 1))
        echo "PASS"
    else
        fail "$name: ${actual}${unit} > ${threshold}${unit} FAIL"
        FAIL_COUNT=$((FAIL_COUNT + 1))
        echo "FAIL"
    fi
}

CPU_RESULT=$(check_threshold "CPU overhead" "$CPU_DELTA" "$CPU_THRESHOLD" "%")
MEM_RESULT=$(check_threshold "Memory overhead" "$MEM_DELTA" "$MEM_THRESHOLD" "MB")
RPC_RESULT=$(check_threshold "RPC p99 impact" "$RPC_DELTA" "$RPC_THRESHOLD" "ms")
TPS_RESULT=$(check_threshold "TPS impact" "$TPS_IMPACT" "$TPS_THRESHOLD" "%")
CONS_RESULT=$(check_threshold "Consensus impact" "$CONS_IMPACT" "$CONSENSUS_THRESHOLD" "%")

# ---------------------------------------------------------------------------
# Output Markdown table
# ---------------------------------------------------------------------------
REPORT_FILE="$RESULTS_DIR/benchmark-report-${TIMESTAMP}.md"

cat > "$REPORT_FILE" <<EOMD
# Telemetry Performance Benchmark Report

**Date**: $(date -u +"%Y-%m-%d %H:%M:%S UTC")
**Nodes**: $NUM_NODES | **Duration**: ${DURATION}s per run
**Binary**: $XRPLD

## Results

| Metric | Baseline | Telemetry | Delta | Threshold | Result |
|--------|----------|-----------|-------|-----------|--------|
| CPU (avg %) | ${BASE_CPU}% | ${TELE_CPU}% | ${CPU_DELTA}% | < ${CPU_THRESHOLD}% | ${CPU_RESULT} |
| Memory RSS (peak MB) | ${BASE_MEM} MB | ${TELE_MEM} MB | ${MEM_DELTA} MB | < ${MEM_THRESHOLD} MB | ${MEM_RESULT} |
| RPC p99 Latency (ms) | ${BASE_RPC} ms | ${TELE_RPC} ms | ${RPC_DELTA} ms | < ${RPC_THRESHOLD} ms | ${RPC_RESULT} |
| Throughput (TPS) | ${BASE_TPS} | ${TELE_TPS} | ${TPS_IMPACT}% | < ${TPS_THRESHOLD}% | ${TPS_RESULT} |
| Consensus Round p95 (ms) | ${BASE_CONS} ms | ${TELE_CONS} ms | ${CONS_IMPACT}% | < ${CONSENSUS_THRESHOLD}% | ${CONS_RESULT} |

## Summary

- **Passed**: $PASS_COUNT / $((PASS_COUNT + FAIL_COUNT))
- **Failed**: $FAIL_COUNT / $((PASS_COUNT + FAIL_COUNT))

## Raw Data

- Baseline: \`$(basename "$BASELINE_FILE")\`
- Telemetry: \`$(basename "$TELEMETRY_FILE")\`
EOMD

ok "Benchmark report written to $REPORT_FILE"
cat "$REPORT_FILE"

# Exit with failure if any check failed.
if [ "$FAIL_COUNT" -gt 0 ]; then
    exit 1
fi
