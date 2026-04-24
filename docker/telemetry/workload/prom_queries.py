#!/usr/bin/env python3
"""Shared Prometheus query helpers for the regression gate.

Single source of truth for how regression metrics are computed. Both
``capture_timings.py`` and any future tooling consume this module so metric
name → PromQL expression stays consistent.

Design:
- Every captured metric has a key in the form ``{category}.{name}.p{quantile}``
  (e.g. ``span.tx.process.p99``). Keys are flat strings so JSON diffing is
  trivial.
- Quantile queries go through ``histogram_quantile`` over the standard
  ``_bucket`` series. The rate window is a parameter (defaults to the
  capture window, not Prometheus's default 5m) so short CI runs are usable.
- The catalogue of what to capture lives in ``regression-metrics.json`` —
  this module only knows how to translate that JSON into HTTP queries.

Usage::

    import asyncio, aiohttp
    from prom_queries import build_query_plan, run_query_plan

    plan = build_query_plan("regression-metrics.json", window="3m")
    async with aiohttp.ClientSession() as s:
        timings = await run_query_plan(s, "http://localhost:9090", plan)
    # timings = {"span.tx.process.p99": 12.4, ...}
"""

from __future__ import annotations

import json
import logging
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import aiohttp

logger = logging.getLogger("prom_queries")


@dataclass(frozen=True)
class QueryEntry:
    """One metric to capture from Prometheus.

    Attributes:
        key:      Flat output key, e.g. ``span.tx.process.p99``.
        promql:   The PromQL expression to send to /api/v1/query.
        unit:     Unit of the returned value, e.g. ``ms`` or ``us``.
                  Baseline JSON preserves this so the comparator can
                  sanity-check unit drift.
    """

    key: str
    promql: str
    unit: str


def build_query_plan(metrics_path: str | Path, window: str = "3m") -> list[QueryEntry]:
    """Translate regression-metrics.json into a list of PromQL queries.

    Args:
        metrics_path: Path to ``regression-metrics.json``.
        window:       Rate window passed to ``rate()``. For short CI runs
                      keep this close to the test duration so the bucket
                      counts are meaningful. Default 3m matches the
                      ``regression`` workload profile.

    Returns:
        A list of ``QueryEntry`` values, one per (metric × quantile).
    """
    with open(metrics_path) as f:
        cfg = json.load(f)

    plan: list[QueryEntry] = []

    span_cfg = cfg.get("spans", {})
    tmpl = span_cfg.get("_query_template", "")
    unit = span_cfg.get("_unit", "ms")
    for name in span_cfg.get("names", []):
        for q in span_cfg.get("_quantiles", []):
            expr = (
                tmpl.replace("{quantile}", _format_quantile(q))
                .replace("{name}", name)
                .replace("{window}", window)
            )
            plan.append(
                QueryEntry(
                    key=f"span.{name}.p{_quantile_label(q)}",
                    promql=expr,
                    unit=unit,
                )
            )

    rpc_cfg = cfg.get("rpc_methods", {})
    tmpl = rpc_cfg.get("_query_template", "")
    unit = rpc_cfg.get("_unit", "us")
    for name in rpc_cfg.get("names", []):
        for q in rpc_cfg.get("_quantiles", []):
            expr = (
                tmpl.replace("{quantile}", _format_quantile(q))
                .replace("{name}", name)
                .replace("{window}", window)
            )
            plan.append(
                QueryEntry(
                    key=f"rpc.{name}.p{_quantile_label(q)}",
                    promql=expr,
                    unit=unit,
                )
            )

    job_cfg = cfg.get("job_queue", {})
    unit = job_cfg.get("_unit", "us")
    phases = job_cfg.get("_phases", ["queued", "running"])
    tmpl_map = {
        "queued": job_cfg.get("_queued_template", ""),
        "running": job_cfg.get("_running_template", ""),
    }
    for name in job_cfg.get("names", []):
        for phase in phases:
            tmpl = tmpl_map.get(phase, "")
            if not tmpl:
                continue
            for q in job_cfg.get("_quantiles", []):
                expr = (
                    tmpl.replace("{quantile}", _format_quantile(q))
                    .replace("{name}", name)
                    .replace("{window}", window)
                )
                plan.append(
                    QueryEntry(
                        key=f"job.{name}.{phase}.p{_quantile_label(q)}",
                        promql=expr,
                        unit=unit,
                    )
                )

    return plan


async def run_query_plan(
    session: aiohttp.ClientSession,
    prom_url: str,
    plan: list[QueryEntry],
) -> dict[str, dict[str, Any]]:
    """Execute a query plan and return a flat ``key → {value, unit}`` map.

    Queries that return no data (NaN, empty result) are still included in
    the output with ``value: null`` — the comparator treats missing values
    as "not yet observed" rather than as a regression. This keeps the
    baseline schema stable across runs with different load levels.

    Args:
        session:  Shared aiohttp session.
        prom_url: Base URL of Prometheus (e.g. ``http://localhost:9090``).
        plan:     Output of :func:`build_query_plan`.

    Returns:
        Mapping from metric key to ``{"value": float|None, "unit": str}``.
    """
    results: dict[str, dict[str, Any]] = {}
    for entry in plan:
        value = await _instant_query(session, prom_url, entry.promql)
        results[entry.key] = {"value": value, "unit": entry.unit}
    return results


async def _instant_query(
    session: aiohttp.ClientSession,
    prom_url: str,
    promql: str,
) -> float | None:
    """POST an instant query to Prometheus; return the scalar value or None.

    None is returned for NaN, empty results, or HTTP errors — every call
    site treats None identically ("no data captured").
    """
    url = f"{prom_url.rstrip('/')}/api/v1/query"
    try:
        async with session.post(url, data={"query": promql}, timeout=30) as resp:
            if resp.status != 200:
                logger.warning("query HTTP %d: %s", resp.status, promql)
                return None
            body = await resp.json()
    except (aiohttp.ClientError, TimeoutError) as exc:
        logger.warning("query failed: %s — %s", promql, exc)
        return None

    if body.get("status") != "success":
        logger.warning("query status=%s: %s", body.get("status"), promql)
        return None

    result = body.get("data", {}).get("result", [])
    if not result:
        return None

    raw = result[0].get("value", [None, None])[1]
    if raw is None or raw in ("NaN", "+Inf", "-Inf"):
        return None
    try:
        return float(raw)
    except (TypeError, ValueError):
        return None


def _format_quantile(q: float) -> str:
    """Format a quantile for PromQL (``0.99`` → ``"0.99"``)."""
    return f"{q:g}"


def _quantile_label(q: float) -> str:
    """Format a quantile for the output key (``0.95`` → ``"95"``)."""
    return str(int(round(q * 100)))
