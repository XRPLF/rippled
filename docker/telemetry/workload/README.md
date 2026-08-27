# Telemetry Workload Tools

Synthetic workload generation and validation tools for xrpld's OpenTelemetry telemetry stack. These tools validate that all spans, metrics, dashboards, and log-trace correlation work end-to-end under controlled load.

## Quick Start

```bash
# Build xrpld with telemetry enabled (see BUILD.md for the full flow)
mkdir -p .build && cd .build
conan install .. --output-folder . --build missing \
    --settings build_type=Release -o telemetry=True
cmake -DCMAKE_TOOLCHAIN_FILE:FILEPATH=build/generators/conan_toolchain.cmake \
    -DCMAKE_BUILD_TYPE=Release -Dtelemetry=ON ..
cmake --build . --parallel "$(nproc)" --target xrpld
cd ..

# Run full validation (starts everything, runs load, validates)
docker/telemetry/workload/run-full-validation.sh --xrpld .build/xrpld

# Cleanup when done
docker/telemetry/workload/run-full-validation.sh --cleanup
```

## Architecture

The validation suite runs a multi-node xrpld cluster as local processes alongside
a Docker Compose telemetry stack. The cluster exercises consensus, peer-to-peer
spans (proposals, validations), and all metric pipelines.

```
run-full-validation.sh (shell orchestrator)
  |
  |-- docker-compose.workload.yaml
  |     |-- otel-collector (otlp receiver: traces + beast::insight metrics;
  |     |                  filelog receiver: node debug.log -> Loki)
  |     |-- tempo (trace backend + TraceQL search API)
  |     |-- prometheus (metrics scraping)
  |     |-- loki (log aggregation for log-trace correlation)
  |     |-- grafana (dashboards, provisioned automatically)
  |
  |-- generate-validator-keys.sh
  |     -> validator-keys.json, validators.txt
  |
  |-- Nx xrpld nodes (local processes, full telemetry)
  |     - Each node: [telemetry] enabled=1, all 5 trace_* categories on
  |     - [insight] server=otel (beast::insight metrics over OTLP, no StatsD)
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
        |-- collect_system_metrics.sh (per-leg CPU/RSS/latency/TPS sampling)
        -> benchmark-report-*.md
```

## Workload Profiles

The workload orchestrator (`workload_orchestrator.py`) reads named profiles
from `workload-profiles.json` and executes sequential load phases. Within
each phase, the RPC generator and TX submitter run concurrently.

### Available Profiles

| Profile           | Phases | Duration                    | Purpose                                                                                          |
| ----------------- | ------ | --------------------------- | ------------------------------------------------------------------------------------------------ |
| `full-validation` | 7      | 4.5 min + 1 min propagation | Coverage for the full asserted span/metric/dashboard inventory, with burst/idle/plateau patterns |
| `quick-smoke`     | 1      | 30s + 30s propagation       | Fast CI smoke test                                                                               |
| `stress`          | 3      | 3.5 min + 1 min propagation | Heavy sustained load for benchmarking                                                            |

Durations are the sum of the phase `duration_sec` values in
`workload-profiles.json` plus that profile's `propagation_wait_sec`; they exclude
cluster startup and the validation pass itself.

### full-validation Phases

| Phase        | RPC Rate           | TX TPS | Duration | Dashboard Coverage                                                                                                                                                                               |
| ------------ | ------------------ | ------ | -------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| warmup       | 5 RPS              | —      | 30s      | Node Health, Validator Health (baseline gauges)                                                                                                                                                  |
| steady-state | 30 RPS             | 3 TPS  | 60s      | All dashboards (plateau data)                                                                                                                                                                    |
| rpc-burst    | 100 RPS            | —      | 30s      | Job Queue, RPC Performance (latency spikes)                                                                                                                                                      |
| tx-flood     | 5 RPS              | 20 TPS | 30s      | Fee Market & TxQ, Transaction Overview                                                                                                                                                           |
| txq-burst    | 5 RPS (100% `fee`) | 60 TPS | 30s      | Fee Market & TxQ — single-type Payment burst that forces open-ledger fee escalation and TxQ queueing, exercising the `txq.*` spans (`txq.enqueue`, `txq.accept`, `txq.accept_tx`, `txq.cleanup`) |
| mixed-peak   | 50 RPS             | 10 TPS | 60s      | Consensus Health, Ledger Operations                                                                                                                                                              |
| cooldown     | 5 RPS              | —      | 30s      | Recovery patterns, state transitions                                                                                                                                                             |

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

Orchestrates the complete validation pipeline. Starts the telemetry stack, starts a multi-node xrpld cluster, generates load, and validates the results.

```bash
# Full validation with defaults (uses full-validation profile)
./run-full-validation.sh --xrpld /path/to/xrpld

# Quick smoke test
./run-full-validation.sh --xrpld /path/to/xrpld --profile quick-smoke

# Stress test with benchmarks
./run-full-validation.sh --xrpld /path/to/xrpld --profile stress --with-benchmark

# Skip Loki checks (if log export is not deployed)
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
xrpld's **native WebSocket command format** (`{"command": ...}`) with flat
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
Uses xrpld's **native WebSocket command format** (`{"command": ...}`) rather
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

Automated validation that all expected telemetry data exists. Every metric in `expected_metrics.json` is required — if it doesn't fire, the validation fails. Spans are required unless the entry carries `"optional": true`.

- **Span validation**: All span types from `expected_spans.json` with required attributes and parent-child hierarchies. Entries marked `"optional": true` only fire under traffic the harness may not produce (HTTP/JSON-RPC client, gRPC client, path-finding RPC — see [Pathfinding is not exercised](#pathfinding-is-not-exercised) — missing-ledger fetch, mode transitions); their absence is recorded as a passing skip, not a failure.
- **Metric validation**: All metrics from `expected_metrics.json` — SpanMetrics, `beast::insight` gauges/counters/histograms, `MetricsRegistry` OTLP metrics. Every listed metric must have > 0 series. Uses the Prometheus `/api/v1/series` endpoint (not instant queries), polled until the metric appears or the poll window elapses, so a late-populating or quiet series is not a false negative.
- **Log-trace correlation**: trace_id/span_id in Loki logs (requires Loki). The two checks are `log.trace_id_present` and `log.trace_id_cross_reference`, and they exist only when `--skip-loki` is **not** passed — `run_validation()` builds them inside an `if not skip_loki` branch, so with the flag they are absent from the report rather than reported as skipped. **CI always passes `--skip-loki`, so these two are never exercised there** — see [CI Integration](#ci-integration).
- **Dashboard validation**: Every dashboard uid listed under `grafana_dashboards.uids` in `expected_metrics.json` loads with panels. That list currently covers **all 15** dashboards provisioned in `docker/telemetry/grafana/dashboards/`. Note the scope of this check: it asks the Grafana API whether the dashboard exists and returns a panel count — it does **not** run the panels' queries, so a dashboard can pass here while individual panels render empty.

```bash
# Run all validations
python3 validate_telemetry.py --report /tmp/report.json

# Skip Loki checks
python3 validate_telemetry.py --skip-loki --report /tmp/report.json
```

### OTel Timings Regression Gate

`capture_timings.py` + `compare_to_baseline.py` implement a regression gate
that compares OTel-derived per-span/per-RPC/per-job timings against a
committed baseline. Unlike `benchmark.sh` (which measures the overhead of
enabling telemetry on the current binary), this gate catches **xrpld
performance regressions over time** by diffing against a stored baseline
from a prior run.

How it runs inside the validation pipeline:

1. `run-full-validation.sh` executes the normal workload and validation suite.
2. After validation, `capture_timings.py` queries Prometheus for every
   metric `regression-metrics.json` declares and does not list in
   `excluded_keys`, then writes `reports/timings.json`. That file records
   how much of the declared surface actually came back, in a `capture`
   block alongside `metrics` — see [Capture completeness](#capture-completeness).
3. `compare_to_baseline.py` reads `timings.json`,
   `baselines/baseline-timings.json`, and `regression-thresholds.json`,
   then either:
   - Prints the paste-me JSON block (when the baseline is a placeholder
     or empty and the capture is complete) and exits 0. An incomplete
     capture is refused instead, with exit 2 and nothing on stdout.
   - Prints a delta table, writes `reports/regression-report.json`, and
     exits non-zero if any metric breached both the percentage AND
     absolute bound.

Bootstrapping a baseline:

1. Push the branch. The `Telemetry Validation` CI run prints the full
   timings JSON under "Paste into `baselines/baseline-timings.json`" in
   the workflow Step Summary.
2. Open a PR copying that JSON block verbatim into
   `baselines/baseline-timings.json`. Reviewer approval is the audit gate.
3. Subsequent runs compare against it; the gate fails on regression.

#### Capture completeness

`capture_timings.py` writes `timings.json` and only then enforces
`--min-capture-ratio`, so a run that reached too little of Prometheus still
leaves a file behind — one that exists, parses, and carries every declared key,
some of them `null`. Nothing about it looks degraded.

Every capture therefore states its own verdict:

```json
"capture": { "declared": 20, "captured": 20, "min_ratio": 0.5, "complete": true }
```

`complete` is exactly the condition `capture_timings.py` exits 0 on. Both
paste-me paths — the workflow's Step Summary block and `compare_to_baseline.py`
— read that flag and withhold the JSON unless it is `true`, because the
placeholder path is the only route to a committed baseline and a thin capture
pasted into one narrows the gate silently. An absent `capture` block (an
artifact from before this existed) counts as not complete: completeness has to
be proven, not assumed.

Per-run tuning:

- `--skip-regression` disables the gate (local exploration only).
- `REGRESSION_WINDOW` env var overrides the default Prometheus `rate()`
  window (`3m`). Keep close to the workload duration.
- Metric surface lives in `regression-metrics.json`; thresholds in
  `regression-thresholds.json`; both are reviewed changes. Each gated key's
  absolute bound is `hi_next - baseline` — the distance from its baseline to the
  top of the next bucket up — so refreshing the baseline obliges you to
  re-derive the bounds. See `_absolute_bound_derivation` in that file;
  `.github/scripts/telemetry/check_regression_bounds.py` enforces it in CI.
- That bound budgets for **quantization** noise only, so a key whose run-to-run
  variance is larger than it cannot be gated at all. **Five keys are excluded**
  for that reason: `span.ledger.validate.p95` and `.p99`, plus
  `span.tx.apply.p50`, `span.ledger.build.p50` and
  `span.consensus.ledger_close.p50` as of the 2026-08-26 refresh. Each carries
  its measurements in `excluded_keys` in `regression-metrics.json`. Check a key's
  observed maximum across runs against `baseline + bound` before gating it;
  widening the bound is not the fix, and neither is re-baselining until a run
  lands favourably. See `baselines/README.md`.
- A refresh moves sensitivity in **both** directions, because the trip point is
  derived from the baseline, and a single run carries no information about
  spread. The 2026-08-26 refresh loosened `job.acceptLedger.running.p95` from a
  5.74x detection floor to 16.28x (it does not fire, so it stays gated) and cut
  the three `p50` keys above from a bound that had absorbed their spread to one
  that could not — `span.tx.apply.p50` read 0.7917 ms in the previous baseline
  and 0.00597 ms in this one, a 132x move on the same workload, taking its bound
  from 4.21 ms to 0.0440 ms. Gating those keys again needs a **multi-run
  baseline** (or a spread measurement captured beside it), not a new threshold.
  All of it is measured in `baselines/README.md`; re-check after every refresh.

See [`baselines/README.md`](./baselines/README.md) for the baseline
lifecycle and refresh process.

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

Each report row is `PASS`, `FAIL`, or `INCONCLUSIVE`. The throughput and
consensus rows are ratios of the baseline, so they have nothing to report when
the baseline run measured zero — that row becomes `INCONCLUSIVE` and **counts
as a failure**, because an undefined result must never read as a pass.

Exit codes:

| Code | Meaning                                                                                                                     |
| ---- | --------------------------------------------------------------------------------------------------------------------------- |
| 0    | Every metric was measured and is within its threshold                                                                       |
| 1    | Every metric was measured and at least one exceeded its threshold                                                           |
| 2    | The overhead could not be measured — missing prerequisite, cluster never reached consensus, or incomplete metric collection |

`run-full-validation.sh` keeps the last two apart: 1 folds into its own
"checks failed" exit, 2 into its "infrastructure error" exit. A run that
measured nothing is therefore never reported as a performance regression.

### collect_system_metrics.sh

Samples CPU, peak RSS, RPC p99 latency, TPS and the mean inter-ledger interval
from the running nodes, and writes them as JSON. `benchmark.sh` calls it once
per leg; it is rarely run by hand.

```bash
./collect_system_metrics.sh 5020,5021,5022 300 /tmp/metrics.json
```

Processes are selected by matching `argv[0]`'s basename against the daemon
binary name; the pre-rename spelling is accepted too, so the sampler still
works against an older deployment. A wrapper that merely names the binary in
its arguments, and unrelated tools whose command line happens to contain the
string, are not sampled — including them diluted the CPU average and
attributed a foreign process's RSS to the node. `ps -C xrpld` is not usable
for this: xrpld renames itself, so its `comm` is `xrpld-main`.

Selection covers the whole host, so a second xrpld from another checkout is
sampled as well. Benchmark on a machine running one cluster only.

The output carries a `metrics_complete` flag. It is `false` when any
measurement source came back empty — no matching process, no successful RPC
probe, or a ledger sequence that never advanced — and the affected metrics are
then `0` placeholders. Since `0` clears every threshold, a `false` flag must be
read as inconclusive, never as a pass.

Exit codes:

| Code | Meaning                                                                                                   |
| ---- | --------------------------------------------------------------------------------------------------------- |
| 0    | Every metric was measured; `metrics_complete` is `true`                                                   |
| 1    | Cannot run: bad arguments, no GNU `date` with `%N`, or a failed process sample. No output file is written |
| 3    | The output file was written, but `metrics_complete` is `false`                                            |

`benchmark.sh` treats either non-zero code — and an explicit
`"metrics_complete": false` in an otherwise successful run — as fatal, and
exits 2 rather than comparing an incomplete run.

A nanosecond clock is required. RPC latency is graded against a 2 ms
threshold, and GNU `date +%s%N` is the only source cheap enough that the clock
does not dominate what it measures, so the script refuses to start without it.

## Reading Validation Reports

The validation report (`validation-report.json`) is structured as follows. The
counts below are illustrative — the real total is the sum of the span, metric,
log, dashboard and parity checks for the run.

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
      "name": "span.rpc.ws_message",
      "category": "span",
      "passed": true,
      "message": "rpc.ws_message: 15 traces found",
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
- **parity**: Span attributes required by the external-parity dashboard panels (validator-health, peer-quality, and friends)

## CI Integration

The validation runs as a GitHub Actions workflow (`.github/workflows/telemetry-validation.yml`):

- Triggered manually (`workflow_dispatch`) or on pushes to telemetry branches. There is no cron schedule.
- Builds xrpld, starts the full stack, runs load, validates
- Uploads reports as artifacts (and node logs when validation did not succeed)
- Writes the validation summary and the regression-gate summary to the workflow **Step Summary** (`$GITHUB_STEP_SUMMARY`). It does **not** comment on the PR — the workflow declares no `permissions:` block and calls no GitHub API, so read the summary on the run page.

Of the five `workflow_dispatch` inputs, only `run_benchmark` changes behaviour.
`rpc_rate`, `rpc_duration`, `tx_tps` and `tx_duration` are forwarded to
`run-full-validation.sh`, which parses them into shell variables and never reads
them again — load shape comes entirely from `--profile` and
`workload-profiles.json`. Their `description:` fields say so.

### Log-trace correlation in CI

The workflow no longer passes `--skip-loki`, so `log.trace_id_present` and
`log.trace_id_cross_reference` are constructed and gated on every CI run. A green
`Telemetry Validation` is now evidence that log lines carry trace context and
that a logged trace id resolves to an exported trace. `integration-test.sh` has
its own `check_log_correlation()`, but no workflow runs that script.

Correlation depends on four independent legs, and a failed check on its own names
none of them: the node must write a `debug.log` line carrying trace ids, the
collector container must see that file, its `filelog` receiver must parse and
export the line, and Loki must return it for the validator's LogQL.
`run-full-validation.sh` prints a per-leg diagnostic after the suite whenever the
Loki checks are enabled — per-node correlated-line counts and severity mix, the
container-side view of `/var/log/xrpld`, the receiver's watched files and
internal log-record counters, and Loki's own entry counts for the selector with
and without the line filter. Read that block first; it identifies the broken leg
without reproducing anything.

Those two entry counts **must** be wrapped in `sum()`. The `filelog` receiver's
`regex_parser` leaves `message` and `timestamp` as log-record attributes, and
Loki's OTLP path stores them as structured metadata that joins the label set of a
metric query — so an unaggregated `count_over_time` returns one series per log
line and Loki rejects it with `HTTP 400 maximum number of series (500) reached`
past a few hundred lines. That is not hypothetical: it made both legs print
`unavailable` on runs `32877465763` and `32964262700`, at which point the block
distinguished nothing. `_loki_json` in `validate_telemetry.py` and
`diag_loki_count` in `run-full-validation.sh` now print the HTTP status and
Loki's own plain-text body, so a future rejection names its own cause instead of
surfacing as a mimetype error.

`log.trace_id_cross_reference` polls Tempo for up to `METRIC_POLL_TIMEOUT_SEC`
(45 s, the same window and interval every other poll in the file uses) before
reporting that a logged trace id does not resolve. A trace id reaches a log line
when its span is created but is queryable only after export, ingest and indexing,
so a single query races that pipeline. A failure now means the id was absent for
the whole window.

The check distinguishes three outcomes, not two, because "Tempo never answered"
and "the spans were not exported" send a reader to different subsystems:

| outcome                           | Tempo said                               | reported as                       |
| --------------------------------- | ---------------------------------------- | --------------------------------- |
| resolved                          | 200 with spans                           | pass                              |
| absent for the whole window       | 404, or 200 with no spans, every attempt | "…do not resolve; not exported"   |
| query failed and nothing resolved | any other non-200 (4xx/5xx)              | "could not verify … last error …" |

`_tempo_get_trace` treats **404 as absence** and returns an empty list, because a
trace id read from a log line is legitimately not yet indexed and every caller
loops over candidates relying on that. Any **other** non-200 raises
`TempoQueryError` — previously an error body was fed straight to `resp.json()`,
so a JSON 5xx read as "0 spans" and a `text/plain` 5xx surfaced as a mimetype
complaint. `_tempo_search` has no absence status at all (an empty match is 200
with an empty list), so there every non-200 raises.

The same block prints locally:

```bash
docker/telemetry/workload/run-full-validation.sh --xrpld .build/xrpld
```

Re-run it after any change to log formatting, span activation, the collector's
`filelog` receiver, or the Loki exporter.

### Pathfinding is not exercised

`rpc_load_generator.py` stopped issuing `ripple_path_find` on 2026-08-25 — the weight, the request-builder branch and the docstring line went together.

**Why.** Pathfinding is disabled on every node this harness starts, so those calls could only ever fail:

- `src/xrpld/core/detail/Config.cpp:725-726` sets `pathSearchMax = 0` whenever a `[validation_seed]` or `[validator_token]` section is present — "by default, validators don't have pathfinding enabled".
- `run-full-validation.sh:308` writes `[validation_seed]` into every generated node cfg, and that script carries no `[path_search]`, `[path_search_fast]` or `[path_search_max]` section to put the default back.
- `src/xrpld/rpc/handlers/orderbook/RipplePathFind.cpp:48-49` therefore returns `rpcNOT_SUPPORTED`; `PathFind.cpp:39` does the same for `path_find`.

**What removing it fixes.** The refusals were not silent. `pathfind.request` is opened at `RipplePathFind.cpp:35`, **above** that guard, so every refused call still exported a span, and the enclosing `rpc.command.ripple_path_find` span carried `rpc_status=error`. At a 3% weight that manufactured a steady ~3% error floor in `span_calls_total{status_code="STATUS_CODE_ERROR"}`. **Any error-rate threshold derived from harness data before this change was measuring the harness, not xrpld** — re-derive it.

**What it costs.** Pathfinding now has no coverage here at all. Four spans (`pathfind.request`, `.compute`, `.discover`, `.update_all`) and two histograms (`pathfind_fast_milliseconds`, `pathfind_full_milliseconds`) go unexercised, and `pathfind.request` moved from required to `"optional": true` in `expected_spans.json` for that reason. Until the load returns, verify pathfinding by hand: the **PathFind** row of [`../TESTING.md`](../TESTING.md) carries a `curl` recipe, and `../xrpld-telemetry.cfg` is a non-validator config that already enables pathfinding.

**Putting it back.** All four steps are required. The first two alone just restore the error floor:

1. Add a `[path_search_max]` section to the node cfg `run-full-validation.sh` generates — or drop `[validation_seed]` and run a non-validator node. The `[path_search*]` block in `../xrpld-telemetry.cfg` is a working example.
2. Restore the `ripple_path_find` weight in `DEFAULT_WEIGHTS` and its branch in `build_rpc_request()`. `path_find` is a streaming subscription and needs its own phase instead — the generator is strictly one request, one reply.
3. Set `pathfind.request` back to required in `expected_spans.json`. Step 1 also makes `pathfind.compute` reachable, so the `pathfind.request -> pathfind.compute` relationship can lose its `"skip": true`.
4. **Re-capture `baselines/baseline-timings.json`.** Restoring the load changes the RPC mix, and `span.rpc.ws_message.{p50,p95,p99}` is a gated key — a baseline captured under a different mix is stale. See [OTel Timings Regression Gate](#otel-timings-regression-gate).

## Configuration Files

| File                              | Purpose                                                                                                                       |
| --------------------------------- | ----------------------------------------------------------------------------------------------------------------------------- |
| `workload-profiles.json`          | Named load profiles with phase definitions                                                                                    |
| `expected_spans.json`             | Span inventory (names, attributes, hierarchies, config flags)                                                                 |
| `expected_metrics.json`           | Metric inventory — every listed metric must be present — plus the `grafana_dashboards.uids` list the dashboard check iterates |
| `test_accounts.json`              | Test account roles (keys generated at runtime)                                                                                |
| `regression-metrics.json`         | Metric surface for the OTel regression gate                                                                                   |
| `regression-thresholds.json`      | Per-metric regression bounds (pct AND abs)                                                                                    |
| `baselines/baseline-timings.json` | Committed baseline — populated from first CI run                                                                              |
| `requirements.txt`                | Python dependencies                                                                                                           |

### expected_metrics.json Format

```json
{
  "description": "Top-level doc string — skipped by the validator.",
  "category_name": {
    "description": "Human-readable description.",
    "metrics": ["metric_1", "metric_2"],
    "required_labels": ["label_1"]
  },
  "grafana_dashboards": {
    "uids": ["rpc-performance", "node-health"]
  },
  "not_asserted": {
    "description": "Why these are excluded.",
    "metrics_excluded": { "metric_3": "reason" }
  },
  "accounted_patterns": [
    {
      "pattern": "^family_[a-z]+_(a|b)$",
      "family": "Short label.",
      "reason": "Why."
    }
  ]
}
```

Every metric listed under a `metrics` array must produce > 0 Prometheus series during the validation run. If a metric doesn't fire, the workload generators need to produce enough load to trigger it.

`required_labels` is optional and read for every category that declares one. Each label becomes one additional check, named `metric.<category>.label.<label>`, that at least one of that category's series carries the label with a non-empty value. It is matched as `<label>!=""` rather than `<label>=~".*"` because Prometheus cannot tell an absent label from an empty one, so a regex match would pass on a node that lost the label entirely. The guarantee rule is the same as for `metrics`: list a label only where the workload guarantees it. Declaring `required_labels` on a category with no `metrics` fails the check rather than skipping it, so the key can never sit unenforced.

Four top-level keys are not metric categories:

- `description` is a string, and `accounted_patterns` is a list, so
  `_metric_check_targets` skips both structurally — it walks only top-level
  values that are objects.
- `grafana_dashboards` is an object but declares no `metrics`, so it contributes
  no checks. `grafana_dashboards.uids` drives the dashboard check, so adding a
  dashboard to `docker/telemetry/grafana/dashboards/` does **not** put it under
  the gate until its uid is added here too.
- `not_asserted` is skipped the same way: the loop reads
  `category_data.get("metrics", [])`, and this group deliberately has no
  `metrics` key — its entries live under `metrics_excluded` as a name-to-reason
  map. It documents metrics that are emitted and dashboarded but left unasserted
  because they are workload-gated or defect-gated (a check that fails on a
  healthy run is worse than no check). Promote an entry into an asserted group
  only after the workload is changed to guarantee it fires.
- `accounted_patterns` asserts nothing. It is a list of `{pattern, family,
reason}` entries feeding the reverse coverage check described below.

### Reverse Coverage — emitted but not accounted for

The checks above run in one direction only: they read the contract and ask the
backend whether each listed name exists. That direction is blind to a name the
contract omits, which is how a large metric gap and several unknown spans went
unnoticed — both emitted inventories were already being fetched for the CI log,
and neither was compared back.

Two extra checks close the loop, `metric.reverse_coverage` and
`span.reverse_coverage`. Each lists every name the backend reports that the
contract never mentions, sorted, one per line in the log, with counts in the
report's `details`.

**They warn and never fail.** `passed` is hardcoded `True` in
`_reverse_coverage_result`, so an unaccounted name cannot turn CI red. That is
deliberate: downstream branches legitimately add telemetry an upstream contract
has not seen yet, and a hard failure would redden every one of them for doing
the right thing. The value is visibility, not enforcement.

A metric family counts as accounted for when any of these is true:

- a group's `metrics` array lists it (with any label matcher stripped),
- a `not_asserted.metrics_excluded` key names it — deliberately unasserted is
  not the same as unknown,
- an `accounted_patterns` regex matches it (anchored, `fullmatch`).

Exporter shapes are folded before matching, so a contract entry written for a
family covers what the exporter derives from it: a histogram's
`_bucket`/`_count`/`_sum` names fold back onto their base family, and counters
are listed with the `_total` the exporter appends. Spans need no pattern list —
`expected_spans.json` already carries globs such as `rpc.command.*`, and the
reverse check reuses the same matcher the forward check uses.

Use `accounted_patterns` only for a family whose membership is derived
mechanically from a table in the code and so cannot be enumerated by hand — the
per-job-type job-queue instruments and the overlay per-category traffic cross
product are the two real cases, plus the Prometheus scrape plumbing that is not
xrpld telemetry at all. Keep each pattern anchored and no wider than its family
needs: a pattern that swallows unrelated names defeats the check.

### expected_spans.json Format

Each span entry defines its name, category, parent (for hierarchy validation),
required attributes, and the `config_flag` that must be enabled. A trailing `*`
in `name` is a wildcard. The optional `"optional": true` field marks a span whose
absence is a skip rather than a failure:

```json
{
  "name": "rpc.command.*",
  "category": "rpc",
  "parent": "rpc.process",
  "required_attributes": ["command", "version", "rpc_role", "rpc_status"],
  "config_flag": "trace_rpc"
}
```

## Node Configuration Notes

The orchestrator (`run-full-validation.sh`) generates node configs with:

- `[telemetry] enabled=1` with all five trace categories: `trace_rpc`, `trace_transactions`, `trace_consensus`, `trace_peer`, `trace_ledger`
- `[insight] server=otel` with `endpoint=http://localhost:4318/v1/metrics` — `beast::insight` metrics reach Prometheus over OTLP, because the collector declares no `statsd` receiver. No `prefix` is set: it would be inert, since `OTelCollector` applies no prefix to instrument names and exported names are the lowercased raw names (`jobq_job_count`, not `xrpld_jobq_job_count`)
- `[signing_support] true` — required for `tx_submitter.py` to submit signed transactions via WebSocket
- `[ips]` (not `[ips_fixed]`) — ensures peer connections are counted in the PeerFinder active-peer gauges, exported as `peer_finder_active_inbound_peers` / `peer_finder_active_outbound_peers` (fixed peers are excluded from these counters by design). The `beast::insight` group/name pair is `Peer_Finder` / `Active_Inbound_Peers`; `formatName()` lowercases it for export.

## Gauge Export Behaviour

The harness configures each node with `[insight] server=otel` (see the
`[insight]` block generated by `run-full-validation.sh`), so `beast::insight`
gauges go through `OTelGaugeImpl` in
`src/libxrpl/beast/insight/OTelCollector.cpp`, not through the StatsD collector.
That matters for how the validator queries Prometheus.

**How `OTelGaugeImpl` exports.** It wraps an OTel **observable** (asynchronous)
gauge. `set()` and `increment()` only store into an `std::atomic<int64_t>`;
nothing is exported at call time. The SDK's collection thread invokes
`gaugeCallback`, which runs the collector's hooks and then `Observe()`s whatever
the atomic currently holds. So the gauge reports **every collection cycle,
whether or not the value changed** — including a gauge that sits at 0 from
startup. There is no dirty flag on this path, and no first-flush special case is
needed.

**Why the validator still uses `/api/v1/series`.** Two reasons survive the move
to OTLP:

1. **Late-populating series.** A gauge or counter may not have completed the
   export → collector → Prometheus-scrape pipeline by the time validation runs.
   `_check_prometheus_metric` in `validate_telemetry.py` therefore polls
   `/api/v1/series` (which returns anything that existed anywhere in the query
   window) until the metric appears or the poll window elapses, instead of
   racing a single instant query.
2. **Staleness robustness.** `/api/v1/series` does not care whether the newest
   sample is inside Prometheus's ~5-minute staleness horizon, so the check
   cannot be defeated by a quiet series.

> **Note — the StatsD path is still in the tree but unused here.** If a node is
> configured with `server=statsd`, `StatsDGaugeImpl` (in
> `src/libxrpl/beast/insight/StatsDCollector.cpp`) does gate emission on a
> `dirty_` flag that is only set by `set()`/`increment()`, and it is
> initialised to `true` so the initial value is emitted on the first flush. The
> collector configs shipped in `docker/telemetry/` declare no `statsd` receiver
> (the metrics pipeline is `[otlp, spanmetrics]`) and the base
> `docker-compose.yml` keeps its StatsD UDP port commented out, so nothing in
> this harness can receive StatsD.
