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

## Current state: 20 metrics gate, on a baseline captured 2026-08-26

`baseline-timings.json` holds real captured values for the 20 keys the harness gates, from CI run
`32964262700` at `8418d474a7`, profile `full-validation`, window `3m`. It replaced a capture taken
at `6a82fc6f37` that predated two workload changes — the removal of the refused path-finding RPC
load (`59a0595a6e`) and everything after it — so its numbers described a workload the harness no
longer runs. The entries before that, captured on 2026-06-05, were voided into a placeholder: they
predated the
spanmetrics ladder re-cut of 2026-08-04 (`3860c93db2`), which made every sub-millisecond quantile
in that capture bucket-edge arithmetic rather than a latency (a p95 of `0.95` ms is `0.95 × 1 ms`).
Because the comparator only flags a metric when the current value _exceeds_ the baseline, a
stale-high baseline passes everything silently, so the entries had to be dropped rather than left
in place. They stay retrievable from this file's git history.

**A placeholder must not outlive one run.** CI stays green the whole time one stands, so an
un-copied block is not a failure anyone will notice — it is a silent loss of regression coverage
that looks identical to a passing gate. Voiding a baseline is the one hand edit this file allows;
_setting_ one always comes from a printed CI block, per the "Refreshing the baseline" rule below.

## Absolute bounds are derived per metric, from the ladder

`../regression-thresholds.json` gives every gated key its own `max_abs_increase_*`, equal to
**`hi_next − baseline`**: locate the baseline in the half-open bucket `(lo, hi]` of its ladder,
take `hi_next` as the next edge above `hi`, and the bound is the distance from the baseline to
`hi_next`. The trip point is therefore exactly `hi_next` — the gate fires only once the reading
clears the bucket **above** the baseline's own.

That is what buys the guarantee. `histogram_quantile` returns a value interpolated inside
whichever bucket the true quantile falls in, so any reading taken while the quantile is still in
the baseline's bucket, or anywhere in the one immediately above, is at most `hi_next` and cannot
fire. Firing needs the quantile to have moved at least two buckets up. A multiple of the
_enclosing_ bucket's width cannot deliver this, because once the quantile crosses `hi` the
interpolation happens across the **next** bucket, which on this ladder is up to 8x wider —
`(0.5, 1]` is 0.5 ms wide and `(1, 5]` is 4 ms wide. The full derivation, both ladders, and a
per-key table of the arithmetic are in that file's `_absolute_bound_derivation` and
`_derivation_table`.

Two earlier generations of this bound were wrong, in opposite directions:

| generation                     | bound                                        | 10x regression caught | single-crossing false positive reachable |
| ------------------------------ | -------------------------------------------- | --------------------- | ---------------------------------------- |
| flat                           | 10 ms `p50`/`p95`, 15 ms `p99`, 20000 us job | 5 / 28 keys           | 2 / 25 keys                              |
| 2 × enclosing bucket width     | per metric                                   | 28 / 28 keys          | **21 / 25 keys**                         |
| `hi_next − baseline` (current) | per metric                                   | 19 / 20 keys          | **0 / 20 keys**                          |

The first two rows were measured when 28 and 25 keys were gated; the current row was re-measured
over today's 20 by injecting a 10x regression into each gated key in turn against a real CI
`timings.json`. The gate flagged 19. The exception is `job.acceptLedger.running.p95`, whose
detection floor is 16.28x: 10x reaches 61429 us against a 100000 us trip point, and the gate first
fires at 16.28x (measured — 16.2x passes, 16.28x fails). At **20x the sweep catches 20 of 20**. That
is the ladder, not the rule; the key is listed under
[weakly guarded](#which-keys-are-only-weakly-guarded) below. On the 2026-08-24 baseline the same key
had a 5.74x floor and 10x did catch it, which is what a baseline refresh can silently do to
sensitivity. The zero in the last column is by
construction rather than by sampling: rule C in
[`check_regression_bounds.py`](../../../../.github/scripts/telemetry/check_regression_bounds.py)
fails the build unless every trip point is exactly `hi_next`, and a trip point at a bucket edge
cannot be crossed by interpolation inside that bucket.

The flat bound was calibrated for a 5-25 ms band the spans do not occupy: 18 of the 28
quantiles gated at the time sat below 1 ms, so it sat 1.15x to 2000x above the metric it guarded,
and because the rule is an `AND` the percentage bound could never carry a regression alone. A 100x
regression injected into `span.ledger.store.p95` reported **0 regressions, exit 0**. The second
generation fixed the magnitude but kept an assumption that does not hold — that the reading's
excursion is bounded by the enclosing bucket's width — which put 21 of 25 trip points inside the
adjacent bucket, so a single legitimate bucket crossing could turn CI red.

**Refreshing the baseline means re-deriving the bounds**, because a refreshed value can land in a
different bucket and so get a different `hi_next`. This is no longer a documentation-only rule:
[`.github/scripts/telemetry/check_regression_bounds.py`](../../../../.github/scripts/telemetry/check_regression_bounds.py)
fails CI when a bound is not the one its own baseline implies, when a gated key has no override,
when a baseline key is not declared by `../regression-metrics.json` (or the reverse), when the
percentage bound would become the operative one, and when a baseline carries the ladder-floor
signature described below.

### Which keys are only weakly guarded

The guarantee costs sensitivity where the ladder is coarse: the detection floor is
`hi_next / baseline`, so a baseline sitting just above an edge is guarded loosely. Measured over
the current baseline the floor ranges 2.21x to 16.28x. Do **not** read these as guarded:

| key                               | baseline  | fires at  | floor  | limiting ladder step |
| --------------------------------- | --------- | --------- | ------ | -------------------- |
| `job.acceptLedger.running.p95`    | 6142.9 us | 100000 us | 16.28x | 25000 us → 100000 us |
| `span.consensus.accept.p50`       | 0.5287 ms | 5 ms      | 9.46x  | 1 ms → 5 ms          |
| `job.transaction.running.p95`     | 600.0 us  | 5000 us   | 8.33x  | 1000 us → 5000 us    |
| `span.tx.process.p95`             | 0.6100 ms | 5 ms      | 8.20x  | 1 ms → 5 ms          |
| `span.rpc.ws_message.p95`         | 0.6977 ms | 5 ms      | 7.17x  | 1 ms → 5 ms          |
| `span.consensus.ledger_close.p95` | 0.7830 ms | 5 ms      | 6.39x  | 1 ms → 5 ms          |
| `span.rpc.ws_message.p99`         | 0.9757 ms | 5 ms      | 5.12x  | 1 ms → 5 ms          |

`job.acceptLedger.running.p95` is the one that matters most, because it is the only gated key a
10x regression does not catch (see the generation table above). Its floor moved there **in this
refresh**, from 5.74x: the baseline fell from 17428.6 us to 6142.9 us while `hi_next` stayed at
100000 us. It does **not** fire on any observed run — its worst reading is 0.16 of its trip point —
so it stays gated, and the weak floor is recorded here so it is visible rather than surprising. The
fix is a 2 ms edge (ideally 3 ms as well) in the collector's spanmetrics `buckets` list plus the
matching entries in `kMillisecondBuckets`, and 2000 us plus 50000 us edges in `kMicrosecondBuckets`.
That work belongs to the branch that owns the ladders.

`span.tx.apply.p50` is absent from this table because it is **no longer gated at all** — see
[what all five excluded keys have in common](#what-all-five-excluded-keys-have-in-common). Beyond
its variance it had a second, independent problem: its baseline of `0.00597` ms sat inside the
ladder's **first** bucket `(0, 0.01]`, so the reported figure was interpolation across that bucket,
tracking the _fraction_ of applies finishing under 10 us rather than a latency — the same mechanism
that disqualified `ledger.store` below. Rule E did not flag it, correctly: the value is not
`quantile × first_edge` exactly, so some mass does sit above 0.01 ms. Restoring the key therefore
needs a finer low-end ladder **as well as** a spread-aware baseline.

## Known exclusion: `ledger.store` is below the ladder's resolution

`span.ledger.store` is **not** gated. The 2026-08-24 capture returned p50/p95/p99 of exactly
`0.005` / `0.0095` / `0.0099` ms, which is `0.5` / `0.95` / `0.99 × 0.01` ms — the ladder's first
edge times the quantile, the signature of every sample landing in the first bucket. Those numbers
are interpolation arithmetic on the bucket floor, not latencies. It is physically plausible:
[`LedgerMaster.cpp:463`](../../../../src/xrpld/app/ledger/detail/LedgerMaster.cpp#L463) wraps an
in-memory `ledgerHistory_.insert`, which completes in single-digit microseconds.

While all the mass stays under 10 us the reported quantile cannot move materially, so **no
absolute bound can gate this key** — every `ledger.store` slowing from 2 us to 9 us, a 4.5x
regression, leaves the reported value unchanged. Three keys that read as covered but cannot fire
are worse than no keys, the same argument that excluded `rpc.process`, so they were removed from
`../regression-metrics.json` rather than left in with a bound that looks derived.

Restoring the key needs sub-10 us edges on the collector's spanmetrics ladder (for example
`0.001ms` and `0.005ms`) plus the matching entries in `HistogramBuckets.h`. `ledger.store`
presence is still asserted by `../expected_spans.json` and `docker/telemetry/integration-test.sh`,
and its rate is still on the ledger-operations dashboard; only the latency gate drops it.
`check_regression_bounds.py` rule E fails the build if a key with this signature is gated again.

## Known exclusion: `ledger.validate` p95 and p99 vary more than any bound can absorb

`span.ledger.validate.p95` and `.p99` are **not** gated. `p50` still is. They are the first
exclusion at _quantile_ rather than _span_ granularity, which is why
[`../regression-metrics.json`](../regression-metrics.json) grew an `excluded_keys` map — `spans.names`
lists span names and `_quantiles` is shared across all of them, so removing two quantiles of one
span cannot be expressed by deleting a name.

Measured across four CI runs:

| key                               | baseline  | trip point | observed min | observed max | spread |
| --------------------------------- | --------- | ---------- | ------------ | ------------ | ------ |
| `span.ledger.validate.p50` (kept) | 0.0647 ms | 0.25 ms    | 0.0484 ms    | 0.0778 ms    | 1.6x   |
| `span.ledger.validate.p95`        | 0.2404 ms | 0.5 ms     | 0.1281 ms    | 0.7500 ms    | 5.9x   |
| `span.ledger.validate.p99`        | 1.0600 ms | 10 ms      | 0.3875 ms    | 25.8750 ms   | 66.8x  |

Both excluded quantiles reach past their trip point on an ordinary run, so CI reddened twice with
no code change: run `32867433073` read `p95` = 0.7500 ms (+212%) and run `32862589645` read
`p99` = 25.8750 ms (+2341%). The two failures landed on **different** quantiles in different runs
while the other quantile stayed well inside its bound in the same run — the signature of variance,
not of a regression.

The mechanism is arrival timing, not slow code. The span opens only once a quorum-completing
validation arrives ([`LedgerMaster.cpp:987`](../../../../src/xrpld/app/ledger/detail/LedgerMaster.cpp#L987),
inside `checkAccept`, past the `tvc < minVal` early return) and wraps the promotion work that
follows — `setValidated`, `setFull`, `setValidLedger`, `pendSaveValidated`. Its duration therefore
tracks when peer validations arrive in a 5-node cluster and what promotion then schedules, so a
single slow consensus round dominates the tail of a 3 m rate window, and which round that is
differs every run.

**Widening the bound is not an option and must not be attempted.** Tolerating 25.8750 ms against a
1.0600 ms baseline needs a bound of ~24.8 ms, i.e. a gate that fires at nothing a regression could
plausibly reach. A bound that admits every healthy run's worst case admits every regression too.
`check_regression_bounds.py` rule F fails the build if either key is re-gated with a bound while
still listed in `excluded_keys`, and the per-key reasons in that map record this in full.

### The general rule this exposed

`hi_next − baseline` is derived from the **ladder**, so it budgets for **quantization** noise — one
bucket of interpolation headroom — and for nothing else. It knows nothing about how far the metric
itself moves between runs on identical code. Where run-to-run workload variance is the larger term,
the bound is simply the wrong size and the gate reddens on a healthy run.

**Before gating any key, check its observed maximum across several runs against its trip point
(`baseline + bound`), and gate it only if the maximum stays below that with margin.** Spread alone
proves nothing; it is spread **relative to the trip point** that decides. And because the trip
point is derived from the baseline, a baseline that lands at the **low end** of a metric's own
range shrinks that trip point without anything about the metric having changed.

That is what the 2026-08-26 refresh did to three `p50` keys, and **all three are now excluded** —
this rule being applied, not a new exception. Measured across the three CI runs `32862589645`,
`32867433073` and `32964262700` (the last of which is this baseline):

| key                               | bound     | trip point | observed max | max ÷ trip | spread |
| --------------------------------- | --------- | ---------- | ------------ | ---------- | ------ |
| `span.tx.apply.p50`               | 0.0440 ms | 0.05 ms    | 2.3378 ms    | **46.76x** | 391.8x |
| `span.ledger.build.p50`           | 0.3849 ms | 0.5 ms     | 2.3826 ms    | **4.77x**  | 20.7x  |
| `span.consensus.ledger_close.p50` | 0.0613 ms | 0.1 ms     | 0.2377 ms    | **2.38x**  | 6.1x   |

Before the exclusion, replaying **either** older run against this baseline reported exactly those
three and nothing else — and run `32867433073` carries the same post-path-finding-removal workload
as the baseline itself, so the movement was metric variance, not a workload difference. Those two
runs are what would have reddened CI. After the exclusion both replay clean.

The evidence that settles it is `span.tx.apply.p50`'s own history. It read **0.7917 ms** in the
previous baseline and **0.00597 ms** in this one — a 132x difference between two runs of the same
workload. At the old value the identical `hi_next − baseline` rule produced a 4.21 ms bound whose
5 ms trip point absorbed the entire range; at the new value it produces 0.0440 ms and cannot.
Nothing about the metric changed. **Whether the gate functioned was decided by where in its own
distribution the captured run happened to land** — which is not a threshold that needs tuning, it
is a key that cannot be gated from a single-run baseline at all.

So the remedy is the `excluded_keys` entry with the measurement behind it, exactly as
`ledger.validate` p95 and p99 got — **not** a widened bound, and **not** re-baselining until a run
lands favourably. A key that fails this test is never fixed by widening its bound. The remaining
20 gated keys sit at or below 0.58 of their trip points, the worst being `span.consensus.accept.p50`.

### What all five excluded keys have in common

| key                               | trip point | observed max | mechanism                             |
| --------------------------------- | ---------- | ------------ | ------------------------------------- |
| `span.tx.apply.p50`               | 0.05 ms    | 2.3378 ms    | baseline in the ladder's first bucket |
| `span.consensus.ledger_close.p50` | 0.1 ms     | 0.2377 ms    | baseline in a low bucket              |
| `span.ledger.build.p50`           | 0.5 ms     | 2.3826 ms    | baseline in a low bucket              |
| `span.ledger.validate.p95`        | 0.5 ms     | 0.7500 ms    | baseline in a low bucket              |
| `span.ledger.validate.p99`        | 10 ms      | 25.8750 ms   | spread too large for any bound        |

One invariant covers all five: **the observed maximum exceeds `baseline + bound`**, so an ordinary
run clears the trip point with nothing having regressed. Two mechanisms produce it. Four of the five
have a baseline sitting low in the ladder, where the derived bound is tiny because the bound _is_
the distance to the next edge up. The fifth, `ledger.validate.p99`, has a comparatively generous
8.94 ms bound and still fails, because a 66.8x spread reaches 25.875 ms against a 10 ms trip point.

**The follow-up that would restore coverage**, stated rather than left implied: a baseline captured
from a **single run** cannot support these keys, because one sample carries no information about
spread and the bound is derived from that one sample alone. What would let them be gated again is a
**multi-run baseline** — or a spread measurement captured alongside the baseline — so a bound can be
sized against observed variance instead of against the ladder only. That is not implemented; it is
the design change these five exclusions are waiting on.

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

Refreshing the baseline also obliges you to re-derive the absolute bounds in
`../regression-thresholds.json`, per
[Absolute bounds are derived per metric](#absolute-bounds-are-derived-per-metric-from-the-ladder).
A value that moves into a different bucket needs a different bound, and a bound left behind
either stops catching regressions or starts firing on quantization noise.

It also obliges you to re-check each key's run-to-run spread against its new trip point, per
[The general rule this exposed](#the-general-rule-this-exposed). A refreshed baseline can land in a
bucket whose `hi_next` no longer clears the metric's own variance, which turns the key into a
recurring false positive — the failure that excluded `ledger.validate` p95 and p99.

## The baseline is only valid at the log level it was captured at

Every timing here is coupled to the `log_level` that `run-full-validation.sh` writes into
each node's `[rpc_startup]` stanza. Logging is **synchronous**, and several of the gated
spans contain log statements, so the configured level is part of the measurement:

- `ledger.build` contains [`BuildLedger.cpp:81`](../../../../src/xrpld/app/ledger/detail/BuildLedger.cpp#L81) (debug).
- `consensus.accept` contains [RCLConsensus.cpp:655/663/686](../../../../src/xrpld/app/consensus/RCLConsensus.cpp#L663) (debug) — `:663` logs **once per transaction** in the canonical set.
- `tx.apply` and the other `spans.names` entries in [`../regression-metrics.json`](../regression-metrics.json) are affected the same way.

Raising the level admits more of those statements and inflates the p50/p95/p99 of the very
spans the gate measures; lowering it deflates them. Neither shows up as a regression, because
the baseline moves with it — the gate simply starts measuring a different configuration.

**Changing the workload log level therefore invalidates this baseline and requires
re-capturing it.** Treat it exactly like a deliberate performance change: follow
[Refreshing the baseline](#refreshing-the-baseline), and note the level change in the PR so
the reviewer knows why the numbers moved. In particular, do not capture a baseline while the
harness is running at `debug` — see the runbook's "Why not `debug`" note; if you need
debug-level detail, enable it per partition **after** the baseline exists.

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
