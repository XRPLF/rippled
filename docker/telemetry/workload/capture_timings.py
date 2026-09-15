#!/usr/bin/env python3
"""Capture OTel-derived timings from Prometheus for the regression gate.

Queries Prometheus for every metric declared in ``regression-metrics.json``
and writes the results to a JSON file in the exact schema
``baseline-timings.json`` expects. When a user wants to refresh the
baseline, they copy a CI run's ``timings.json`` artifact (or the block
printed to the workflow step summary) into
``baselines/baseline-timings.json`` in a reviewable PR.

Output schema (stable — ``compare_to_baseline.py`` reads it verbatim)::

    {
        "schema_version": 1,
        "captured_at": "2026-04-24T17:30:00Z",
        "window": "3m",
        "git_sha": "<from $GITHUB_SHA or `git rev-parse HEAD`>",
        "profile": "full-validation",
        "capture": {
            "declared": 20,
            "captured": 20,
            "min_ratio": 0.5,
            "complete": true
        },
        "metrics": {
            "span.tx.process.p99": {"value": 12.4, "unit": "ms"},
            "job.transaction.queued.p95": {"value": 850.0, "unit": "us"},
            ...
        }
    }

The ``capture`` block is what makes this file safe to use as baseline material.
The output is written BEFORE ``--min-capture-ratio`` is enforced, so a run that
reached too little of Prometheus still leaves a ``timings.json`` behind. That
file exists, parses, and carries every declared key — some with ``value: null``
— so a thin capture is indistinguishable from a good one to a reader who only
checks that the file is there. Pasted into
``baselines/baseline-timings.json`` it would narrow the gate to whichever keys
happened to come back, with nothing reporting that the gate had narrowed.

``complete`` is exactly the condition this script exits 0 on. It is computed
once, in ``_capture_status``, and drives both the exit code and the block, so
the two cannot disagree. Consumers read the flag rather than re-deriving the
ratio rule for themselves: the workflow's "Print regression summary" step, the
paste-me path in ``compare_to_baseline.py``, and a human reading the artifact
all get the same answer from one place. ``declared``, ``captured`` and
``min_ratio`` sit alongside it so a rejected capture can be judged without
re-running it.

The block is additive — a sibling of ``metrics``, never an entry inside it — so
it is neither a metric key nor a gated entry, and readers that predate it are
unaffected. Its ABSENCE means an artifact from before it existed, whose
completeness cannot be established; the paste-me paths treat that as not
complete rather than as complete.

Usage::

    python3 capture_timings.py \\
        --prometheus http://localhost:9090 \\
        --metrics regression-metrics.json \\
        --output /tmp/timings.json \\
        --window 3m \\
        --profile regression
"""

from __future__ import annotations

import argparse
import asyncio
import json
import logging
import os
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

import aiohttp

from prom_queries import build_query_plan, run_query_plan

logger = logging.getLogger("capture_timings")

SCHEMA_VERSION = 1


async def capture(
    prom_url: str,
    metrics_path: Path,
    window: str,
    profile: str,
    min_capture_ratio: float,
) -> dict:
    """Build and execute the query plan, return the full report dict.

    ``min_capture_ratio`` is recorded in the report rather than only applied to
    the exit code, so the artifact states the bar it was judged against.
    """
    plan = build_query_plan(metrics_path, window=window)
    logger.info("Capturing %d metrics from %s (window=%s)", len(plan), prom_url, window)

    async with aiohttp.ClientSession() as session:
        metrics = await run_query_plan(session, prom_url, plan)

    metrics = dict(sorted(metrics.items()))
    return {
        "schema_version": SCHEMA_VERSION,
        "captured_at": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "window": window,
        "git_sha": _detect_git_sha(),
        "profile": profile,
        "capture": _capture_status(metrics, min_capture_ratio),
        "metrics": metrics,
    }


def _capture_status(metrics: dict, min_ratio: float) -> dict:
    """Summarise how much of the declared surface this capture actually got.

    ``declared`` is every key the surface asked for; ``captured`` is how many
    came back with a value. ``complete`` is the single fact every consumer
    keys on, and it is the same predicate that decides this script's exit
    code — see the module docstring for why it lives in the artifact.

    An empty surface is NOT complete. ``declared == 0`` means the capture asked
    Prometheus for nothing: ``build_query_plan`` returns an empty plan, without
    complaining, for any config that yields no ``spans``/``rpc_methods``/
    ``job_queue`` entries -- a ``--metrics`` path pointing at the wrong file, a
    truncated one, or every key excluded. Nothing about that run is evidence
    the pipeline works, so treating it as vacuously complete would exit 0 and
    hand the paste-me path a ``metrics: {}`` artifact to offer as baseline
    material. Pasted in, it still reads as a placeholder, so the gate stays off
    while the workflow reports the baseline as activated -- the silent-green
    outcome the whole ``capture`` block exists to prevent. The exit code reads
    this same flag, so the two still cannot disagree.
    """
    declared = len(metrics)
    captured = sum(1 for entry in metrics.values() if entry["value"] is not None)
    return {
        "declared": declared,
        "captured": captured,
        "min_ratio": min_ratio,
        "complete": declared > 0 and (captured / declared) >= min_ratio,
    }


def _detect_git_sha() -> str:
    """Return the current commit SHA from env or git, else ``"unknown"``.

    Prefers ``GITHUB_SHA`` (set in Actions), falls back to ``git rev-parse``.
    Silent fallback is fine here — a missing SHA only affects the captured
    metadata, not the comparison logic.
    """
    env_sha = os.environ.get("GITHUB_SHA")
    if env_sha:
        return env_sha
    try:
        result = subprocess.run(
            ["git", "rev-parse", "HEAD"],
            capture_output=True,
            text=True,
            timeout=5,
            check=False,
        )
        if result.returncode == 0:
            return result.stdout.strip()
    except (OSError, subprocess.SubprocessError):
        pass
    return "unknown"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--prometheus",
        default="http://localhost:9090",
        help="Prometheus base URL (default: http://localhost:9090)",
    )
    parser.add_argument(
        "--metrics",
        type=Path,
        default=Path(__file__).parent / "regression-metrics.json",
        help="Path to regression-metrics.json",
    )
    parser.add_argument(
        "--output",
        type=Path,
        required=True,
        help="Where to write the captured timings JSON",
    )
    parser.add_argument(
        "--window",
        default="3m",
        help="Prometheus rate() window (default: 3m)",
    )
    parser.add_argument(
        "--profile",
        default="full-validation",
        help=(
            "Workload profile used during capture, recorded as metadata in the "
            "timings file (default: full-validation). Must name a profile in "
            "workload-profiles.json; run-full-validation.sh always passes this "
            "explicitly."
        ),
    )
    parser.add_argument(
        "--min-capture-ratio",
        type=float,
        default=0.5,
        help="Fail if fewer than this fraction of metrics are captured (default: 0.5)",
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="Enable debug logging",
    )
    args = parser.parse_args()

    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(levelname)s %(name)s: %(message)s",
    )

    report = asyncio.run(
        capture(
            prom_url=args.prometheus,
            metrics_path=args.metrics,
            window=args.window,
            profile=args.profile,
            min_capture_ratio=args.min_capture_ratio,
        )
    )

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with open(args.output, "w") as f:
        json.dump(report, f, indent=2, sort_keys=True)
        f.write("\n")

    # The exit code is read off the same flag the artifact carries, so a file
    # marked complete is always one this script exited 0 on.
    status = report["capture"]
    captured, total = status["captured"], status["declared"]
    logger.info("Wrote %s (%d/%d metrics captured)", args.output, captured, total)

    if not status["complete"]:
        if total == 0:
            # No ratio to report: nothing was asked for, so the shortfall is the
            # declared surface, not Prometheus. Named separately because the
            # percentage below would divide by zero.
            logger.error(
                "No metrics were declared, so nothing was captured. Does %s "
                "declare spans/rpc_methods/job_queue names, and does "
                "excluded_keys leave any of them gated? The file is marked "
                "capture.complete=false and must not be pasted into the baseline.",
                args.metrics,
            )
        else:
            logger.error(
                "Only %d/%d (%.0f%%) metrics captured — below the %.0f%% minimum. "
                "Is Prometheus reachable at %s? The file is marked "
                "capture.complete=false and must not be pasted into the baseline.",
                captured,
                total,
                captured / total * 100,
                args.min_capture_ratio * 100,
                args.prometheus,
            )
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
