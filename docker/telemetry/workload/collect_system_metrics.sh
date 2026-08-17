#!/usr/bin/env bash
# collect_system_metrics.sh — Collect CPU, memory, and RPC latency metrics
# from running xrpld nodes for benchmark comparison.
#
# Samples system metrics at regular intervals and writes a JSON summary.
# Used by benchmark.sh for baseline vs telemetry comparison.
#
# Usage:
#   ./collect_system_metrics.sh <rpc_ports_csv> <duration_seconds> <output_file>
#
# Example:
#   ./collect_system_metrics.sh "5005,5006,5007" 300 /tmp/metrics-baseline.json
#
# Output JSON format:
#   {
#     "cpu_pct_avg": 12.5,
#     "memory_rss_mb_peak": 450.2,
#     "rpc_p99_ms": 15.3,
#     "tps": 4.8,
#     "consensus_round_mean_ms": 3200,
#     "metrics_complete": true,
#     "samples": 60
#   }
#
# Exit codes:
#   0  Every metric was measured; "metrics_complete" is true.
#   1  Cannot run at all: bad arguments, no GNU date with %N, or a failed
#      process sample. No output file is written.
#   3  Output file was written, but at least one measurement source was empty,
#      so the affected metrics are 0 placeholders and "metrics_complete" is
#      false. Callers must treat this as inconclusive, never as a pass.

set -euo pipefail

# ---------------------------------------------------------------------------
# Colored output helpers
# ---------------------------------------------------------------------------
log() { printf "\033[1;34m[METRICS]\033[0m %s\n" "$*"; }
ok() { printf "\033[1;32m[METRICS]\033[0m %s\n" "$*"; }
# Warnings go to stderr so they never mix into the JSON echoed on stdout.
warn() { printf "\033[1;33m[METRICS]\033[0m %s\n" "$*" >&2; }
die() {
    printf "\033[1;31m[METRICS]\033[0m %s\n" "$*" >&2
    exit 1
}

# ---------------------------------------------------------------------------
# Argument parsing
# ---------------------------------------------------------------------------
usage() {
    echo "Usage: $0 <rpc_ports_csv> <duration_seconds> <output_file>"
    echo ""
    echo "Arguments:"
    echo "  rpc_ports_csv     Comma-separated RPC ports (e.g., 5005,5006,5007)"
    echo "  duration_seconds  How long to collect metrics"
    echo "  output_file       Path to write JSON results"
    exit 1
}

if [ $# -lt 3 ]; then
    usage
fi

RPC_PORTS_CSV="$1"
DURATION="$2"
OUTPUT_FILE="$3"

IFS=',' read -ra RPC_PORTS <<<"$RPC_PORTS_CSV"
SAMPLE_INTERVAL=5

# Reject anything the sample arithmetic cannot use, instead of silently
# treating it as 0.
case "$DURATION" in
    '' | *[!0-9]*) die "duration_seconds must be a positive integer, got '$DURATION'" ;;
esac

# Normalise to base 10 once, right after the digits check. Bash arithmetic
# reads a leading zero as octal, so "08" aborted with "value too great for
# base" and "0100" was silently taken as 64. Doing it here also keeps the
# value a valid JSON number in the output below, where "08" is not.
DURATION=$((10#$DURATION))

# Round up, so a duration shorter than one interval still takes one sample
# rather than truncating to zero and emitting an all-zero JSON.
SAMPLES=$(((DURATION + SAMPLE_INTERVAL - 1) / SAMPLE_INTERVAL))
if [ "$SAMPLES" -lt 1 ]; then
    die "duration_seconds=$DURATION yields $SAMPLES samples; need at least 1"
fi

log "Collecting metrics for ${DURATION}s (${SAMPLES} samples, ${#RPC_PORTS[@]} nodes)..."

# ---------------------------------------------------------------------------
# Nanosecond clock
#
# GNU date supports "+%s%N". BSD/macOS date has no %N and echoes it back
# literally, which used to abort the sampling loop under `set -e` and still
# exit 0 with an all-zero JSON — a silent false pass.
#
# The clock has to be cheap as well as precise, because the latency it
# measures is compared against a 2 ms threshold. Measured on a dev box: `date
# +%s%N` costs ~1.2 ms per call, forking python3 for the same value ~13 ms.
# Two calls bracket every request, so a python3 fallback would add ~26 ms of
# its own overhead to a 2 ms budget and make the number meaningless. There is
# no cheap alternative worth having, so probe once and refuse to run without
# GNU date rather than report a figure that is quietly an order of magnitude
# wrong.
# ---------------------------------------------------------------------------
if [[ ! "$(date +%s%N 2>/dev/null)" =~ ^[0-9]+$ ]]; then
    die "GNU coreutils date with %N is required for RPC latency timing; this date does not support it"
fi

# Echo the current time in nanoseconds since the epoch.
now_ns() {
    date +%s%N
}

# ---------------------------------------------------------------------------
# Temporary files for aggregation
# ---------------------------------------------------------------------------
TMPDIR_METRICS="$(mktemp -d)"
CPU_FILE="$TMPDIR_METRICS/cpu.txt"
MEM_FILE="$TMPDIR_METRICS/mem.txt"
RPC_FILE="$TMPDIR_METRICS/rpc.txt"
LEDGER_FILE="$TMPDIR_METRICS/ledger.txt"

touch "$CPU_FILE" "$MEM_FILE" "$RPC_FILE" "$LEDGER_FILE"

cleanup() {
    rm -rf "$TMPDIR_METRICS"
}
trap cleanup EXIT

# ---------------------------------------------------------------------------
# Get initial ledger sequence for TPS calculation
# ---------------------------------------------------------------------------
INITIAL_SEQ=0
INITIAL_TIME=$(date +%s)
for port in "${RPC_PORTS[@]}"; do
    seq=$(curl -sf "http://localhost:$port" \
        -d '{"method":"server_info"}' 2>/dev/null |
        jq -r '.result.info.validated_ledger.seq // 0' 2>/dev/null || echo 0)
    if [ "$seq" -gt "$INITIAL_SEQ" ]; then
        INITIAL_SEQ=$seq
    fi
done
log "Initial validated ledger seq: $INITIAL_SEQ"

# ---------------------------------------------------------------------------
# Sampling loop
# ---------------------------------------------------------------------------
for sample in $(seq 1 "$SAMPLES"); do
    # Sample CPU% and RSS for the xrpld processes. One ps pass feeds both
    # files, so the two numbers always come from the same instant.
    #
    # Selection is on argv[0]'s basename, NOT on "the command line mentions
    # xrpld". The loose form matched every bystander whose command line
    # happened to contain the string: this harness's own launcher (invoked as
    # `--xrpld .build/xrpld`, whose argv[0] is bash), an editor's clangd, and
    # any shell sitting in a directory with xrpld in its path. Measured
    # against a live 5-node cluster, that pulled in 8-15 processes instead of
    # 5, halved the CPU average with idle bystanders, and reported clangd's
    # 1.1 GB RSS as xrpld's peak against a 5 MB threshold. `ps -C xrpld` is
    # not an alternative: xrpld renames itself, so its comm is "xrpld-main"
    # and -C matches nothing. "rippled" is accepted alongside "xrpld" so a
    # rename of the binary cannot silently zero the collector.
    #
    # Scope is the whole host, as it always was: a second xrpld from another
    # checkout is sampled too. Only run a benchmark on a box with one cluster.
    #
    # A %cpu of exactly 0.0 is a real reading and is counted — dropping idle
    # samples would inflate the average — while non-numeric output is
    # rejected by the pattern. An RSS of 0 is not a live process, so it
    # contributes no memory sample; counting it would leave the file non-empty
    # and mark a dead cluster's 0 MB peak as a complete measurement.
    ps -eo %cpu=,rss=,args= |
        awk -v cpu_file="$CPU_FILE" -v mem_file="$MEM_FILE" '
            $3 !~ /(^|\/)(xrpld|rippled)$/ { next }
            $1 ~ /^[0-9]+(\.[0-9]+)?$/ { cpu_sum += $1; cpu_n++ }
            $2 ~ /^[0-9]+$/ && $2 + 0 > 0 { printf("%.2f\n", $2 / 1024) >> mem_file }
            END { if (cpu_n > 0) printf("%.2f\n", cpu_sum / cpu_n) >> cpu_file }
        ' || die "process sampling failed on sample $sample/$SAMPLES"

    # Collect RPC latency from each node. Only a successful call is a latency
    # measurement: a refused connection returns in well under a millisecond,
    # and recording that as ~0 ms would pull the reported p99 down.
    for port in "${RPC_PORTS[@]}"; do
        start_ns=$(now_ns)
        if curl -sf "http://localhost:$port" \
            -d '{"method":"server_info"}' >/dev/null 2>&1; then
            end_ns=$(now_ns)
            latency_ms=$(((end_ns - start_ns) / 1000000))
            echo "$latency_ms" >>"$RPC_FILE"
        fi
    done

    # Record current validated ledger seq.
    for port in "${RPC_PORTS[@]}"; do
        seq=$(curl -sf "http://localhost:$port" \
            -d '{"method":"server_info"}' 2>/dev/null |
            jq -r '.result.info.validated_ledger.seq // 0' 2>/dev/null || echo 0)
        echo "$seq" >>"$LEDGER_FILE"
        break # Only need one node's seq per sample.
    done

    # Progress indicator.
    if [ $((sample % 10)) -eq 0 ]; then
        log "  Sample $sample/$SAMPLES..."
    fi

    sleep "$SAMPLE_INTERVAL"
done

# ---------------------------------------------------------------------------
# Compute aggregated metrics
# ---------------------------------------------------------------------------
log "Computing aggregated metrics..."

# Cleared by any empty measurement source. A 0 metric is otherwise
# indistinguishable from a real reading, so the flag is exported in the JSON
# and drives the exit-3 contract documented in the header.
METRICS_COMPLETE=true

# CPU average.
if [ -s "$CPU_FILE" ]; then
    CPU_AVG=$(awk '{ sum += $1; n++ } END { if (n>0) printf "%.2f", sum/n; else print "0" }' "$CPU_FILE")
else
    # Now that the selector cannot match this harness's own processes, an
    # empty file means no xrpld process was running for any sample.
    warn "No CPU samples collected (no xrpld process matched); cpu_pct_avg is a 0 placeholder"
    CPU_AVG="0"
    METRICS_COMPLETE=false
fi

# Memory peak RSS (MB).
if [ -s "$MEM_FILE" ]; then
    MEM_PEAK=$(sort -n "$MEM_FILE" | tail -1)
else
    warn "No memory samples collected (no xrpld process matched); memory_rss_mb_peak is a 0 placeholder"
    MEM_PEAK="0"
    METRICS_COMPLETE=false
fi

# RPC latency p99 (ms).
if [ -s "$RPC_FILE" ]; then
    RPC_COUNT=$(wc -l <"$RPC_FILE")
    # Nearest-rank p99: ceil(count * 99 / 100), clamped into [1, count].
    # Integer arithmetic avoids both the floor bias of the old bc expression
    # and the bc dependency, and the lower clamp keeps sed off line address 0
    # (a file with no trailing newline makes wc -l report 0).
    P99_INDEX=$(((RPC_COUNT * 99 + 99) / 100))
    if [ "$P99_INDEX" -lt 1 ]; then
        P99_INDEX=1
    fi
    if [ "$P99_INDEX" -gt "$RPC_COUNT" ]; then
        P99_INDEX="$RPC_COUNT"
    fi
    RPC_P99=$(sort -n "$RPC_FILE" | sed -n "${P99_INDEX}p")
    if [ -z "$RPC_P99" ]; then
        warn "RPC latency file has no line $P99_INDEX; rpc_p99_ms is a 0 placeholder"
        RPC_P99="0"
        METRICS_COMPLETE=false
    fi
else
    warn "No successful RPC probes; rpc_p99_ms is a 0 placeholder"
    RPC_P99="0"
    METRICS_COMPLETE=false
fi

# TPS calculation from ledger sequence advancement.
FINAL_SEQ=0
for port in "${RPC_PORTS[@]}"; do
    seq=$(curl -sf "http://localhost:$port" \
        -d '{"method":"server_info"}' 2>/dev/null |
        jq -r '.result.info.validated_ledger.seq // 0' 2>/dev/null || echo 0)
    if [ "$seq" -gt "$FINAL_SEQ" ]; then
        FINAL_SEQ=$seq
    fi
done
FINAL_TIME=$(date +%s)
ELAPSED=$((FINAL_TIME - INITIAL_TIME))
LEDGER_ADVANCE=$((FINAL_SEQ - INITIAL_SEQ))
if [ "$ELAPSED" -gt 0 ] && [ "$LEDGER_ADVANCE" -gt 0 ]; then
    # Rough TPS: assume ~avg_txs_per_ledger * ledgers / elapsed.
    # Without tx count, use ledger close rate as proxy.
    #
    # awk rather than bc, because bc omits the leading zero: `scale=2` prints
    # ".25", not "0.25", and a bare ".25" is not valid JSON. A value below 1 is
    # the normal case here, not an edge case — ledgers close every few seconds,
    # so advance/elapsed is well under 1 for any realistic window. jq happens
    # to accept the malformed form, which is why it survived earlier checks,
    # but a strict parser rejects the whole file. awk's %.2f always pads.
    TPS=$(awk -v a="$LEDGER_ADVANCE" -v b="$ELAPSED" 'BEGIN { printf "%.2f", a / b }')
else
    TPS="0"
fi

# Mean inter-ledger interval in ms: DURATION / (distinct ledgers - 1) * 1000.
#
# This is a MEAN, not a percentile — the JSON key says so. It is also aliased
# by the sample loop: LEDGER_FILE gets one sequence per sample, so at a
# SAMPLE_INTERVAL of 5 s the series cannot resolve a close interval faster
# than that (a ~4 s close is invisible). Read it as a coarse trend only.
if [ -s "$LEDGER_FILE" ]; then
    UNIQUE_LEDGERS=$(sort -u "$LEDGER_FILE" | wc -l)
    # The > 1 test also keeps the divisor below at 1 or more.
    if [ "$UNIQUE_LEDGERS" -gt 1 ]; then
        CONSENSUS_MEAN=$(echo "scale=0; $DURATION * 1000 / ($UNIQUE_LEDGERS - 1)" | bc 2>/dev/null || echo "0")
    else
        warn "Ledger seq never advanced ($UNIQUE_LEDGERS distinct); consensus_round_mean_ms is a 0 placeholder"
        CONSENSUS_MEAN="0"
        METRICS_COMPLETE=false
    fi
else
    warn "No ledger samples collected; consensus_round_mean_ms is a 0 placeholder"
    CONSENSUS_MEAN="0"
    METRICS_COMPLETE=false
fi

# ---------------------------------------------------------------------------
# Write output JSON
# ---------------------------------------------------------------------------
cat >"$OUTPUT_FILE" <<EOF_JSON
{
  "cpu_pct_avg": $CPU_AVG,
  "memory_rss_mb_peak": $MEM_PEAK,
  "rpc_p99_ms": $RPC_P99,
  "tps": $TPS,
  "consensus_round_mean_ms": $CONSENSUS_MEAN,
  "metrics_complete": $METRICS_COMPLETE,
  "samples": $SAMPLES,
  "duration_seconds": $DURATION,
  "node_count": ${#RPC_PORTS[@]},
  "initial_ledger_seq": $INITIAL_SEQ,
  "final_ledger_seq": $FINAL_SEQ
}
EOF_JSON

ok "Metrics written to $OUTPUT_FILE"
cat "$OUTPUT_FILE"

# The file is always written first so CI still has an artifact to publish.
if [ "$METRICS_COMPLETE" != "true" ]; then
    warn "metrics_complete=false — treat this run as inconclusive (exit 3)"
    exit 3
fi
