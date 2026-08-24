#!/usr/bin/env python3
"""Assert every workload-gate absolute bound is the one its own baseline implies.

The regression gate in ``docker/telemetry/workload`` fails CI when a span or
job-queue quantile grows. Whether it *can* fail is decided by
``regression-thresholds.json``, and that file's numbers are derived from
``baselines/baseline-timings.json`` plus the two histogram ladders. Nothing
tied the three together, and the gate has now been broken three times by the
same class of drift:

  1. the microsecond ladder's floor moved 100us -> 1us, voiding every
     job_queue baseline captured before it;
  2. the spanmetrics ladder's floor moved 1ms -> 0.01ms, voiding every
     sub-millisecond span baseline captured before it;
  3. the absolute bounds stayed calibrated for a 5-25ms band the spans had
     left, so a 100x regression on ``span.ledger.store.p95`` reported zero
     regressions and exit 0.

Each time the gate stayed green, which is indistinguishable from a passing
build. Documentation did not prevent recurrence, so this is a check.

The rule it enforces is the one recorded in ``regression-thresholds.json``
under ``_absolute_bound_derivation``: for a baseline sitting in the half-open
bucket ``(lo, hi]`` of its ladder, with ``hi_next`` the next edge above ``hi``,

    max_abs_increase_* == hi_next - baseline

so the gate trips only when the reading clears the bucket *above* the
baseline's own. Five rules are checked:

  A  the baseline's key set equals the surface ``regression-metrics.json``
     declares (a stale key left behind reads as covered but never gates);
  B  every gated key has a per-metric override, not a fallback default;
  C  each absolute bound equals ``hi_next - baseline``;
  D  each percentage bound stays below ``100 * bound / baseline``, so the
     absolute bound remains the operative half of the ``AND`` -- the span
     ladder's 2s/3s/4s edges are only 1.25x-1.5x apart, where this silently
     stops being true;
  E  no baseline carries the ladder-floor signature ``quantile x first_edge``,
     which means every sample landed in the first bucket and the number is
     interpolation arithmetic rather than a latency.

A PLACEHOLDER baseline -- ``"placeholder": true`` or an empty ``metrics``
object -- exits 0, because that is the documented bootstrap state and CI has to
stay green while a baseline is being recaptured. A missing, unreadable or
malformed input is a different thing and exits 1: a check that reports success
without having checked anything is the same green-build-that-is-not failure this
script exists to prevent, so renaming or deleting one of its inputs must not
silence it.

Exit 0 when every rule holds, 1 with per-key detail otherwise.
"""

import json
import re
import sys
from pathlib import Path

WORKLOAD = Path("docker/telemetry/workload")
BASELINE = WORKLOAD / "baselines/baseline-timings.json"
THRESHOLDS = WORKLOAD / "regression-thresholds.json"
METRICS = WORKLOAD / "regression-metrics.json"
COLLECTOR = Path("docker/telemetry/otel-collector-config.yaml")
HEADER = Path("include/xrpl/telemetry/HistogramBuckets.h")

UNIT_TO_MS = {"ms": 1.0, "s": 1000.0}
# A bound may differ from the derived value only by double round-tripping.
REL_TOLERANCE = 1e-12


def read_text_or_exit(path):
    """Read a required text input, or exit 1 naming the input that failed."""
    try:
        return path.read_text()
    except OSError as exc:
        sys.exit(f"{path}: required input could not be read -- {exc}")


def read_json_or_exit(path):
    """Read and parse a required JSON input, or exit 1 naming what failed."""
    try:
        return json.loads(read_text_or_exit(path))
    except json.JSONDecodeError as exc:
        sys.exit(f"{path}: required input is not valid JSON -- {exc}")


def span_edges_ms():
    """Parse the spanmetrics bucket list, normalising each edge to milliseconds."""
    match = re.search(r"buckets:\s*\[(.*?)\]", read_text_or_exit(COLLECTOR), re.S)
    if not match:
        sys.exit(f"{COLLECTOR}: no 'buckets:' list found")
    edges = []
    for raw in match.group(1).split(","):
        token = raw.strip()
        if not token:
            continue
        parsed = re.fullmatch(r"([0-9.]+)(ms|s)", token)
        if not parsed:
            sys.exit(f"{COLLECTOR}: cannot parse bucket edge {token!r}")
        edges.append(float(parsed.group(1)) * UNIT_TO_MS[parsed.group(2)])
    return edges


def microsecond_edges():
    """Parse kMicrosecondBuckets out of the header that owns every ladder."""
    match = re.search(r"kMicrosecondBuckets\{(.*?)\};", read_text_or_exit(HEADER), re.S)
    if not match:
        sys.exit(f"{HEADER}: kMicrosecondBuckets not found")
    return [
        float(token.strip().replace("'", ""))
        for token in match.group(1).split(",")
        if token.strip()
    ]


def declared_keys(metrics_cfg):
    """Rebuild the flat key set regression-metrics.json declares.

    Deliberately reimplemented rather than imported from ``prom_queries.py``,
    which pulls in aiohttp; CI telemetry checks stay dependency-free. The key
    format is fixed by that file's own ``_key_format`` field.
    """
    keys = set()
    spans = metrics_cfg.get("spans", {})
    for name in spans.get("names", []):
        for quantile in spans.get("_quantiles", []):
            keys.add(f"span.{name}.p{_quantile_label(quantile)}")
    jobs = metrics_cfg.get("job_queue", {})
    for name in jobs.get("names", []):
        for phase in jobs.get("_phases", []):
            for quantile in jobs.get("_quantiles", []):
                keys.add(f"job.{name}.{phase}.p{_quantile_label(quantile)}")
    return keys


def _quantile_label(quantile):
    """0.95 -> '95', 0.5 -> '50', matching capture_timings.py's key format."""
    return f"{quantile * 100:g}".replace(".", "")


def brackets(value, edges):
    """Return ``(lo, hi, hi_next)`` for the bucket ``(lo, hi]`` holding value."""
    padded = [0.0] + list(edges)
    for i in range(1, len(padded)):
        if value <= padded[i]:
            hi_next = padded[i + 1] if i + 1 < len(padded) else None
            return padded[i - 1], padded[i], hi_next
    return None, None, None


def resolve_override(key, thresholds):
    """Return the override rule for a key, or None if it falls back to defaults."""
    group, quantile = key.rsplit(".", 1)
    return thresholds.get("overrides", {}).get(group, {}).get(quantile)


def check_key(key, entry, thresholds, ladders):
    """Apply rules B, C, D and E to one gated key. Returns a list of failures."""
    value, unit = entry.get("value"), entry.get("unit", "")
    edges = ladders.get(unit)
    if value is None or edges is None:
        return [f"{key}: baseline has no value, or unknown unit {unit!r}"]

    failures = []
    first_edge = edges[0]
    quantile = int(key.rsplit(".p", 1)[1]) / 100.0
    if abs(value - quantile * first_edge) <= 1e-9 * first_edge:
        failures.append(
            f"{key}: baseline {value!r} equals quantile {quantile:g} x the ladder "
            f"floor {first_edge:g}{unit}, so every sample landed in the first "
            f"bucket and this is bucket arithmetic, not a latency. No absolute "
            f"bound can gate it -- add a finer ladder edge or drop the metric "
            f"from {METRICS} (rule E)"
        )
        return failures

    _, _, hi_next = brackets(value, edges)
    if hi_next is None:
        return [
            f"{key}: baseline {value!r}{unit} sits in or above the ladder's top "
            f"bucket, so there is no hi_next to derive a bound from -- extend the "
            f"ladder (rule C)"
        ]

    rule = resolve_override(key, thresholds)
    if rule is None:
        failures.append(
            f"{key}: no per-metric override, so it falls back to the defaults and "
            f"gates on the percentage bound alone. Add an override with "
            f"max_abs_increase = {hi_next - value!r} (rule B)"
        )
        return failures

    bound = rule.get("max_abs_increase_ms", rule.get("max_abs_increase_us"))
    expected = hi_next - value
    if bound is None or abs(bound - expected) > REL_TOLERANCE * expected:
        failures.append(
            f"{key}: absolute bound is {bound!r}, expected {expected!r} "
            f"(hi_next {hi_next:g} - baseline {value!r}) (rule C)"
        )

    pct = rule.get("max_pct_increase")
    if pct is None:
        failures.append(f"{key}: no max_pct_increase, so the metric never gates")
    elif bound is not None and pct >= 100.0 * bound / value:
        failures.append(
            f"{key}: max_pct_increase {pct:g}% is at or above the absolute bound's "
            f"{100.0 * bound / value:.1f}% of baseline, so the percentage bound "
            f"becomes the operative one and the bucket guarantee is lost. Lower it "
            f"or document the metric as percentage-gated (rule D)"
        )
    return failures


def main():
    missing = [
        p for p in (BASELINE, THRESHOLDS, METRICS, COLLECTOR, HEADER) if not p.exists()
    ]
    if missing:
        print("Cannot check workload regression bounds.", file=sys.stderr)
        for path in missing:
            print(f"  {path}: required input is absent", file=sys.stderr)
        print(
            "\nA missing input is not a reason to pass. Deleting or renaming one of\n"
            "these would otherwise leave the gate reporting success without having\n"
            "checked a single bound -- the failure this script exists to prevent. If\n"
            "the workload harness has genuinely moved, update the paths here.",
            file=sys.stderr,
        )
        return 1

    baseline = read_json_or_exit(BASELINE)
    thresholds = read_json_or_exit(THRESHOLDS)
    metrics_cfg = read_json_or_exit(METRICS)

    if baseline.get("placeholder") is True or not baseline.get("metrics"):
        print("OK: baseline is a placeholder, bounds cannot be derived yet")
        return 0

    ladders = {"ms": span_edges_ms(), "us": microsecond_edges()}
    gated = baseline["metrics"]
    failures = []

    declared = declared_keys(metrics_cfg)
    for key in sorted(set(gated) - declared):
        failures.append(
            f"{key}: in the baseline but not declared by {METRICS}, so it is "
            f"reported every run and can never gate -- remove it (rule A)"
        )
    for key in sorted(declared - set(gated)):
        failures.append(
            f"{key}: declared by {METRICS} but absent from the baseline, so it "
            f"never gates -- capture a baseline for it (rule A)"
        )

    for key in sorted(gated):
        if key in declared:
            failures.extend(check_key(key, gated[key], thresholds, ladders))

    if not failures:
        print(
            f"OK: {len(gated)} gated key(s); every absolute bound equals "
            f"hi_next - baseline, every key has an override, and the absolute "
            f"bound is the operative half of the AND for all of them"
        )
        return 0

    print(
        "Workload regression bounds are not derived from the baseline.", file=sys.stderr
    )
    for failure in failures:
        print(f"  {failure}", file=sys.stderr)
    print(
        f"\nThe rule is recorded in {THRESHOLDS} under _absolute_bound_derivation:\n"
        "a bound is hi_next - baseline, where hi_next is the edge above the top of\n"
        "the bucket holding the baseline. Refreshing a baseline therefore obliges\n"
        "you to re-derive its bound; see baselines/README.md.",
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    sys.exit(main())
