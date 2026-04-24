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
        "profile": "regression",
        "metrics": {
            "span.tx.process.p99": {"value": 12.4, "unit": "ms"},
            "rpc.server_info.p95": {"value": 850.0, "unit": "us"},
            ...
        }
    }

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
) -> dict:
    """Build and execute the query plan, return the full report dict."""
    plan = build_query_plan(metrics_path, window=window)
    logger.info("Capturing %d metrics from %s (window=%s)", len(plan), prom_url, window)

    async with aiohttp.ClientSession() as session:
        metrics = await run_query_plan(session, prom_url, plan)

    return {
        "schema_version": SCHEMA_VERSION,
        "captured_at": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "window": window,
        "git_sha": _detect_git_sha(),
        "profile": profile,
        "metrics": dict(sorted(metrics.items())),
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
        default="regression",
        help="Workload profile used during capture (metadata only)",
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
        )
    )

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with open(args.output, "w") as f:
        json.dump(report, f, indent=2, sort_keys=True)
        f.write("\n")

    captured = sum(1 for v in report["metrics"].values() if v["value"] is not None)
    total = len(report["metrics"])
    logger.info("Wrote %s (%d/%d metrics captured)", args.output, captured, total)
    return 0


if __name__ == "__main__":
    sys.exit(main())
