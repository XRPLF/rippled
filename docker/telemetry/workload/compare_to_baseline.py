#!/usr/bin/env python3
"""Compare captured OTel timings against a committed baseline.

Operating modes (chosen automatically based on the baseline file contents):

1. **No baseline** — if ``baseline-timings.json`` has an empty
   ``metrics`` object (or is marked with ``"placeholder": true``), this
   script is in "populate" mode. It prints the captured timings JSON in
   the exact format expected for pasting into
   ``baselines/baseline-timings.json``, then exits 0. No regression check.

2. **Populated baseline** — per-metric percentage AND absolute deltas are
   computed against thresholds from ``regression-thresholds.json``. A
   regression occurs when BOTH bounds are breached for the same quantile.
   Prints a human-readable table and writes a full JSON report.
   Exits 1 if any regression was detected, else 0.

Inputs:
    --timings     Captured timings JSON (from capture_timings.py)
    --baseline    Committed baseline JSON
    --thresholds  Threshold policy JSON
    --report      Where to write regression-report.json (optional)

Exit codes:
    0 — No baseline (paste-me emitted), OR baseline populated and no regression
    1 — Regression detected (at least one metric breached both bounds)
    2 — Internal error (e.g. bad JSON, baseline/current key mismatch)
"""

from __future__ import annotations

import argparse
import json
import logging
import sys
from dataclasses import dataclass, asdict
from pathlib import Path
from typing import Any

logger = logging.getLogger("compare_to_baseline")


@dataclass
class MetricDelta:
    """Single metric's baseline-vs-current comparison outcome.

    Attributes:
        key:                Flat metric key (e.g. span.tx.process.p99).
        baseline:           Baseline value (may be None if unpopulated).
        current:            Current run value (may be None if not captured).
        delta:              current - baseline (None if either side None).
        pct_change:         100 * delta / baseline (None if baseline ≤ 0).
        unit:               Unit from baseline (preserved as-is).
        threshold_pct:      Resolved per-metric pct threshold.
        threshold_abs:      Resolved per-metric absolute threshold.
        regressed:          True iff both bounds breached.
        note:               Human-readable classification when not regressed.
    """

    key: str
    baseline: float | None
    current: float | None
    delta: float | None
    pct_change: float | None
    unit: str
    threshold_pct: float | None
    threshold_abs: float | None
    regressed: bool
    note: str


def load_json(path: Path) -> dict:
    with open(path) as f:
        return json.load(f)


def is_placeholder(baseline: dict) -> bool:
    """A baseline is a placeholder if explicitly marked OR metrics are empty."""
    if baseline.get("placeholder") is True:
        return True
    return not baseline.get("metrics")


def print_paste_me(timings: dict) -> None:
    """Print captured timings in the exact baseline-timings.json format.

    The output between the two banner lines is the file contents to paste,
    byte-for-byte — sorted keys, 2-space indent, trailing newline.
    """
    banner = "=" * 72
    print(banner, file=sys.stderr)
    print(
        "  NO BASELINE FOUND — paste the JSON below into",
        file=sys.stderr,
    )
    print(
        "  docker/telemetry/workload/baselines/baseline-timings.json",
        file=sys.stderr,
    )
    print(banner, file=sys.stderr)

    print(json.dumps(timings, indent=2, sort_keys=True))

    print(banner, file=sys.stderr)
    print(
        "  (End of paste-me JSON. Gate did NOT run — baseline is empty.)",
        file=sys.stderr,
    )
    print(banner, file=sys.stderr)


def resolve_thresholds(
    key: str,
    thresholds: dict,
) -> tuple[float | None, float | None]:
    """Return ``(pct_threshold, abs_threshold)`` for a metric key.

    Per-metric overrides win over defaults. Returns ``(None, None)`` if no
    threshold is defined for this category/quantile — such metrics are
    captured but never gate the build.
    """
    parts = key.split(".")
    if len(parts) < 3:
        return (None, None)
    category_key = parts[0]
    quantile_key = parts[-1]

    category_map = {
        "span": "span",
        "rpc": "rpc_method",
        "job": "job_queue",
    }
    cat = category_map.get(category_key)
    if cat is None:
        return (None, None)

    override_prefix_key = ".".join(parts[:-1])
    override_key = f"{category_key}.{'.'.join(parts[1:-1])}"
    overrides = thresholds.get("overrides", {})
    defaults = thresholds.get("defaults", {}).get(cat, {})

    rule = overrides.get(override_key, {}).get(quantile_key)
    if rule is None:
        rule = defaults.get(quantile_key)
    if rule is None:
        return (None, None)

    pct = rule.get("max_pct_increase")
    abs_bound = rule.get("max_abs_increase_ms") or rule.get("max_abs_increase_us")
    return (pct, abs_bound)


def compute_delta(
    key: str,
    baseline_entry: dict | None,
    current_entry: dict | None,
    thresholds: dict,
) -> MetricDelta:
    """Compute a MetricDelta for one metric key.

    A regression requires BOTH bounds to be breached simultaneously. This
    tolerates small-value noise: a 100% increase on a 0.5 ms metric
    (to 1.0 ms) is not a regression under a 5 ms absolute bound.
    """
    baseline = baseline_entry.get("value") if baseline_entry else None
    current = current_entry.get("value") if current_entry else None
    unit = (baseline_entry or current_entry or {}).get("unit", "")

    pct_threshold, abs_threshold = resolve_thresholds(key, thresholds)

    if baseline is None and current is None:
        return MetricDelta(
            key=key,
            baseline=None,
            current=None,
            delta=None,
            pct_change=None,
            unit=unit,
            threshold_pct=pct_threshold,
            threshold_abs=abs_threshold,
            regressed=False,
            note="no data (neither baseline nor current)",
        )

    if baseline is None:
        return MetricDelta(
            key=key,
            baseline=None,
            current=current,
            delta=None,
            pct_change=None,
            unit=unit,
            threshold_pct=pct_threshold,
            threshold_abs=abs_threshold,
            regressed=False,
            note="new metric (not in baseline)",
        )

    if current is None:
        return MetricDelta(
            key=key,
            baseline=baseline,
            current=None,
            delta=None,
            pct_change=None,
            unit=unit,
            threshold_pct=pct_threshold,
            threshold_abs=abs_threshold,
            regressed=False,
            note="not captured in current run",
        )

    delta = current - baseline
    pct_change = (delta / baseline * 100.0) if baseline > 0 else None

    if pct_threshold is None or abs_threshold is None:
        return MetricDelta(
            key=key,
            baseline=baseline,
            current=current,
            delta=delta,
            pct_change=pct_change,
            unit=unit,
            threshold_pct=pct_threshold,
            threshold_abs=abs_threshold,
            regressed=False,
            note="no threshold configured",
        )

    pct_breach = pct_change is not None and pct_change > pct_threshold
    abs_breach = delta > abs_threshold
    regressed = pct_breach and abs_breach

    if regressed:
        note = "REGRESSION"
    elif delta < 0:
        note = "improved"
    else:
        note = "within bounds"

    return MetricDelta(
        key=key,
        baseline=baseline,
        current=current,
        delta=delta,
        pct_change=pct_change,
        unit=unit,
        threshold_pct=pct_threshold,
        threshold_abs=abs_threshold,
        regressed=regressed,
        note=note,
    )


def print_summary(deltas: list[MetricDelta]) -> None:
    """Print a sorted, human-readable table of per-metric results."""
    regressions = [d for d in deltas if d.regressed]
    improvements = [
        d
        for d in deltas
        if d.delta is not None and d.delta < 0 and d.baseline not in (None, 0)
    ]
    improvements.sort(key=lambda d: d.pct_change or 0)
    regressions.sort(key=lambda d: -(d.pct_change or 0))

    print("=" * 72)
    print(f"  Regression check: {len(regressions)} regression(s) detected")
    print("=" * 72)

    if regressions:
        print("\nRegressions (breached BOTH pct AND absolute bounds):")
        _print_table(regressions)

    if improvements:
        top = improvements[:5]
        print("\nTop improvements:")
        _print_table(top)

    missing = [d for d in deltas if d.note == "not captured in current run"]
    if missing:
        print(f"\n{len(missing)} baseline metric(s) not captured in current run:")
        for d in missing:
            print(f"  {d.key}")


def _print_table(rows: list[MetricDelta]) -> None:
    """Print a fixed-width table for a list of deltas."""
    header = f"  {'METRIC':<45} {'BASE':>10} {'CUR':>10} {'Δ':>10} {'%':>8} UNIT"
    print(header)
    print("  " + "-" * (len(header) - 2))
    for d in rows:
        base = f"{d.baseline:.2f}" if d.baseline is not None else "-"
        cur = f"{d.current:.2f}" if d.current is not None else "-"
        delta = f"{d.delta:+.2f}" if d.delta is not None else "-"
        pct = f"{d.pct_change:+.1f}%" if d.pct_change is not None else "-"
        print(f"  {d.key:<45} {base:>10} {cur:>10} {delta:>10} {pct:>8} {d.unit}")


def write_report(
    deltas: list[MetricDelta],
    report_path: Path,
    baseline: dict,
    timings: dict,
) -> None:
    """Write regression-report.json — machine-readable artifact for CI."""
    regressions = [d for d in deltas if d.regressed]
    payload = {
        "schema_version": 1,
        "baseline_captured_at": baseline.get("captured_at"),
        "baseline_git_sha": baseline.get("git_sha"),
        "current_captured_at": timings.get("captured_at"),
        "current_git_sha": timings.get("git_sha"),
        "window": timings.get("window"),
        "profile": timings.get("profile"),
        "summary": {
            "total": len(deltas),
            "regressions": len(regressions),
            "improvements": sum(
                1
                for d in deltas
                if d.delta is not None and d.delta < 0 and d.baseline not in (None, 0)
            ),
            "missing_in_current": sum(
                1 for d in deltas if d.note == "not captured in current run"
            ),
        },
        "metrics": [asdict(d) for d in deltas],
    }
    report_path.parent.mkdir(parents=True, exist_ok=True)
    with open(report_path, "w") as f:
        json.dump(payload, f, indent=2, sort_keys=True)
        f.write("\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--timings",
        type=Path,
        required=True,
        help="Captured timings JSON (from capture_timings.py)",
    )
    parser.add_argument(
        "--baseline",
        type=Path,
        required=True,
        help="Committed baseline-timings.json",
    )
    parser.add_argument(
        "--thresholds",
        type=Path,
        default=Path(__file__).parent / "regression-thresholds.json",
        help="Threshold policy JSON",
    )
    parser.add_argument(
        "--report",
        type=Path,
        default=None,
        help="Where to write regression-report.json (optional)",
    )
    args = parser.parse_args()

    logging.basicConfig(
        level=logging.INFO,
        format="%(levelname)s %(name)s: %(message)s",
    )

    try:
        timings = load_json(args.timings)
        baseline = load_json(args.baseline)
        thresholds = load_json(args.thresholds)
    except (OSError, json.JSONDecodeError) as exc:
        logger.error("failed to load inputs: %s", exc)
        return 2

    if is_placeholder(baseline):
        print_paste_me(timings)
        return 0

    baseline_metrics = baseline.get("metrics", {})
    current_metrics = timings.get("metrics", {})

    all_keys = sorted(set(baseline_metrics) | set(current_metrics))
    deltas = [
        compute_delta(
            key,
            baseline_metrics.get(key),
            current_metrics.get(key),
            thresholds,
        )
        for key in all_keys
    ]

    print_summary(deltas)

    if args.report:
        write_report(deltas, args.report, baseline, timings)
        logger.info("wrote %s", args.report)

    return 1 if any(d.regressed for d in deltas) else 0


if __name__ == "__main__":
    sys.exit(main())
