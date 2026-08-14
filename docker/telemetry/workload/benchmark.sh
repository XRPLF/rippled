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
#
# Exit codes:
#   0  Every overhead metric was measured and is within its threshold.
#   1  Every overhead metric was measured and at least one exceeded its
#      threshold. This is the only "telemetry is too expensive" signal.
#      Also returned for a command-line usage error, which the pipeline
#      cannot trigger (run-full-validation.sh passes a fixed flag list).
#   2  The overhead could not be measured at all — a missing prerequisite, a
#      cluster that never reached consensus, or an incomplete metric
#      collection. Nothing was compared, so nothing was breached.
#
# run-full-validation.sh depends on that split: it folds 1 into its own
# "checks failed" exit and 2 into its "infrastructure error" exit. Reporting a
# run that measured nothing as a threshold breach would be a false regression.

set -euo pipefail

# ---------------------------------------------------------------------------
# Colored output helpers
# ---------------------------------------------------------------------------
log() { printf "\033[1;34m[BENCH]\033[0m  %s\n" "$*"; }
ok() { printf "\033[1;32m[BENCH]\033[0m  %s\n" "$*"; }
warn() { printf "\033[1;33m[BENCH]\033[0m  %s\n" "$*"; }
fail() { printf "\033[1;31m[BENCH]\033[0m  %s\n" "$*"; }

# Usage error. Exit 1 by shell convention; see the exit-code block above.
die() {
    fail "$*" >&2
    exit 1
}

# Fatal, and no overhead figure was produced. Exit 2 keeps exit 1 exclusively
# for a measured threshold breach, so the caller never grades a run that
# measured nothing as a performance regression.
cannot_measure() {
    fail "$*" >&2
    exit 2
}

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
        --xrpld)
            XRPLD="$2"
            shift 2
            ;;
        --duration)
            DURATION="$2"
            shift 2
            ;;
        --nodes)
            NUM_NODES="$2"
            shift 2
            ;;
        --output)
            RESULTS_DIR="$2"
            shift 2
            ;;
        -h | --help) usage ;;
        *) die "Unknown option: $1" ;;
    esac
done

# Validate prerequisites. A missing binary or tool means no measurement can be
# taken, which is "cannot measure", not "too slow".
[ -x "$XRPLD" ] || cannot_measure "xrpld not found at $XRPLD"
command -v jq >/dev/null 2>&1 || cannot_measure "jq not found"
command -v bc >/dev/null 2>&1 || cannot_measure "bc not found"
command -v curl >/dev/null 2>&1 || cannot_measure "curl not found"

mkdir -p "$RESULTS_DIR"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)

# ---------------------------------------------------------------------------
# Node cluster management
# ---------------------------------------------------------------------------

# True while xrpld children spawned by start_cluster may still be alive.
# Read by stop_cluster so it can be called any number of times.
CLUSTER_RUNNING=false

start_cluster() {
    local telemetry_enabled="$1"
    local label="$2"

    log "Starting $NUM_NODES-node cluster ($label, telemetry=$telemetry_enabled)..."

    rm -rf "$WORKDIR"
    mkdir -p "$WORKDIR"

    # Generate keys using first node.
    bash "$SCRIPT_DIR/generate-validator-keys.sh" "$XRPLD" "$NUM_NODES" "$WORKDIR"

    # Set before the spawn loop so a failure part-way through it still gets
    # cleaned up by the EXIT trap.
    CLUSTER_RUNNING=true

    # Build per-node configs.
    for i in $(seq 1 "$NUM_NODES"); do
        local node_dir="$WORKDIR/node$i"
        mkdir -p "$node_dir/nudb" "$node_dir/db"

        local rpc_port
        rpc_port=$((RPC_PORT_BASE + i - 1))
        local peer_port
        peer_port=$((PEER_PORT_BASE + i - 1))
        local seed
        seed=$(jq -r ".[$((i - 1))].seed" "$WORKDIR/validator-keys.json")

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
batch_size=512
batch_delay_ms=2000
max_queue_size=2048
trace_rpc=1
trace_transactions=1
trace_consensus=1
trace_peer=1
trace_ledger=1

[insight]
server=otel
endpoint=http://localhost:4318/v1/metrics
prefix=xrpld"
        else
            telemetry_section="
[telemetry]
enabled=0"
        fi

        cat >"$node_dir/xrpld.cfg" <<EOCFG
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

        "$XRPLD" --conf "$node_dir/xrpld.cfg" --start >"$node_dir/stdout.log" 2>&1 &
        echo $! >"$node_dir/xrpld.pid"
    done

    # Wait for consensus. Reaching the limit is fatal: numbers taken from a
    # cluster that never got to "proposing" would silently corrupt the
    # baseline-vs-telemetry comparison.
    local max_wait=120
    log "Waiting for consensus (up to ${max_wait}s)..."
    for attempt in $(seq 1 "$max_wait"); do
        local ready=0
        for i in $(seq 1 "$NUM_NODES"); do
            local port
            port=$((RPC_PORT_BASE + i - 1))
            local state
            state=$(curl -sf "http://localhost:$port" \
                -d '{"method":"server_info"}' 2>/dev/null |
                jq -r '.result.info.server_state' 2>/dev/null || echo "")
            if [ "$state" = "proposing" ]; then
                ready=$((ready + 1))
            fi
        done
        if [ "$ready" -ge "$NUM_NODES" ]; then
            ok "All $NUM_NODES nodes proposing (attempt $attempt)"
            break
        fi
        if [ "$attempt" -eq "$max_wait" ]; then
            cannot_measure "Consensus timeout — only $ready/$NUM_NODES nodes proposing after ${max_wait}s"
        fi
        sleep 1
    done

    # Let the cluster stabilize.
    sleep 5
}

stop_cluster() {
    # Idempotent. The happy path calls this directly and the EXIT trap calls
    # it again, so a second call must not re-kill or log a misleading message.
    [ "$CLUSTER_RUNNING" = true ] || return 0
    CLUSTER_RUNNING=false

    log "Stopping cluster..."
    for i in $(seq 1 "$NUM_NODES"); do
        local pidfile="$WORKDIR/node$i/xrpld.pid"
        if [ -f "$pidfile" ]; then
            kill "$(cat "$pidfile")" 2>/dev/null || true
        fi
    done
    # Belt and braces for a node whose pidfile is missing or stale. Matched on
    # the per-node config path — the shape start_cluster launches nodes with
    # (`--conf $WORKDIR/nodeN/xrpld.cfg`) — rather than on the workdir alone.
    # The loose form killed anything whose command line merely mentioned the
    # workdir, including a developer's `tail -f $WORKDIR/node1/debug.log`, and
    # the EXIT trap now makes this run on every exit path.
    pkill -f "$WORKDIR/node[0-9]+/xrpld\.cfg" 2>/dev/null || true

    # Guarded on purpose. This runs as the EXIT trap, where any unguarded
    # failure makes `set -e` exit with that command's status and discard the
    # status the script meant to report — a threshold breach would surface as
    # a plain 1 and "cannot measure" would lose its 2. With every command here
    # guarded, the explicit `return 0` below is reachable and authoritative.
    sleep 3 || true

    return 0
}

# Reap the cluster on every exit path. Installed here rather than straight
# after argument parsing so the handler name always resolves. Without it, any
# failure between start_cluster and stop_cluster leaks the xrpld children
# along with their RPC ports (5020+) and peer ports (51250+).
trap stop_cluster EXIT

# Build RPC ports CSV string.
rpc_ports_csv() {
    local ports=""
    for i in $(seq 1 "$NUM_NODES"); do
        [ -n "$ports" ] && ports="$ports,"
        ports="$ports$((RPC_PORT_BASE + i - 1))"
    done
    echo "$ports"
}

# Collects one leg of the benchmark.
#
# The collector exits non-zero when it cannot run (1) or when a measurement
# source came back empty (3). An all-zero or partial sample set clears every
# threshold, so an incomplete leg aborts with "cannot measure" instead of being
# compared and passed.
collect_metrics() {
    local label="$1"
    local out_file="$2"

    local status=0
    bash "$SCRIPT_DIR/collect_system_metrics.sh" \
        "$(rpc_ports_csv)" "$DURATION" "$out_file" || status=$?
    [ "$status" -eq 0 ] ||
        cannot_measure "$label metric collection failed (exit $status) — refusing to compare an incomplete run"

    # Only an explicit false counts. The flag is absent from older artifacts,
    # and jq's "//" operator would turn a real false into the default.
    local complete
    complete=$(jq -r '.metrics_complete' "$out_file" 2>/dev/null || echo "null")
    [ "$complete" != "false" ] ||
        cannot_measure "$label metrics are flagged incomplete — refusing to compare an incomplete run"
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
collect_metrics "baseline" "$BASELINE_FILE"
stop_cluster

# --- Telemetry run ---
TELEMETRY_FILE="$RESULTS_DIR/telemetry-${TIMESTAMP}.json"
start_cluster "1" "telemetry"
collect_metrics "telemetry" "$TELEMETRY_FILE"
stop_cluster

# ---------------------------------------------------------------------------
# Compare results
# ---------------------------------------------------------------------------
log "Comparing results..."

# Written into an impact variable when its baseline could not be used.
# check_threshold turns this into an INCONCLUSIVE verdict.
INCONCLUSIVE="n/a"

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

# Both impacts below are ratios of the baseline, so a non-positive baseline
# leaves them undefined. The collector writes tps=0 whenever no ledger
# advanced and read_metric defaults a missing key to 0, so this is a routine
# outcome rather than an edge case. Reporting it as "0% impact" would clear
# the threshold and hide a failed baseline run.
#
# Both expressions scale by 100 before dividing. bc truncates at "scale" after
# every operation, so dividing first would floor the ratio to 2 decimals and
# then multiply the lost precision by 100 — a real 1.25% consensus impact came
# out as exactly 1.00 and passed the 1% threshold.
BASE_TPS=$(read_metric "$BASELINE_FILE" "tps")
TELE_TPS=$(read_metric "$TELEMETRY_FILE" "tps")
if [[ "$(echo "$BASE_TPS > 0" | bc 2>/dev/null)" = "1" ]]; then
    TPS_IMPACT=$(echo "scale=2; ($BASE_TPS - $TELE_TPS) * 100 / $BASE_TPS" | bc 2>/dev/null || echo "0")
else
    TPS_IMPACT="$INCONCLUSIVE"
fi

BASE_CONS=$(read_metric "$BASELINE_FILE" "consensus_round_mean_ms")
TELE_CONS=$(read_metric "$TELEMETRY_FILE" "consensus_round_mean_ms")
if [[ "$(echo "$BASE_CONS > 0" | bc 2>/dev/null)" = "1" ]]; then
    CONS_IMPACT=$(echo "scale=2; ($TELE_CONS - $BASE_CONS) * 100 / $BASE_CONS" | bc 2>/dev/null || echo "0")
else
    CONS_IMPACT="$INCONCLUSIVE"
fi

# ---------------------------------------------------------------------------
# Pass/fail checks
# ---------------------------------------------------------------------------
PASS_COUNT=0
FAIL_COUNT=0
INCONCLUSIVE_COUNT=0

# Records the verdict for one row of the report.
#
# Arguments: metric name, measured value, threshold, unit, and the name of the
# variable to write the bare verdict into.
#
# The verdict travels through that named variable and every diagnostic goes to
# stderr. Calling this through a command substitution would run it in a
# subshell, which drops the counter updates and captures the colored log line
# into the caller's variable.
check_threshold() {
    local name="$1"
    local actual="$2"
    local threshold="$3"
    local unit="$4"
    local result_var="$5"

    # Unusable measurement. Counted as a failure so the exit gate fires: an
    # undefined result must never read as a pass.
    if [ "$actual" = "$INCONCLUSIVE" ]; then
        fail "$name: INCONCLUSIVE — baseline was zero or missing" >&2
        FAIL_COUNT=$((FAIL_COUNT + 1))
        INCONCLUSIVE_COUNT=$((INCONCLUSIVE_COUNT + 1))
        printf -v "$result_var" 'INCONCLUSIVE'
        return
    fi

    # Compare: actual <= threshold
    if [[ "$(echo "$actual <= $threshold" | bc 2>/dev/null)" = "1" ]]; then
        ok "$name: ${actual}${unit} <= ${threshold}${unit} PASS" >&2
        PASS_COUNT=$((PASS_COUNT + 1))
        printf -v "$result_var" 'PASS'
    else
        fail "$name: ${actual}${unit} > ${threshold}${unit} FAIL" >&2
        FAIL_COUNT=$((FAIL_COUNT + 1))
        printf -v "$result_var" 'FAIL'
    fi
}

# Formats a delta for the report table: appends the unit to a real number and
# leaves the INCONCLUSIVE placeholder bare.
fmt_delta() {
    local value="$1"
    local unit="$2"
    if [ "$value" = "$INCONCLUSIVE" ]; then
        printf '%s' "$value"
    else
        printf '%s%s' "$value" "$unit"
    fi
}

check_threshold "CPU overhead" "$CPU_DELTA" "$CPU_THRESHOLD" "%" CPU_RESULT
check_threshold "Memory overhead" "$MEM_DELTA" "$MEM_THRESHOLD" "MB" MEM_RESULT
check_threshold "RPC p99 impact" "$RPC_DELTA" "$RPC_THRESHOLD" "ms" RPC_RESULT
check_threshold "TPS impact" "$TPS_IMPACT" "$TPS_THRESHOLD" "%" TPS_RESULT
check_threshold "Consensus impact" "$CONS_IMPACT" "$CONSENSUS_THRESHOLD" "%" CONS_RESULT

# ---------------------------------------------------------------------------
# Output Markdown table
# ---------------------------------------------------------------------------
REPORT_FILE="$RESULTS_DIR/benchmark-report-${TIMESTAMP}.md"

cat >"$REPORT_FILE" <<EOMD
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
| Throughput (TPS) | ${BASE_TPS} | ${TELE_TPS} | $(fmt_delta "$TPS_IMPACT" "%") | < ${TPS_THRESHOLD}% | ${TPS_RESULT} |
| Consensus Round Mean (ms) | ${BASE_CONS} ms | ${TELE_CONS} ms | $(fmt_delta "$CONS_IMPACT" "%") | < ${CONSENSUS_THRESHOLD}% | ${CONS_RESULT} |

\`INCONCLUSIVE\` means the baseline for that row was zero or missing, so the
impact could not be computed. Such rows count as failures.

\`Consensus Round Mean\` is the mean inter-ledger interval derived from the
collector's 5 s ledger-sequence samples, not a percentile.

## Summary

- **Passed**: $PASS_COUNT / $((PASS_COUNT + FAIL_COUNT))
- **Failed**: $FAIL_COUNT / $((PASS_COUNT + FAIL_COUNT))
- **Inconclusive**: $INCONCLUSIVE_COUNT (included in Failed)

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
