# Performance Baselines

This directory holds the committed baseline file used by the OTel-driven regression gate.

## How the gate works

After the validation suite runs, `capture_timings.py` queries Prometheus for the timings
declared in [`../regression-metrics.json`](../regression-metrics.json) and writes a
`timings.json`. Then `compare_to_baseline.py` reads [`baseline-timings.json`](./baseline-timings.json),
[`../regression-thresholds.json`](../regression-thresholds.json), and the captured
`timings.json`. The comparator picks one of two modes automatically:

- **Placeholder baseline** (`"placeholder": true` or empty `metrics`): the comparator
  prints the captured timings JSON in exactly the format expected for this file, then
  exits 0 without gating. This is how we bootstrap the baseline.
- **Populated baseline**: the comparator diffs per-metric, enforces the thresholds
  (regression = current exceeds baseline on BOTH the percentage AND absolute bound),
  and exits non-zero on any regression. The single exception is a baseline that is
  not positive: the percentage change is undefined there, so the absolute bound
  decides alone. Without that fallback the AND gate would be unreachable and a
  0 ms → 500 ms jump would be reported as "within bounds".

The regression gate runs against whatever workload profile `run-full-validation.sh`
was invoked with. Capture and comparison are profile-agnostic — they only read
Prometheus — so all existing profiles (`full-validation`, `quick-smoke`, `stress`)
continue to work unchanged.

## Current state: the baseline is a placeholder

`baseline-timings.json` currently carries `"placeholder": true` and an empty `metrics` object,
so **no metric gates right now**. Its entries were captured on 2026-06-05 against a spanmetrics
ladder that was re-cut on 2026-08-04 in `3860c93db2`, which makes every sub-millisecond quantile
in that capture bucket-edge arithmetic rather than a latency (a p95 of `0.95` ms is `0.95 × 1 ms`).
Because the comparator only flags a metric when the current value _exceeds_ the baseline, a
stale-high baseline passes everything silently — so the entries were voided instead of left in
place. The file's `_note` records why, and which numbers were dropped.

To restore gating, follow [Bootstrapping the baseline](#bootstrapping-the-baseline) below — a
placeholder is exactly the state that loop expects. Pasting the CI block **replaces the whole
file**, `_note` included; that is intended, and the voided numbers stay retrievable from this
file's git history.

**Do not let the placeholder outlive one run.** CI stays green the whole time the placeholder
stands, so an un-copied block is not a failure anyone will notice — it is a silent loss of
regression coverage that looks identical to a passing gate.

Voiding a baseline is the one hand edit this file allows; _setting_ one always comes from a
printed CI block, per the "Refreshing the baseline" rule below.

## Bootstrapping the baseline

1. Merge a CI run with a `"placeholder": true` baseline. The telemetry-validation
   workflow runs, fails no gate, and prints the captured timings block to the workflow
   Step Summary under the heading `### Paste into baselines/baseline-timings.json`.
2. Open a new PR. Copy the full JSON block from the Step Summary (or download the
   `timings.json` artifact) into this file, replacing the placeholder contents. The
   JSON is emitted in the exact byte-for-byte format this file expects — sorted keys,
   2-space indent, trailing newline.
3. The committed baseline PR needs reviewer approval just like any other code change.
   This is the primary audit point for "who moved the performance bar."

## Refreshing the baseline

Refresh when a legitimate performance change lands on `develop` (for example, a
deliberate rewrite that changes a span's structure). The process is identical to
bootstrapping: run CI with the current baseline, inspect the delta, and if the
new numbers should become the norm, open a PR pasting the fresh timings into
`baseline-timings.json`. The reviewer decides whether the new baseline is acceptable.

Do **not** edit `baseline-timings.json` by hand outside of this process — every entry
should trace back to a real CI run so variance characteristics are preserved.

## Schema

```json
{
  "schema_version": 1,
  "captured_at": "2026-04-24T17:30:00Z",
  "window": "3m",
  "git_sha": "<SHA of the commit that produced these numbers>",
  "profile": "<workload profile used>",
  "metrics": {
    "span.tx.process.p99": { "value": 12.4, "unit": "ms" },
    "job.transaction.queued.p95": { "value": 1500.0, "unit": "us" }
  }
}
```

Keys follow `{category}.{name}.p{quantile}`. Only two categories are actually
produced today — `span.*` and `job.*` — because `build_query_plan()` in
`prom_queries.py` reads the `spans` and `job_queue` groups of
`regression-metrics.json`, and that file defines only those two.

Placeholder baselines additionally include `"placeholder": true`. The comparator
detects this field (or an empty `metrics` object) to switch into "populate" mode
instead of enforcing thresholds. Remove the `placeholder` key when pasting real
captured timings.

Missing metrics (value `null`) in a captured run do not count as regressions. In
`regression-report.json`, `summary.missing_in_current` is a **count** only; the
identities are in the `metrics[]` array, as the entries whose `note` is
`"not captured in current run"`. Filter for those to see which keys went missing:

```bash
jq -r '.metrics[] | select(.note == "not captured in current run") | .key' \
    /tmp/xrpld-validation/reports/regression-report.json
```

This keeps the gate robust when a profile doesn't exercise every span on every run.

## Known gap: no `rpc.*` metric can gate (FU-4)

Per-RPC-method timings are **not** gated, and would not gate even if they were
captured. Two independent blockers:

1. **Nothing emits an `rpc.*` key.** `build_query_plan()` in `prom_queries.py`
   builds `rpc.*` entries from `cfg.get("rpc_methods", {})`, and
   `regression-metrics.json` has no `rpc_methods` block — so the group resolves
   to empty and no `rpc.*` key ever reaches `timings.json` or this baseline.
2. **Even a captured `rpc.*` key would silently not gate.** `resolve_thresholds()`
   in `compare_to_baseline.py` maps the `rpc` category to the threshold group
   `rpc_method`, but `regression-thresholds.json` defines only
   `defaults.span` and `defaults.job_queue`. With no `rpc_method` block the
   lookup returns `(None, None)`, which the comparator treats as "no threshold
   configured" — the metric is reported but can never fail the build.

Closing this needs **both** an `rpc_methods` group in `regression-metrics.json`
and a `defaults.rpc_method` block in `regression-thresholds.json`. Adding only
the first produces metrics that look gated in the report but are not.

## Known exclusion: `rpc.process` is not captured

`rpc.process` is deliberately absent from the `spans.names` list in
`regression-metrics.json`, so no `span.rpc.process.*` key appears in this
baseline. The span is created only in `ServerHandler::processRequest()`
(`src/xrpld/rpc/detail/ServerHandler.cpp:705`), which is reached only from the
HTTP/JSON-RPC session path. The harness load generator is WebSocket-only and
that path never calls `processRequest`, so the span is never emitted under any
workload profile here — `expected_spans.json` marks it `"optional": true` for
the same reason.

While it was listed, the three quantiles were captured as `null` on every run
and the comparator short-circuited them as `"new metric (not in baseline)"` —
so a 9999 ms value would still have reported `regressed: false`. Three keys
that can never gate are worse than no keys: they inflate `summary.total` and
read as covered.

If per-request HTTP timings are wanted, the fix is to give the harness an
HTTP/JSON-RPC load path first, then re-add `rpc.process` and bootstrap a real
baseline for it.
