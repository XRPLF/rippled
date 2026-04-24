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
  and exits non-zero on any regression.

The regression gate runs against whatever workload profile `run-full-validation.sh`
was invoked with. Capture and comparison are profile-agnostic — they only read
Prometheus — so all existing profiles (`full-validation`, `quick-smoke`, `stress`)
continue to work unchanged.

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
    "rpc.server_info.p95": { "value": 850.0, "unit": "us" },
    "job.transaction.queued.p95": { "value": 1500.0, "unit": "us" }
  }
}
```

Missing metrics (value `null`) in a captured run do not count as regressions — they
are reported separately in `regression-report.json` under `missing_in_current`.
This keeps the gate robust when a profile doesn't exercise every span on every run.
