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
#     "consensus_round_p95_ms": 3200,
#     "samples": 60
#   }

set -euo pipefail

# ---------------------------------------------------------------------------
# Colored output helpers
# ---------------------------------------------------------------------------
log()  { printf "\033[1;34m[METRICS]\033[0m %s\n" "$*"; }
ok()   { printf "\033[1;32m[METRICS]\033[0m %s\n" "$*"; }
die()  { printf "\033[1;31m[METRICS]\033[0m %s\n" "$*" >&2; exit 1; }

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

IFS=',' read -ra RPC_PORTS <<< "$RPC_PORTS_CSV"
SAMPLE_INTERVAL=5
SAMPLES=$((DURATION / SAMPLE_INTERVAL))

log "Collecting metrics for ${DURATION}s (${SAMPLES} samples, ${#RPC_PORTS[@]} nodes)..."

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
        -d '{"method":"server_info"}' 2>/dev/null \
        | jq -r '.result.info.validated_ledger.seq // 0' 2>/dev/null || echo 0)
    if [ "$seq" -gt "$INITIAL_SEQ" ]; then
        INITIAL_SEQ=$seq
    fi
done
log "Initial validated ledger seq: $INITIAL_SEQ"

# ---------------------------------------------------------------------------
# Sampling loop
# ---------------------------------------------------------------------------
for sample in $(seq 1 "$SAMPLES"); do
    # Collect CPU usage for xrpld processes.
    # Uses ps to find all xrpld processes and average their CPU%.
    cpu_sum=0
    cpu_count=0
    while IFS= read -r line; do
        cpu_val=$(echo "$line" | awk '{print $1}')
        if [ -n "$cpu_val" ] && [ "$cpu_val" != "0.0" ]; then
            cpu_sum=$(echo "$cpu_sum + $cpu_val" | bc 2>/dev/null || echo "$cpu_sum")
            cpu_count=$((cpu_count + 1))
        fi
    done < <(ps aux 2>/dev/null | grep '[x]rpld' | awk '{print $3}')

    if [ "$cpu_count" -gt 0 ]; then
        cpu_avg=$(echo "scale=2; $cpu_sum / $cpu_count" | bc 2>/dev/null || echo "0")
        echo "$cpu_avg" >> "$CPU_FILE"
    fi

    # Collect memory RSS for xrpld processes.
    while IFS= read -r line; do
        rss_kb=$(echo "$line" | awk '{print $1}')
        if [ -n "$rss_kb" ] && [ "$rss_kb" != "0" ]; then
            rss_mb=$(echo "scale=2; $rss_kb / 1024" | bc 2>/dev/null || echo "0")
            echo "$rss_mb" >> "$MEM_FILE"
        fi
    done < <(ps aux 2>/dev/null | grep '[x]rpld' | awk '{print $6}')

    # Collect RPC latency from each node.
    for port in "${RPC_PORTS[@]}"; do
        start_ms=$(date +%s%N)
        curl -sf "http://localhost:$port" \
            -d '{"method":"server_info"}' > /dev/null 2>&1 || true
        end_ms=$(date +%s%N)
        latency_ms=$(( (end_ms - start_ms) / 1000000 ))
        echo "$latency_ms" >> "$RPC_FILE"
    done

    # Record current validated ledger seq.
    for port in "${RPC_PORTS[@]}"; do
        seq=$(curl -sf "http://localhost:$port" \
            -d '{"method":"server_info"}' 2>/dev/null \
            | jq -r '.result.info.validated_ledger.seq // 0' 2>/dev/null || echo 0)
        echo "$seq" >> "$LEDGER_FILE"
        break  # Only need one node's seq per sample.
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

# CPU average.
if [ -s "$CPU_FILE" ]; then
    CPU_AVG=$(awk '{ sum += $1; n++ } END { if (n>0) printf "%.2f", sum/n; else print "0" }' "$CPU_FILE")
else
    CPU_AVG="0"
fi

# Memory peak RSS (MB).
if [ -s "$MEM_FILE" ]; then
    MEM_PEAK=$(sort -n "$MEM_FILE" | tail -1)
else
    MEM_PEAK="0"
fi

# RPC latency p99 (ms).
if [ -s "$RPC_FILE" ]; then
    RPC_COUNT=$(wc -l < "$RPC_FILE")
    P99_INDEX=$(echo "scale=0; $RPC_COUNT * 99 / 100" | bc)
    RPC_P99=$(sort -n "$RPC_FILE" | sed -n "${P99_INDEX}p")
    [ -z "$RPC_P99" ] && RPC_P99="0"
else
    RPC_P99="0"
fi

# TPS calculation from ledger sequence advancement.
FINAL_SEQ=0
for port in "${RPC_PORTS[@]}"; do
    seq=$(curl -sf "http://localhost:$port" \
        -d '{"method":"server_info"}' 2>/dev/null \
        | jq -r '.result.info.validated_ledger.seq // 0' 2>/dev/null || echo 0)
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
    TPS=$(echo "scale=2; $LEDGER_ADVANCE / $ELAPSED" | bc 2>/dev/null || echo "0")
else
    TPS="0"
fi

# Consensus round time p95 (from ledger close interval).
# Approximate by looking at ledger sequence progression intervals.
if [ -s "$LEDGER_FILE" ]; then
    # Calculate intervals between consecutive ledger sequences.
    LEDGER_COUNT=$(wc -l < "$LEDGER_FILE")
    # Rough estimate: DURATION / number_of_distinct_ledgers * 1000 ms
    UNIQUE_LEDGERS=$(sort -u "$LEDGER_FILE" | wc -l)
    if [ "$UNIQUE_LEDGERS" -gt 1 ]; then
        CONSENSUS_P95=$(echo "scale=0; $DURATION * 1000 / ($UNIQUE_LEDGERS - 1)" | bc 2>/dev/null || echo "0")
    else
        CONSENSUS_P95="0"
    fi
else
    CONSENSUS_P95="0"
fi

# ---------------------------------------------------------------------------
# Write output JSON
# ---------------------------------------------------------------------------
cat > "$OUTPUT_FILE" <<EOF_JSON
{
  "cpu_pct_avg": $CPU_AVG,
  "memory_rss_mb_peak": $MEM_PEAK,
  "rpc_p99_ms": $RPC_P99,
  "tps": $TPS,
  "consensus_round_p95_ms": $CONSENSUS_P95,
  "samples": $SAMPLES,
  "duration_seconds": $DURATION,
  "node_count": ${#RPC_PORTS[@]},
  "initial_ledger_seq": $INITIAL_SEQ,
  "final_ledger_seq": $FINAL_SEQ
}
EOF_JSON

ok "Metrics written to $OUTPUT_FILE"
cat "$OUTPUT_FILE"
