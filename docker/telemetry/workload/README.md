# Telemetry Workload Tools

Synthetic workload generation and validation tools for rippled's OpenTelemetry telemetry stack. These tools validate that all spans, metrics, dashboards, and log-trace correlation work end-to-end under controlled load.

## Quick Start

```bash
# Build rippled with telemetry enabled
conan install . --build=missing -o telemetry=True
cmake --preset default -Dtelemetry=ON
cmake --build --preset default

# Run full validation (starts everything, runs load, validates)
docker/telemetry/workload/run-full-validation.sh --xrpld .build/xrpld

# Cleanup when done
docker/telemetry/workload/run-full-validation.sh --cleanup
```

## Architecture

The validation suite runs a multi-node rippled cluster as local processes alongside
a Docker Compose telemetry stack. The cluster exercises consensus, peer-to-peer
spans (proposals, validations), and all metric pipelines.

```
run-full-validation.sh (shell orchestrator)
  |
  |-- docker-compose.workload.yaml
  |     |-- otel-collector (traces via OTLP + StatsD receiver)
  |     |-- jaeger (trace search API)
  |     |-- prometheus (metrics scraping)
  |     |-- grafana (dashboards, provisioned automatically)
  |
  |-- generate-validator-keys.sh
  |     -> validator-keys.json, validators.txt
  |
  |-- Nx xrpld nodes (local processes, full telemetry)
  |     - Each node: [telemetry] enabled=1, trace_rpc/consensus/transactions
  |     - [signing_support] true (server-side signing for tx_submitter)
  |     - Peer discovery via [ips] (not [ips_fixed]) for active peer counts
  |
  |-- workload_orchestrator.py (phased load execution)
  |     |-- rpc_load_generator.py (WebSocket RPC traffic)
  |     |-- tx_submitter.py (transaction diversity)
  |     -> workload-report.json + per-phase reports
  |
  |-- validate_telemetry.py (pass/fail checks)
  |     -> validation-report.json
  |
  |-- benchmark.sh (baseline vs telemetry comparison)
        -> benchmark-report-*.md
```

## Workload Profiles

The workload orchestrator (`workload_orchestrator.py`) reads named profiles
from `workload-profiles.json` and executes sequential load phases. Within
each phase, the RPC generator and TX submitter run concurrently.

### Available Profiles

| Profile           | Phases | Duration                     | Purpose                                                     |
| ----------------- | ------ | ---------------------------- | ----------------------------------------------------------- |
| `full-validation` | 6      | ~5 min + 1 min propagation   | Full 18-dashboard coverage with burst/idle/plateau patterns |
| `quick-smoke`     | 1      | ~30s + 30s propagation       | Fast CI smoke test                                          |
| `stress`          | 3      | ~3.5 min + 1 min propagation | Heavy sustained load for benchmarking                       |

### full-validation Phases

| Phase        | RPC Rate | TX TPS | Duration | Dashboard Coverage                              |
| ------------ | -------- | ------ | -------- | ----------------------------------------------- |
| warmup       | 5 RPS    | —      | 30s      | Node Health, Validator Health (baseline gauges) |
| steady-state | 30 RPS   | 3 TPS  | 60s      | All dashboards (plateau data)                   |
| rpc-burst    | 100 RPS  | —      | 30s      | Job Queue, RPC Performance (latency spikes)     |
| tx-flood     | 5 RPS    | 20 TPS | 30s      | Fee Market & TxQ, Transaction Overview          |
| mixed-peak   | 50 RPS   | 10 TPS | 60s      | Consensus Health, Ledger Operations             |
| cooldown     | 5 RPS    | —      | 30s      | Recovery patterns, state transitions            |

### Custom Profiles

Add profiles to `workload-profiles.json`:

```json
{
  "profiles": {
    "my-custom": {
      "description": "Custom profile for specific testing",
      "phases": [
        {
          "name": "phase-name",
          "description": "What this phase exercises",
          "duration_sec": 60,
          "rpc": { "rate": 50, "weights": { "server_info": 80, "fee": 20 } },
          "tx": { "tps": 5, "weights": { "Payment": 100 } }
        }
      ],
      "propagation_wait_sec": 30
    }
  }
}
```

Set `"rpc"` or `"tx"` to `null` to skip that generator for a phase.
Custom `"weights"` override the default command/transaction distribution.

## Tools Reference

### run-full-validation.sh

Orchestrates the complete validation pipeline. Starts the telemetry stack, starts a multi-node rippled cluster, generates load, and validates the results.

```bash
# Full validation with defaults (uses full-validation profile)
./run-full-validation.sh --xrpld /path/to/xrpld

# Quick smoke test
./run-full-validation.sh --xrpld /path/to/xrpld --profile quick-smoke

# Stress test with benchmarks
./run-full-validation.sh --xrpld /path/to/xrpld --profile stress --with-benchmark

# Skip Loki checks (if Phase 8 not deployed)
./run-full-validation.sh --xrpld /path/to/xrpld --skip-loki
```

### workload_orchestrator.py

Reads a named profile from `workload-profiles.json` and executes sequential
load phases. Within each phase, `rpc_load_generator.py` and `tx_submitter.py`
run as concurrent subprocesses. Produces per-phase reports and a combined
summary.

```bash
# Run with a specific profile
python3 workload_orchestrator.py --profile full-validation

# Multiple endpoints
python3 workload_orchestrator.py --profile full-validation \
    --endpoints ws://localhost:6006 ws://localhost:6007

# Save combined report
python3 workload_orchestrator.py --profile stress --report /tmp/report.json
```

### rpc_load_generator.py

Generates RPC traffic matching realistic production distribution. Uses
rippled's **native WebSocket command format** (`{"command": ...}`) with flat
parameters — the same format as `tx_submitter.py`.

- 40% health checks (server_info, fee)
- 30% wallet queries (account_info, account_lines, account_objects)
- 15% explorer queries (ledger, ledger_data)
- 10% transaction lookups (tx, account_tx)
- 5% DEX queries (book_offers, amm_info)

```bash
# Basic usage
python3 rpc_load_generator.py --endpoints ws://localhost:6006 --rate 50 --duration 120

# Multiple endpoints (round-robin)
python3 rpc_load_generator.py \
    --endpoints ws://localhost:6006 ws://localhost:6007 \
    --rate 100 --duration 300

# Custom weights
python3 rpc_load_generator.py --endpoints ws://localhost:6006 \
    --weights '{"server_info": 80, "account_info": 20}'
```

### tx_submitter.py

Submits diverse transaction types to exercise the full span and metric surface.
Uses rippled's **native WebSocket command format** (`{"command": ...}`) rather
than JSON-RPC format. The response payload is inside the `"result"` key, with
`"status"` at the top level.

Supported transaction types:

- Payment (XRP transfers) — exercises `tx.process`, `tx.receive`, `tx.apply`
- OfferCreate / OfferCancel (DEX activity)
- TrustSet (trust line creation)
- NFTokenMint / NFTokenCreateOffer (NFT activity)
- EscrowCreate / EscrowFinish (escrow lifecycle)
- AMMCreate / AMMDeposit (AMM pool operations)

Requires `[signing_support] true` in the node config for server-side signing.

```bash
# Basic usage
python3 tx_submitter.py --endpoint ws://localhost:6006 --tps 5 --duration 120

# Custom mix
python3 tx_submitter.py --endpoint ws://localhost:6006 \
    --weights '{"Payment": 60, "OfferCreate": 20, "TrustSet": 20}'
```

### validate_telemetry.py

Automated validation that all expected telemetry data exists. Every metric and span is required — if it doesn't fire, the validation fails.

- **Span validation**: All span types from `expected_spans.json` with required attributes and parent-child hierarchies
- **Metric validation**: All metrics from `expected_metrics.json` — SpanMetrics, StatsD gauges/counters/histograms, Phase 9 OTLP metrics. Every listed metric must have > 0 series. Uses the Prometheus `/api/v1/series` endpoint (not instant queries) to avoid false negatives from stale gauges.
- **Log-trace correlation**: trace_id/span_id in Loki logs (requires Loki)
- **Dashboard validation**: All 10 Grafana dashboards load with panels

```bash
# Run all validations
python3 validate_telemetry.py --report /tmp/report.json

# Skip Loki checks
python3 validate_telemetry.py --skip-loki --report /tmp/report.json
```

### benchmark.sh

Compares baseline (no telemetry) vs telemetry-enabled performance:

```bash
./benchmark.sh --xrpld /path/to/xrpld --duration 300
```

Thresholds (configurable via environment):

| Metric            | Threshold | Env Variable                |
| ----------------- | --------- | --------------------------- |
| CPU overhead      | < 3%      | BENCH_CPU_OVERHEAD_PCT      |
| Memory overhead   | < 5MB     | BENCH_MEM_OVERHEAD_MB       |
| RPC p99 latency   | < 2ms     | BENCH_RPC_LATENCY_IMPACT_MS |
| Throughput impact | < 5%      | BENCH_TPS_IMPACT_PCT        |
| Consensus impact  | < 1%      | BENCH_CONSENSUS_IMPACT_PCT  |

## Reading Validation Reports

The validation report (`validation-report.json`) is structured as:

```json
{
  "summary": {
    "total": 45,
    "passed": 42,
    "failed": 3,
    "all_passed": false
  },
  "checks": [
    {
      "name": "span.rpc.request",
      "category": "span",
      "passed": true,
      "message": "rpc.request: 15 traces found",
      "details": { "trace_count": 15 }
    }
  ]
}
```

Categories:

- **span**: Span type existence and attribute validation
- **metric**: Prometheus metric existence
- **log**: Log-trace correlation checks
- **dashboard**: Grafana dashboard accessibility

## CI Integration

The validation runs as a GitHub Actions workflow (`.github/workflows/telemetry-validation.yml`):

- Triggered manually or on pushes to telemetry branches
- Builds rippled, starts the full stack, runs load, validates
- Uploads reports as artifacts
- Posts summary to PR

## Configuration Files

| File                     | Purpose                                                       |
| ------------------------ | ------------------------------------------------------------- |
| `workload-profiles.json` | Named load profiles with phase definitions                    |
| `expected_spans.json`    | Span inventory (names, attributes, hierarchies, config flags) |
| `expected_metrics.json`  | Metric inventory — every listed metric must be present        |
| `test_accounts.json`     | Test account roles (keys generated at runtime)                |
| `requirements.txt`       | Python dependencies                                           |

### expected_metrics.json Format

```json
{
  "category_name": {
    "description": "Human-readable description.",
    "metrics": ["metric_1", "metric_2"]
  }
}
```

Every metric listed must produce > 0 Prometheus series during the validation run. If a metric doesn't fire, the workload generators need to produce enough load to trigger it.

### expected_spans.json Format

Each span entry defines its name, category, parent (for hierarchy validation),
required attributes, and the `config_flag` that must be enabled:

```json
{
  "name": "rpc.request",
  "category": "rpc",
  "parent": null,
  "required_attributes": ["rpc.method", "rpc.grpc.status_code"],
  "config_flag": "trace_rpc"
}
```

## Node Configuration Notes

The orchestrator (`run-full-validation.sh`) generates node configs with:

- `[telemetry] enabled=1` with all trace categories (`trace_rpc`, `trace_consensus`, `trace_transactions`)
- `[signing_support] true` — required for `tx_submitter.py` to submit signed transactions via WebSocket
- `[ips]` (not `[ips_fixed]`) — ensures peer connections are counted in `Peer_Finder_Active_Inbound/Outbound_Peers` metrics (fixed peers are excluded from these counters by design)

## StatsD Gauge Behaviour

Beast::insight StatsD gauges only emit when their value _changes_ from the previous sample. This can cause two problems in the validation environment:

1. **Initial-zero gauges** — if a gauge value is 0 from startup and never changes, the gauge would never emit. To address this, `StatsDGaugeImpl` initializes `m_dirty = true`, ensuring the first flush always emits the initial value.
2. **Stale gauges** — once a gauge stabilizes (e.g., peer count stays at 1), it stops emitting new data points. Prometheus marks it stale after ~5 minutes. The validation script uses the Prometheus `/api/v1/series` endpoint instead of instant queries to catch such gauges.
