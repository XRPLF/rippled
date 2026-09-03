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
#       OR the benchmark exceeded its overhead thresholds
#   2 — Infrastructure error: the run could not be carried out, so no verdict
#       was reached. Every `die` below exits 2 and is the single source of this
#       code; the cases are an unusable command line, a missing prerequisite, a
#       workdir that could not be prepared, a stack or cluster that did not
#       start, failed workload orchestration, a timing capture that failed while
#       the regression gate was active, and an overhead that could not be
#       measured. Read `die`, not this list, if they ever disagree.
#
# `--help` and `--cleanup` also exit 0; they validate nothing.
#
# Every step below records its status and folds it into FINAL_EXIT; the first
# non-zero status in pipeline order is the one returned, so the earliest
# failure — the one that explains the later ones — is what the caller sees.

set -euo pipefail

# ---------------------------------------------------------------------------
# Colored output helpers
# ---------------------------------------------------------------------------
log() { printf "\033[1;34m[VALIDATE]\033[0m %s\n" "$*"; }
ok() { printf "\033[1;32m[VALIDATE]\033[0m %s\n" "$*"; }
warn() { printf "\033[1;33m[VALIDATE]\033[0m %s\n" "$*"; }
fail() { printf "\033[1;31m[VALIDATE]\033[0m %s\n" "$*"; }
# die MESSAGE — report an infrastructure error and exit with code 2.
#
# Every fallible command that must stop the run routes through here instead of
# being left to errexit. Errexit exits with the failing tool's own status —
# docker uses 1 and 125, jq 2 to 5, rm and mkdir 1 — and a caller reading the
# table above would take those for a validation failure or for a code this
# script never promises to return.
die() {
    printf "\033[1;31m[VALIDATE]\033[0m %s\n" "$*" >&2
    exit 2
}

# Overall run status, folded step by step (see the exit-code table above).
FINAL_EXIT=0

# fold_exit STATUS — record a step's status in FINAL_EXIT.
# First non-zero wins, so FINAL_EXIT names the earliest failing step.
fold_exit() {
    if [ "$1" -ne 0 ] && [ "$FINAL_EXIT" -eq 0 ]; then
        FINAL_EXIT="$1"
    fi
}

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
# Inert: parsed from --rpc-rate/--rpc-duration/--tx-tps/--tx-duration and never
# read again. Load shape comes from the workload profile instead. Kept because
# the CI workflow still passes the four flags.
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

# Loki API base URL. Matches validate_telemetry.py's DEFAULT_LOKI, so the
# diagnostics below query the same instance the log-correlation checks do.
LOKI_URL="${LOKI_URL:-http://localhost:3100}"
# Query window for the Loki diagnostics, in seconds. MUST stay equal to
# LOG_QUERY_WINDOW_SECONDS in validate_telemetry.py (4h): a diagnostic that
# looked further back than the check would report entries the check cannot see,
# which is the one way these numbers could mislead rather than explain.
DIAG_LOG_WINDOW_SECONDS=14400
# The shape Logs::format() actually injects: a 32-hex trace_id followed by a
# 16-hex span_id, both lowercase (src/libxrpl/basics/Log.cpp). Matching the
# exact widths rather than a loose "trace_id=" substring keeps a truncated or
# all-zero id from being counted as a correlated line.
DIAG_TRACE_RE='trace_id=[0-9a-f]{32} span_id=[0-9a-f]{16}'

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
    echo "  --profile NAME       Workload profile (default: full-validation)"
    echo "  --with-benchmark     Also run performance overhead benchmark (telemetry off vs on)"
    echo "  --skip-loki          Skip Loki log-trace correlation checks"
    echo "  --skip-regression    Skip the baseline comparison (timings are still captured)"
    echo "  --cleanup            Tear down everything and exit"
    echo "  -h, --help           Show this help"
    echo ""
    echo "Accepted but INERT (parsed for compatibility, then ignored):"
    echo "  --rpc-rate RPS       no effect"
    echo "  --rpc-duration SECS  no effect"
    echo "  --tx-tps TPS         no effect"
    echo "  --tx-duration SECS   no effect"
    echo ""
    echo "  Load shape comes from the workload profile (--profile), which sets"
    echo "  the rate and duration of every phase in workload-profiles.json."
    echo "  These four flags stay accepted because the CI workflow passes them."
    exit 0
}

# require_value "$@" — die when a two-argument option was given no value.
#
# Called with the remaining arguments, so $# is what is left to parse. Without
# it, `set -u` aborts on the unset $2 with status 1, which the table above
# assigns to a failed check — the same mismapping the guards below remove. An
# unknown option and a valueless one are both bad command lines and must return
# the same code.
require_value() {
    [ $# -ge 2 ] || die "Option $1 requires a value"
}

while [ $# -gt 0 ]; do
    case "$1" in
        --xrpld)
            require_value "$@"
            XRPLD="$2"
            shift 2
            ;;
        --nodes)
            require_value "$@"
            NUM_NODES="$2"
            shift 2
            ;;
        # The next four are inert — see the RPC_RATE default above.
        --rpc-rate)
            require_value "$@"
            RPC_RATE="$2"
            shift 2
            ;;
        --rpc-duration)
            require_value "$@"
            RPC_DURATION="$2"
            shift 2
            ;;
        --tx-tps)
            require_value "$@"
            TX_TPS="$2"
            shift 2
            ;;
        --tx-duration)
            require_value "$@"
            TX_DURATION="$2"
            shift 2
            ;;
        --profile)
            require_value "$@"
            WORKLOAD_PROFILE="$2"
            shift 2
            ;;
        --with-benchmark)
            WITH_BENCHMARK=true
            shift
            ;;
        --skip-loki)
            SKIP_LOKI=true
            shift
            ;;
        --skip-regression)
            SKIP_REGRESSION=true
            shift
            ;;
        --cleanup) # Cleanup mode
            log "Cleaning up..."
            # Match the node config path, not the bare workdir: a plain
            # "$WORKDIR" pattern also matches any shell, editor or log tail
            # whose command line merely mentions that path.
            pkill -f "$WORKDIR/node[0-9]+/xrpld\.cfg" 2>/dev/null || true
            docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true
            # The collector bind-mounts $WORKDIR (see XRPLD_LOG_DIR below), so a
            # file left behind owned by a container uid makes this fail. Leaving
            # it in place would hand the next run stale node state, so say so
            # rather than reporting a clean teardown.
            rm -rf "$WORKDIR" || die "Could not remove $WORKDIR — remove it manually before the next run"
            ok "Cleanup complete."
            exit 0
            ;;
        -h | --help) usage ;;
        *) die "Unknown option: $1" ;;
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
pip3 install -q -r "$SCRIPT_DIR/requirements.txt" 2>/dev/null ||
    pip install -q -r "$SCRIPT_DIR/requirements.txt" 2>/dev/null ||
    warn "Could not install Python dependencies — they may already be present"

ok "Prerequisites verified."

# ---------------------------------------------------------------------------
# Cleanup previous run
# ---------------------------------------------------------------------------
log "Cleaning up previous run..."
# Narrowed for the same reason as the --cleanup branch above.
pkill -f "$WORKDIR/node[0-9]+/xrpld\.cfg" 2>/dev/null || true
sleep 2
rm -rf "$WORKDIR" || die "Could not remove the previous run's workdir $WORKDIR"
mkdir -p "$WORKDIR" "$REPORT_DIR" || die "Could not create $WORKDIR and $REPORT_DIR"

# ---------------------------------------------------------------------------
# Step 1: Start observability stack
# ---------------------------------------------------------------------------
log "Step 1: Starting observability stack..."
# Point the collector's log mount at this run's workdir so the filelog
# receiver tails the per-node debug.log files generated below.
XRPLD_LOG_DIR="$WORKDIR" docker compose -f "$COMPOSE_FILE" up -d ||
    die "docker compose up failed for $COMPOSE_FILE — the observability stack did not start"

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

bash "$SCRIPT_DIR/generate-validator-keys.sh" "$XRPLD" "$NUM_NODES" "$WORKDIR" ||
    die "generate-validator-keys.sh failed — no validator keys for the $NUM_NODES-node cluster"

for i in $(seq 1 "$NUM_NODES"); do
    NODE_DIR="$WORKDIR/node$i"
    mkdir -p "$NODE_DIR/nudb" "$NODE_DIR/db" || die "Could not create node$i directories under $NODE_DIR"

    RPC_PORT=$((RPC_PORT_BASE + i - 1))
    WS_PORT=$((WS_PORT_BASE + i - 1))
    PEER_PORT=$((PEER_PORT_BASE + i - 1))
    SEED=$(jq -r ".[$((i - 1))].seed" "$WORKDIR/validator-keys.json") ||
        die "Could not read node$i's seed from $WORKDIR/validator-keys.json"
    # jq prints the string "null" and exits 0 when the array is shorter than
    # NUM_NODES, so the exit status alone does not detect a short key file. An
    # unusable seed here is only visible ~200s later as a cluster that never
    # proposes, which names the wrong step.
    case "$SEED" in
        "" | null) die "node$i has no seed in $WORKDIR/validator-keys.json — the file holds fewer than $NUM_NODES entries, or entry $((i - 1)) carries no seed" ;;
    esac

    # Build ips_fixed.
    IPS_FIXED=""
    for j in $(seq 1 "$NUM_NODES"); do
        if [ "$j" -ne "$i" ]; then
            IPS_FIXED="${IPS_FIXED}127.0.0.1 $((PEER_PORT_BASE + j - 1))
"
        fi
    done

    cat >"$NODE_DIR/xrpld.cfg" <<EOCFG || die "Could not write node$i's config to $NODE_DIR/xrpld.cfg"
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
batch_size=512
batch_delay_ms=2000
max_queue_size=2048
trace_rpc=1
trace_transactions=1
trace_consensus=1
trace_peer=1
trace_ledger=1

[insight]
# Native OTel metrics via OTLP/HTTP. The collector has no StatsD receiver
# (its metrics pipeline is [otlp, spanmetrics]), so beast::insight must export
# over OTLP for system metrics to reach Prometheus at all. server=otel is the
# only load-bearing key here -- it selects the OTel collector.
#
# endpoint below is parsed but inert, exactly like prefix: CollectorManager
# reads it and hands it to OTelCollector, which uses it only in its startup log
# line. The URL the metric exporter actually posts to is derived in
# Telemetry::initMetrics() from [telemetry] endpoint, by swapping the trailing
# /v1/traces for /v1/metrics. It is kept here so the log line names the right
# URL; changing it would not redirect a single metric.
#
# No prefix is set on purpose. It would be inert: OTelCollector applies no
# prefix to instrument names, so exported names are the lowercased raw names
# (jobq_job_count, rpc_requests_total, total_bytes_in). The service is
# identified by the OTel resource service.name, not by a name prefix.
server=otel
endpoint=http://localhost:4318/v1/metrics

[rpc_startup]
# info is required here, not cosmetic, and is the minimum that works. The
# log-trace correlation checks need a log line emitted while a span is current
# on the thread. A span becomes current either as a ScopedSpanGuard or by
# activating a plain SpanGuard via activate()/activateIfLive(); a plain
# SpanGuard that is never activated yields no trace_id.
#
# The guaranteed correlated line at info is the consensus accept pair in
# RCLConsensus.cpp (an if/else, so exactly one fires every accepted round).
# doAccept activates the accept span as ambient over its whole body, so both
# branches carry a trace_id. At ~4s per round that is dozens of correlated
# lines per run.
#
# Do NOT raise this to debug to get more coverage. debug puts synchronous log
# I/O inside ledger.build, consensus.accept and tx.apply -- the same spans
# whose p50/p95/p99 latencies regression-metrics.json gates -- so a baseline
# captured at debug would bake log I/O into the numbers permanently.
{ "command": "log_level", "severity": "info" }

[signing_support]
true

[ssl_verify]
0
EOCFG

    "$XRPLD" --conf "$NODE_DIR/xrpld.cfg" --start >"$NODE_DIR/stdout.log" 2>&1 &
    # The pid file is the only record of this child: every later liveness check
    # and crash report reads it back. Losing it silently would make a dead node
    # indistinguishable from one that was never started.
    echo $! >"$NODE_DIR/xrpld.pid" || die "Could not write node$i's pid file $NODE_DIR/xrpld.pid"
    log "  Node $i: RPC=$RPC_PORT WS=$WS_PORT Peer=$PEER_PORT PID=$!"
done

# ---------------------------------------------------------------------------
# Step 3: Wait for consensus
# ---------------------------------------------------------------------------
# Report whether a node process is still alive.
#
# A child that has exited but not yet been waited on still answers `kill -0`,
# because the zombie keeps its pid until someone collects it. Checking only
# `kill -0` therefore reads a dead node as alive for the whole readiness
# window, which is how a crashed node came to look like a slow one.
node_running() {
    local pid="$1" state
    kill -0 "$pid" 2>/dev/null || return 1
    if [ -r "/proc/$pid/stat" ]; then
        state=$(awk '{print $3}' "/proc/$pid/stat" 2>/dev/null || echo "?")
        [ "$state" != "Z" ] || return 1
    fi
    return 0
}

# Print why each stopped node stopped: its wait status, then its last output.
#
# The status is the discriminator this harness was missing -- 137 is SIGKILL
# (the kernel reclaiming memory), 139 a segfault, 134 an abort, anything under
# 128 a deliberate exit. The nodes are direct children of this script, so their
# status is still retrievable until something waits on them.
#
# stdout is printed inline rather than left to the artifact upload because a
# node that dies before its debug log opens writes nothing else, and a
# cancelled run uploads nothing at all.
report_stopped_nodes() {
    local i pid status
    for i in $(seq 1 "$NUM_NODES"); do
        pid=$(cat "$WORKDIR/node$i/xrpld.pid" 2>/dev/null || echo "")
        [ -n "$pid" ] || continue
        node_running "$pid" && continue
        status=0
        wait "$pid" 2>/dev/null || status=$?
        warn "node$i (pid $pid) is not running — wait status $status"
        if [ -s "$WORKDIR/node$i/stdout.log" ]; then
            warn "node$i last output:"
            tail -n 15 "$WORKDIR/node$i/stdout.log" | sed 's/^/      /' >&2
        else
            warn "node$i wrote no stdout at all"
        fi
    done
}

log "Step 3: Waiting for consensus..."
for attempt in $(seq 1 120); do
    ready=0
    # Reset each attempt so a timeout reports the final state, not a history.
    laggards=""
    for i in $(seq 1 "$NUM_NODES"); do
        port=$((RPC_PORT_BASE + i - 1))
        state=$(curl -sf "http://localhost:$port" \
            -d '{"method":"server_info"}' 2>/dev/null |
            jq -r '.result.info.server_state' 2>/dev/null || echo "")
        if [ "$state" = "proposing" ]; then
            ready=$((ready + 1))
        else
            # Name the node and what it last reported. A bare count says a
            # node is missing but not which one, which leaves nothing to grep
            # for in the artifacts. An empty state means the RPC port did not
            # answer at all, which usually means the process is gone.
            laggards="$laggards node$i=${state:-unreachable}"
        fi
    done
    if [ "$ready" -ge "$NUM_NODES" ]; then
        ok "All $NUM_NODES nodes proposing (attempt $attempt)"
        break
    fi
    # A stopped process will never reach proposing. Waiting out the rest of the
    # window only delays the same failure and buries its cause under two
    # minutes of progress output.
    stopped=0
    for n in $(seq 1 "$NUM_NODES"); do
        p=$(cat "$WORKDIR/node$n/xrpld.pid" 2>/dev/null || echo "")
        if [ -n "$p" ] && ! node_running "$p"; then
            stopped=$((stopped + 1))
        fi
    done
    if [ "$stopped" -gt 0 ]; then
        echo ""
        # Tolerated: the reporter only prints. Unguarded, a failure inside it —
        # a log that became unreadable after the -s test, a full disk — trips
        # errexit there and the die below never runs, so a dead cluster would
        # exit 1 and read as a failed check instead of an infrastructure error.
        report_stopped_nodes || true
        die "$stopped of $NUM_NODES node(s) stopped during startup; only $ready reached proposing. Not proposing:${laggards}. Per-node status is above, then '$0 --cleanup'."
    fi
    if [ "$attempt" -eq 120 ]; then
        # Fatal, not a warning. A partial cluster still answers queries, so the
        # run would complete and report unrelated span/metric failures: series
        # counts scale with the number of live nodes, and spans that need a
        # quorum are simply never emitted. One infrastructure error here is
        # worth more than a pile of misleading assertion failures later.
        echo ""
        # Every node is still running but not proposing, so this is a genuine
        # convergence problem rather than a crash. Run the reporter anyway: it
        # is a no-op when nothing stopped, and it costs nothing to be sure.
        # Tolerated for the same reason as above.
        report_stopped_nodes || true
        die "Consensus timeout — only $ready/$NUM_NODES nodes proposing after ${attempt}s. Not proposing:${laggards}. Check $WORKDIR/node*/debug.log and $WORKDIR/node*/stdout.log (a node that died before its log sink opened writes only the latter), then '$0 --cleanup'."
    fi
    printf "\r  %d/%d nodes proposing..." "$ready" "$NUM_NODES"
    sleep 1
done
echo ""

# Wait for first validated ledger.
log "Waiting for validated ledger..."
for attempt in $(seq 1 60); do
    val_seq=$(curl -sf "http://localhost:$RPC_PORT_BASE" \
        -d '{"method":"server_info"}' 2>/dev/null |
        jq -r '.result.info.validated_ledger.seq // 0' 2>/dev/null || echo 0)
    if [ "$val_seq" -gt 2 ] 2>/dev/null; then
        ok "Validated ledger: seq $val_seq"
        break
    fi
    # Fatal for the same reason as the consensus timeout above, and because
    # several assertions are gated on a validated ledger existing at all:
    # ledger_economy{metric="base_fee_xrp"} is only observed from a validated
    # ledger, and complete_ledgers stays absent while the range is empty.
    if [ "$attempt" -eq 60 ]; then
        die "No validated ledger after ${attempt}s (last seq: $val_seq). Check $WORKDIR/node*/debug.log, then '$0 --cleanup'."
    fi
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

ORCHESTRATOR_EXIT=0
python3 "$SCRIPT_DIR/workload_orchestrator.py" \
    --profile "$WORKLOAD_PROFILE" \
    --endpoints $WS_ENDPOINTS \
    --report "$REPORT_DIR/workload-report.json" \
    --report-dir "$REPORT_DIR" || ORCHESTRATOR_EXIT=$?

if [ "$ORCHESTRATOR_EXIT" -eq 0 ]; then
    ok "Workload orchestration complete."
else
    # Treated as an infrastructure error: the span and metric assertions below
    # would be graded against traffic that was never generated.
    fail "Workload orchestrator failed (exit $ORCHESTRATOR_EXIT) — the checks below run against incomplete traffic"
    fold_exit 2
fi

# ---------------------------------------------------------------------------
# Log-trace correlation diagnostics
# ---------------------------------------------------------------------------
# Log-trace correlation has four legs and a failed check names none of them:
# the node must write a debug.log line carrying trace ids, the collector
# container must see that file, its filelog receiver must parse and export the
# line, and Loki must return it for the validator's own LogQL. Each leg below
# reports what it observed, so a reader with only the CI log can tell which one
# broke instead of guessing.
#
# Everything here is diagnostic. Every leg runs in its own subshell with
# errexit off, and every external call is guarded, so a missing container, a
# wedged docker daemon or an unreachable endpoint degrades to a printed note.
# A diagnostic must never be the reason a run fails.

# Fallback copies of the stream selector and line filter the log checks use.
# Only reached when the constants cannot be read out of validate_telemetry.py
# (see diag_validator_const); the module is the source of truth.
DIAG_LOG_SELECTOR='{service_name="xrpld"}'
DIAG_LOG_FILTER='|= "trace_id="'

# Bound every external call: a hung docker daemon or endpoint must not stall
# the run. Absent coreutils' timeout the calls still run, just unbounded.
DIAG_HAVE_TIMEOUT=false
if command -v timeout >/dev/null 2>&1; then
    DIAG_HAVE_TIMEOUT=true
fi

diag_run() {
    if [ "$DIAG_HAVE_TIMEOUT" = true ]; then
        timeout 20 "$@"
    else
        "$@"
    fi
}

# Container id of the running collector; empty when it is not up.
diag_collector_cid() {
    docker compose -f "$COMPOSE_FILE" ps -q otel-collector 2>/dev/null | head -1
}

# An image from this same stack that carries a shell, used to look inside the
# collector's mounts and network namespace.
#
# The collector's own image cannot serve: it is built from scratch and ships
# only the binary, so `docker exec <collector> ls` fails with "executable file
# not found in $PATH". Read from the compose config rather than from a running
# container so it resolves even when that service is down, and taken from the
# stack so this never pulls an image the run did not already need.
diag_shell_image() {
    docker compose -f "$COMPOSE_FILE" config --format json 2>/dev/null |
        jq -r '.services.prometheus.image // empty' 2>/dev/null
}

# Read a string constant out of validate_telemetry.py.
#
# Two copies of the same query would drift, and the copy with no check attached
# is the one that ends up wrong — so the query is derived from the module that
# runs the checks rather than restated here. $2 is the fallback, used only when
# the import is impossible (e.g. aiohttp missing); it is the value the module
# defines today, so the worst case is a stale literal rather than no output.
diag_validator_const() {
    python3 -c \
        "import sys; sys.path.insert(0, '$SCRIPT_DIR'); import validate_telemetry as v; print(v.$1)" \
        2>/dev/null || printf '%s\n' "$2"
}

# Sum an instant LogQL count over every stream it returns. Returns non-zero
# when Loki could not be queried, which the caller reports as unavailable
# rather than as zero — "Loki said none" and "Loki did not answer" are
# different findings.
diag_loki_count() {
    local raw status body
    # No -f here on purpose. curl -f discards the response body on a 4xx, and
    # Loki explains a rejected query only in that body (as text/plain), so -f
    # turned a self-describing failure into a bare "unavailable". Append the
    # status code instead and read the body either way.
    raw=$(diag_run curl -sG --max-time 10 -w $'\n%{http_code}' \
        "$LOKI_URL/loki/api/v1/query" \
        --data-urlencode "query=$1" --data-urlencode "time=$2" 2>/dev/null) || return 1
    status=${raw##*$'\n'}
    body=${raw%$'\n'*}
    if [ "$status" != "200" ]; then
        # To stderr: this function's stdout is the count its caller captures.
        printf '    Loki rejected the diagnostic query (HTTP %s): %.300s\n' \
            "$status" "$(printf '%s' "$body" | tr -s '[:space:]' ' ')" >&2
        return 1
    fi
    printf '%s' "$body" |
        jq -er '[.data.result[].value[1] | tonumber] | add // 0' 2>/dev/null || return 1
}

# Leg 1 — node: does xrpld emit correlated lines at all?
#
# Separates the three node-side failures that all surface as "no correlated
# logs": no debug.log at all (the node died before opening its log sink), a log
# level too high to reach the correlated call sites (severity mix shows no
# NFO), and lines flowing but none emitted inside an active sampled span
# (correlated=0 alongside a healthy severity mix).
diag_node_logs() {
    local i log bytes total correlated sample
    echo "  [leg 1/4 node] debug.log lines matching '$DIAG_TRACE_RE'"
    for i in $(seq 1 "$NUM_NODES"); do
        log="$WORKDIR/node$i/debug.log"
        if [ ! -f "$log" ]; then
            echo "    node$i: no debug.log at $log — the node never opened its log sink"
            continue
        fi
        bytes=$(wc -c <"$log" 2>/dev/null || echo 0)
        total=$(wc -l <"$log" 2>/dev/null || echo 0)
        # grep -c exits 1 on zero matches but still prints the count, so the
        # guard keeps the 0 rather than replacing it with an empty string.
        correlated=$(grep -cE "$DIAG_TRACE_RE" "$log" 2>/dev/null || true)
        echo "    node$i: bytes=$bytes lines=$total correlated=${correlated:-0}"
        # Severity mix from the '[partition:]SEV' token, which Logs::format()
        # writes as the 4th whitespace-separated field. Fixed key order so two
        # runs' output can be diffed directly. Lines whose 4th field is not a
        # severity code are message continuations, counted as unparsed.
        awk '{
                 n = split($4, f, ":")
                 s = f[n]
                 if (s ~ /^(TRC|DBG|NFO|WRN|ERR|FTL)$/)
                     c[s]++
                 else
                     other++
             }
             END {
                 split("TRC DBG NFO WRN ERR FTL", order, " ")
                 line = ""
                 for (k = 1; k <= 6; k++)
                     line = line sprintf("%s=%d ", order[k], c[order[k]] + 0)
                 printf "      severity: %sunparsed=%d\n", line, other + 0
             }' "$log" 2>/dev/null || echo "      severity: (could not be computed)"
        sample=$(grep -m1 -E "$DIAG_TRACE_RE" "$log" 2>/dev/null || true)
        if [ -n "$sample" ]; then
            printf '      sample: %.200s\n' "$sample"
        fi
    done
}

# Leg 2 — mount: does the collector container see those files?
#
# A correct host-side log with an empty container-side view is the signature of
# a mount or permission problem. The listing runs in a throwaway container
# started with --volumes-from and the collector's own uid, so it reproduces the
# collector's exact mount set and access rights instead of the host's.
diag_collector_mount() {
    local cid img usr binds
    local -a user_flag=()
    echo "  [leg 2/4 mount] container-side view of /var/log/xrpld"
    if ! command -v docker >/dev/null 2>&1; then
        echo "    docker is not on PATH — leg skipped"
        return 0
    fi
    cid=$(diag_collector_cid)
    if [ -z "$cid" ]; then
        echo "    otel-collector container is not running — leg skipped"
        return 0
    fi
    echo "    binds declared on the container:"
    binds=$(diag_run docker inspect \
        -f '{{range .Mounts}}{{.Type}} {{.Source}} -> {{.Destination}} rw={{.RW}}{{"\n"}}{{end}}' \
        "$cid" 2>/dev/null || true)
    if [ -n "$binds" ]; then
        printf '%s\n' "$binds" | sed '/^[[:space:]]*$/d; s/^/      /'
    else
        echo "      (docker inspect reported no mounts)"
    fi
    img=$(diag_shell_image)
    if [ -z "$img" ]; then
        echo "    no stack image with a shell resolved — container-side listing skipped"
        return 0
    fi
    usr=$(diag_run docker inspect -f '{{.Config.User}}' "$cid" 2>/dev/null || true)
    if [ -n "$usr" ]; then
        user_flag=(--user "$usr")
    fi
    echo "    listing as uid '${usr:-<image default>}' using $img:"
    diag_run docker run --rm --volumes-from "$cid" \
        ${user_flag[@]+"${user_flag[@]}"} --entrypoint /bin/sh "$img" -c \
        'ls -la /var/log/xrpld 2>&1
         find /var/log/xrpld -maxdepth 2 -name debug.log -exec ls -l {} \; 2>&1' |
        sed 's/^/      /' || echo "      (container-side listing failed)"
}

# Leg 3 — collector: did the filelog receiver parse and export those lines?
#
# Two independent readings. The collector's own stderr names every file the
# receiver opened and carries any filelog parse or Loki export error. Its
# internal telemetry counts log records in and out: accepted>0 with sent=0 is
# an export failure, accepted=0 while files are being watched is a parse
# failure.
#
# That telemetry endpoint binds to the container's own localhost and its port
# is not published, so it is unreachable from the host and is read from inside
# the container's network namespace instead. The names below are a filter over
# whatever the collector reports, not an assertion that any given series
# exists; when it reports nothing matching, the leg says so.
diag_collector_pipeline() {
    local cid img watched problems metrics
    echo "  [leg 3/4 collector] filelog receiver state"
    if ! command -v docker >/dev/null 2>&1; then
        echo "    docker is not on PATH — leg skipped"
        return 0
    fi
    cid=$(diag_collector_cid)
    if [ -z "$cid" ]; then
        echo "    otel-collector container is not running — leg skipped"
        return 0
    fi
    watched=$(diag_run docker logs "$cid" 2>&1 |
        grep -F 'Started watching file' | grep -oE '"path": "[^"]*"' | sort -u || true)
    if [ -n "$watched" ]; then
        echo "    files the receiver opened:"
        printf '%s\n' "$watched" | sed 's/^/      /'
    else
        echo "    the receiver reported opening no files"
    fi
    # Second filter keys on the collector's own logs-pipeline markers so this
    # does not report warnings from the trace or metric pipelines. Nothing is
    # excluded beyond that: the collector's benign config-alias deprecation
    # notices ("filelog" -> "file_log") do surface here, and suppressing lines
    # because they are usually harmless is how a diagnostic hides the one that
    # was not.
    problems=$(diag_run docker logs "$cid" 2>&1 |
        grep -iE '(warn|error)' |
        grep -iE 'filelog|fileconsumer|loki|signal": *"logs' |
        tail -n 20 || true)
    if [ -n "$problems" ]; then
        echo "    logs-pipeline warnings and errors (last 20):"
        printf '%s\n' "$problems" | cut -c1-300 | sed 's/^/      /'
    else
        echo "    no logs-pipeline warnings or errors in the collector's output"
    fi
    img=$(diag_shell_image)
    if [ -z "$img" ]; then
        echo "    no stack image with a shell resolved — internal telemetry skipped"
        return 0
    fi
    metrics=$(diag_run docker run --rm --network "container:$cid" \
        --entrypoint /bin/sh "$img" -c \
        'wget -qO- --timeout=5 http://localhost:8888/metrics 2>/dev/null' 2>/dev/null |
        grep -E '^otelcol_([a-z]+_)*log_records|^otelcol_fileconsumer' || true)
    if [ -n "$metrics" ]; then
        echo "    collector internal log-record counters:"
        printf '%s\n' "$metrics" | sed 's/^/      /'
    else
        echo "    collector internal telemetry reported no log-record counters"
    fi
}

# Leg 4 — Loki: is the entry queryable by the validator's own LogQL?
#
# Counts the selector on its own and the selector plus line filter separately,
# so "Loki has nothing" is distinguishable from "Loki has lines but none carry
# a trace id". The label inventory catches the third case: entries ingested
# under a label set the validator's selector cannot match.
diag_loki_stream() {
    local now selector correlation query_all query_filtered
    local total matching labels values sample
    echo "  [leg 4/4 loki] entries in the last ${DIAG_LOG_WINDOW_SECONDS}s (the window the checks use)"
    now=$(date +%s)
    selector=$(diag_validator_const LOG_STREAM_SELECTOR "$DIAG_LOG_SELECTOR")
    correlation=$(diag_validator_const LOG_CORRELATION_QUERY \
        "$DIAG_LOG_SELECTOR $DIAG_LOG_FILTER")
    [ -n "$selector" ] || selector="$DIAG_LOG_SELECTOR"
    [ -n "$correlation" ] || correlation="$DIAG_LOG_SELECTOR $DIAG_LOG_FILTER"
    # sum() is required, for the reason recorded at _log_loki_diagnostics in
    # validate_telemetry.py: the filelog regex_parser leaves message/timestamp
    # as log-record attributes, Loki's OTLP path turns those into structured
    # metadata that joins a metric query's label set, so an unaggregated
    # count_over_time yields one series per log line and Loki rejects the query
    # with HTTP 400 past 500 series. Both legs then print "unavailable".
    query_all="sum(count_over_time($selector[${DIAG_LOG_WINDOW_SECONDS}s]))"
    query_filtered="sum(count_over_time($correlation [${DIAG_LOG_WINDOW_SECONDS}s]))"
    echo "    validator LogQL:  $correlation"
    echo "    diagnostic LogQL: $query_all"
    echo "                      $query_filtered"
    labels=$(diag_run curl -sf --max-time 10 "$LOKI_URL/loki/api/v1/labels" 2>/dev/null |
        jq -r '.data // [] | join(", ")' 2>/dev/null || true)
    values=$(diag_run curl -sf --max-time 10 \
        "$LOKI_URL/loki/api/v1/label/service_name/values" 2>/dev/null |
        jq -r '.data // [] | join(", ")' 2>/dev/null || true)
    echo "    stream labels Loki knows: ${labels:-<none or unreachable>}"
    echo "    service_name values:      ${values:-<none or unreachable>}"
    if total=$(diag_loki_count "$query_all" "$now"); then
        echo "    entries matching the selector:            $total"
    else
        echo "    entries matching the selector:            unavailable (Loki did not answer)"
    fi
    if matching=$(diag_loki_count "$query_filtered" "$now"); then
        echo "    entries also matching the line filter:    $matching"
    else
        echo "    entries also matching the line filter:    unavailable (Loki did not answer)"
    fi
    sample=$(diag_run curl -sfG --max-time 10 "$LOKI_URL/loki/api/v1/query_range" \
        --data-urlencode "query=$correlation" \
        --data-urlencode "start=$((now - DIAG_LOG_WINDOW_SECONDS))000000000" \
        --data-urlencode "end=${now}000000000" \
        --data-urlencode "limit=1" --data-urlencode "direction=backward" 2>/dev/null |
        jq -r '.data.result[0].values[0][1] // empty' 2>/dev/null || true)
    if [ -n "$sample" ]; then
        printf '    sample entry: %.200s\n' "$sample"
    fi
}

# Run every leg, isolated. Each subshell disables errexit and nounset so a leg
# that trips still prints what it had, and its failure is reported rather than
# propagated. Always returns success.
run_log_correlation_diagnostics() {
    local leg
    echo ""
    echo "--- Log-trace correlation diagnostics (non-fatal, for attribution only) ---"
    for leg in diag_node_logs diag_collector_mount diag_collector_pipeline diag_loki_stream; do
        (
            set +e +u +o pipefail
            "$leg"
        ) || warn "$leg: the diagnostic itself failed — ignored"
    done
    echo ""
    return 0
}

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

# Only when the log-correlation checks actually ran: with --skip-loki there is
# no result to attribute. Runs whether they passed or failed — the same numbers
# that explain a failure are what proves a pass was not a coincidence. Placed
# after the suite so its Loki counts are never earlier than the checks' own.
if [ "$SKIP_LOKI" != true ]; then
    run_log_correlation_diagnostics
fi

fold_exit "$VALIDATION_EXIT"

# ---------------------------------------------------------------------------
# Step 6: Capture OTel timings and run the regression comparison
# ---------------------------------------------------------------------------
# Capture ALWAYS runs, so every run leaves a timings.json artifact — it is the
# only route to a new committed baseline. The workflow's "Print regression
# summary" step reads that file unconditionally and, when the committed baseline
# is still a placeholder and the capture is complete, pastes it into the step
# summary for the author to copy. Suppressing the capture would remove the one
# way to bootstrap or refresh the baseline.
#
# A non-zero capture status does NOT mean the file is absent: capture_timings.py
# writes its output and only then fails when too few metrics came back (its
# --min-capture-ratio). So a failed capture usually leaves a thin timings.json,
# which is worse than none as baseline material — it would commit metrics that
# were never measured. The messages below say incomplete, never missing.
#
# That thin file also says so itself, in the "capture" block capture_timings.py
# writes into it, so the CAPTURE_EXIT below is not the only record of the
# capture's health: both paste-me paths read the flag and withhold the JSON
# rather than offering an artifact this run has already called unusable.
#
# --skip-regression opts out of the comparison only (e.g. for ad-hoc local
# exploration), and with it out of the gate's verdict: a capture failure is
# reported loudly and shown in the step-status table, but does not fail a run
# whose caller asked not to be gated. With the gate active, a capture failure is
# an infrastructure error (exit 2).
#
# When the comparison does run it either prints the paste-me JSON for a
# placeholder baseline, or enforces thresholds and fails the run on regression.
TIMINGS_FILE="$REPORT_DIR/timings.json"
REGRESSION_REPORT="$REPORT_DIR/regression-report.json"
REGRESSION_EXIT=0
CAPTURE_EXIT=0

log "Step 6: Capturing OTel timings from Prometheus..."
if python3 "$SCRIPT_DIR/capture_timings.py" \
    --prometheus "http://localhost:9090" \
    --metrics "$METRICS_FILE" \
    --output "$TIMINGS_FILE" \
    --window "$REGRESSION_WINDOW" \
    --profile "$WORKLOAD_PROFILE"; then
    ok "Timings captured: $TIMINGS_FILE"
else
    CAPTURE_EXIT=2
    fail "Timing capture failed — anything it left in $TIMINGS_FILE is incomplete and must not be pasted into the baseline."
fi

if [ "$SKIP_REGRESSION" = true ]; then
    if [ "$CAPTURE_EXIT" -ne 0 ]; then
        warn "Regression gate skipped, and timing capture failed — this run cannot refresh the baseline."
    else
        warn "Regression gate skipped — timings were still captured at $TIMINGS_FILE."
    fi
elif [ "$CAPTURE_EXIT" -ne 0 ]; then
    # Without a complete capture the gate reaches no verdict, which the
    # exit-code table calls an infrastructure error rather than a regression.
    REGRESSION_EXIT="$CAPTURE_EXIT"
    fail "Skipping regression comparison — the captured timings are incomplete."
else
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
fi
fold_exit "$REGRESSION_EXIT"

# ---------------------------------------------------------------------------
# Step 7: (Optional) Run overhead benchmark
# ---------------------------------------------------------------------------
BENCHMARK_EXIT=0
if [ "$WITH_BENCHMARK" = true ]; then
    log "Step 7: Running performance benchmark..."
    bash "$SCRIPT_DIR/benchmark.sh" \
        --xrpld "$XRPLD" \
        --duration 120 \
        --nodes 3 \
        --output "$REPORT_DIR" || BENCHMARK_EXIT=$?

    if [ "$BENCHMARK_EXIT" -eq 0 ]; then
        ok "Benchmark within overhead thresholds."
    elif [ "$BENCHMARK_EXIT" -eq 1 ]; then
        # A measured threshold breach — same class as a failed check.
        fail "Benchmark exceeded overhead thresholds (exit 1)"
        fold_exit 1
    else
        # benchmark.sh could not produce a usable measurement (e.g. incomplete
        # system metrics). Reported as an infrastructure error, not a perf
        # regression: nothing was measured, so nothing was breached.
        fail "Benchmark could not measure overhead (exit $BENCHMARK_EXIT) — treated as an infrastructure error"
        fold_exit 2
    fi
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
echo "  Step statuses (0 = ok):"
echo "    Workload orchestration: $ORCHESTRATOR_EXIT"
echo "    Telemetry validation:   $VALIDATION_EXIT"
echo "    Timing capture:         $CAPTURE_EXIT"
echo "    Regression gate:        $REGRESSION_EXIT"
echo "    Overhead benchmark:     $BENCHMARK_EXIT"
echo ""
echo "==========================================================="

# FINAL_EXIT already holds the first non-zero step status (see fold_exit).
exit "$FINAL_EXIT"
