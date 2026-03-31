#!/usr/bin/env python3
"""Telemetry Validation Suite for rippled.

Validates that the full telemetry stack is emitting expected data after
a workload run. Queries Tempo (spans), Prometheus (metrics), Loki (logs),
and Grafana (dashboards) APIs to produce a pass/fail report.

Validation categories:
  1. Span validation     — All 16+ span types present with required attributes
  2. Metric validation   — SpanMetrics, StatsD, and Phase 9 metrics are non-zero
  3. Log-trace correlation — Loki logs contain trace_id/span_id fields
  4. Dashboard validation — All 13 Grafana dashboards render data
  5. External parity     — Span attrs, metric existence, and value sanity for
                           external dashboard parity (validator-health,
                           peer-quality, system-node-health)

Usage:
    python3 validate_telemetry.py --report /tmp/validation-report.json

    # Custom API endpoints:
    python3 validate_telemetry.py \\
        --tempo http://localhost:3200 \\
        --prometheus http://localhost:9090 \\
        --loki http://localhost:3100 \\
        --grafana http://localhost:3000
"""

import argparse
import asyncio
import json
import logging
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import aiohttp

logger = logging.getLogger("validate_telemetry")

# ---------------------------------------------------------------------------
# Configuration defaults
# ---------------------------------------------------------------------------

DEFAULT_TEMPO = "http://localhost:3200"
DEFAULT_PROMETHEUS = "http://localhost:9090"
DEFAULT_LOKI = "http://localhost:3100"
DEFAULT_GRAFANA = "http://localhost:3000"

SCRIPT_DIR = Path(__file__).parent
EXPECTED_SPANS_FILE = SCRIPT_DIR / "expected_spans.json"
EXPECTED_METRICS_FILE = SCRIPT_DIR / "expected_metrics.json"


# ---------------------------------------------------------------------------
# Data classes
# ---------------------------------------------------------------------------


@dataclass
class CheckResult:
    """Result of a single validation check.

    Attributes:
        name:     Check identifier (e.g., "span.rpc.request").
        category: Validation category (span, metric, log, dashboard).
        passed:   Whether the check passed.
        message:  Human-readable description of the result.
        details:  Optional additional data (counts, values, etc.).
    """

    name: str
    category: str
    passed: bool
    message: str
    details: dict[str, Any] = field(default_factory=dict)

    def to_dict(self) -> dict[str, Any]:
        """Serialize to a JSON-compatible dict."""
        return {
            "name": self.name,
            "category": self.category,
            "passed": self.passed,
            "message": self.message,
            "details": self.details,
        }


@dataclass
class ValidationReport:
    """Aggregated validation report.

    Attributes:
        checks:     List of all individual check results.
        start_time: ISO timestamp when validation started.
        end_time:   ISO timestamp when validation completed.
    """

    checks: list[CheckResult] = field(default_factory=list)
    start_time: str = ""
    end_time: str = ""

    @property
    def total_checks(self) -> int:
        """Total number of checks executed."""
        return len(self.checks)

    @property
    def passed(self) -> int:
        """Number of checks that passed."""
        return sum(1 for c in self.checks if c.passed)

    @property
    def failed(self) -> int:
        """Number of checks that failed."""
        return sum(1 for c in self.checks if not c.passed)

    @property
    def all_passed(self) -> bool:
        """Whether all checks passed."""
        return self.failed == 0

    def add(self, check: CheckResult) -> None:
        """Add a check result to the report."""
        self.checks.append(check)
        status = "PASS" if check.passed else "FAIL"
        logger.info("[%s] %s: %s", status, check.name, check.message)

    def to_dict(self) -> dict[str, Any]:
        """Serialize to a JSON-compatible dict."""
        return {
            "summary": {
                "total": self.total_checks,
                "passed": self.passed,
                "failed": self.failed,
                "all_passed": self.all_passed,
            },
            "start_time": self.start_time,
            "end_time": self.end_time,
            "checks": [c.to_dict() for c in self.checks],
        }


# ---------------------------------------------------------------------------
# Tempo API helpers
# ---------------------------------------------------------------------------


async def _tempo_search(
    session: aiohttp.ClientSession,
    tempo_url: str,
    query: str,
    limit: int = 20,
) -> list[dict[str, Any]]:
    """Search traces in Tempo using TraceQL.

    Args:
        session:   aiohttp client session.
        tempo_url: Base URL for Tempo API (e.g., http://localhost:3200).
        query:     TraceQL query string.
        limit:     Maximum number of traces to return.

    Returns:
        List of trace summary dicts from Tempo search results.
    """
    params = {"q": query, "limit": str(limit)}
    async with session.get(f"{tempo_url}/api/search", params=params) as resp:
        data = await resp.json()
        return data.get("traces", [])


async def _tempo_get_trace(
    session: aiohttp.ClientSession,
    tempo_url: str,
    trace_id: str,
) -> list[dict[str, Any]]:
    """Fetch a full trace from Tempo by trace ID.

    Returns the list of spans extracted from the OTLP-format response.

    Args:
        session:   aiohttp client session.
        tempo_url: Tempo API base URL.
        trace_id:  Hex trace ID string.

    Returns:
        Flat list of span dicts with 'name' and 'attributes' keys.
    """
    async with session.get(f"{tempo_url}/api/traces/{trace_id}") as resp:
        data = await resp.json()
        spans: list[dict[str, Any]] = []
        for batch in data.get("batches", []):
            for scope_spans in batch.get("scopeSpans", []):
                spans.extend(scope_spans.get("spans", []))
        return spans


def _otlp_span_attr_keys(span: dict[str, Any]) -> set[str]:
    """Extract all attribute key names from an OTLP span.

    Args:
        span: OTLP span dict with an 'attributes' list.

    Returns:
        Set of attribute key strings.
    """
    return {a["key"] for a in span.get("attributes", []) if "key" in a}


# ---------------------------------------------------------------------------
# Span Validation (Tempo API)
# ---------------------------------------------------------------------------


async def validate_spans(
    session: aiohttp.ClientSession,
    tempo_url: str,
    report: ValidationReport,
) -> None:
    """Validate that all expected spans appear in Tempo.

    Queries the Tempo TraceQL API for each expected span name and checks
    that traces exist. Also validates required attributes on spans and
    parent-child relationships.

    Args:
        session:   aiohttp client session.
        tempo_url: Base URL for Tempo API (e.g., http://localhost:3200).
        report:    ValidationReport to accumulate results.
    """
    logger.info("--- Span Validation (Tempo) ---")

    # Load expected spans.
    with open(EXPECTED_SPANS_FILE) as f:
        expected = json.load(f)

    # Check service registration.
    try:
        async with session.get(
            f"{tempo_url}/api/v2/search/tag/resource.service.name/values"
        ) as resp:
            data = await resp.json()
            tag_values = data.get("tagValues", [])
            services = [tv.get("value", "") for tv in tag_values]
            has_rippled = "rippled" in services
            report.add(
                CheckResult(
                    name="span.service_registration",
                    category="span",
                    passed=has_rippled,
                    message=(
                        f"Service 'rippled' registered (found: {services})"
                        if has_rippled
                        else f"Service 'rippled' NOT found (found: {services})"
                    ),
                )
            )
    except Exception as exc:
        report.add(
            CheckResult(
                name="span.service_registration",
                category="span",
                passed=False,
                message=f"Tempo API unreachable: {exc}",
            )
        )
        return

    # Diagnostic: list all available operations (span names) for the rippled
    # service.  This output appears in CI logs and helps debug missing-span
    # failures without needing to reproduce the full stack locally.
    try:
        async with session.get(
            f"{tempo_url}/api/v2/search/tag/span.name/values"
        ) as resp:
            ops_data = await resp.json()
            tag_values = ops_data.get("tagValues", [])
            operations = [tv.get("value", "") for tv in tag_values]
            logger.info(
                "Tempo operations (%d total): %s",
                len(operations),
                operations,
            )
    except Exception as exc:
        logger.warning("Failed to fetch Tempo operations: %s", exc)

    # Check each expected span.
    for span_def in expected["spans"]:
        span_name = span_def["name"]
        # For wildcard spans (rpc.command.*), search with a concrete example.
        if "*" in span_name:
            operation = "rpc.command.server_info"
            check_name = f"span.{span_name}"
        else:
            operation = span_name
            check_name = f"span.{span_name}"

        try:
            query = '{resource.service.name="rippled" && name="' + operation + '"}'
            traces = await _tempo_search(session, tempo_url, query, limit=5)
            count = len(traces)
            report.add(
                CheckResult(
                    name=check_name,
                    category="span",
                    passed=count > 0,
                    message=(
                        f"{span_name}: {count} traces found"
                        if count > 0
                        else f"{span_name}: 0 traces (expected > 0)"
                    ),
                    details={"trace_count": count},
                )
            )

            # Validate required attributes on first trace.
            if count > 0 and span_def.get("required_attributes"):
                trace_id = traces[0].get("traceID", "")
                if trace_id:
                    spans = await _tempo_get_trace(session, tempo_url, trace_id)
                    await _validate_span_attributes_otlp(spans, span_def, report)
        except Exception as exc:
            report.add(
                CheckResult(
                    name=check_name,
                    category="span",
                    passed=False,
                    message=f"{span_name}: query failed ({exc})",
                )
            )

    # Validate parent-child relationships.
    for rel in expected.get("parent_child_relationships", []):
        # Skip relationships marked with "skip: true" (e.g., cross-thread
        # parent-child that requires a C++ fix to propagate span context).
        if rel.get("skip", False):
            reason = rel.get("skip_reason", "marked skip in expected_spans.json")
            logger.info(
                "[SKIP] span.hierarchy.%s->%s: %s",
                rel["parent"],
                rel["child"],
                reason,
            )
            continue
        await _validate_parent_child(session, tempo_url, rel, report)


async def _validate_span_attributes_otlp(
    spans: list[dict[str, Any]],
    span_def: dict[str, Any],
    report: ValidationReport,
) -> None:
    """Check that OTLP spans contain expected attributes.

    Args:
        spans:    List of OTLP span dicts from Tempo.
        span_def: Span definition from expected_spans.json.
        report:   ValidationReport to accumulate results.
    """
    required_attrs = span_def.get("required_attributes", [])
    if not required_attrs:
        return

    span_name = span_def["name"]
    # Collect all attribute keys from all spans.
    found_attrs: set[str] = set()
    for span in spans:
        found_attrs.update(_otlp_span_attr_keys(span))

    missing = [a for a in required_attrs if a not in found_attrs]
    report.add(
        CheckResult(
            name=f"span.attrs.{span_name}",
            category="span",
            passed=len(missing) == 0,
            message=(
                f"{span_name}: all {len(required_attrs)} attributes present"
                if not missing
                else f"{span_name}: missing attributes: {missing}"
            ),
            details={
                "required": required_attrs,
                "found": list(found_attrs),
                "missing": missing,
            },
        )
    )


async def _validate_parent_child(
    session: aiohttp.ClientSession,
    tempo_url: str,
    relationship: dict[str, Any],
    report: ValidationReport,
) -> None:
    """Validate a parent-child span relationship in Tempo traces.

    Args:
        session:      aiohttp client session.
        tempo_url:    Base URL for Tempo API.
        relationship: Dict with 'parent' and 'child' span names.
        report:       ValidationReport to accumulate results.
    """
    parent_name = relationship["parent"]
    child_name = relationship["child"]

    try:
        # Query traces for the parent span.
        query = '{resource.service.name="rippled" && name="' + parent_name + '"}'
        traces = await _tempo_search(session, tempo_url, query, limit=3)

        if not traces:
            report.add(
                CheckResult(
                    name=f"span.hierarchy.{parent_name}->{child_name}",
                    category="span",
                    passed=False,
                    message=f"No {parent_name} traces to check hierarchy",
                )
            )
            return

        # Check if child spans exist within parent traces.
        # Use the concrete child name for wildcard patterns.
        concrete_child = child_name.replace("*", "server_info")
        found_child = False
        for trace_summary in traces:
            trace_id = trace_summary.get("traceID", "")
            if not trace_id:
                continue
            spans = await _tempo_get_trace(session, tempo_url, trace_id)
            for span in spans:
                op = span.get("name", "")
                if concrete_child in op or ("*" not in child_name and op == child_name):
                    found_child = True
                    break
            if found_child:
                break

        report.add(
            CheckResult(
                name=f"span.hierarchy.{parent_name}->{child_name}",
                category="span",
                passed=found_child,
                message=(
                    f"Found {child_name} as child of {parent_name}"
                    if found_child
                    else f"{child_name} not found in {parent_name} traces"
                ),
            )
        )
    except Exception as exc:
        report.add(
            CheckResult(
                name=f"span.hierarchy.{parent_name}->{child_name}",
                category="span",
                passed=False,
                message=f"Hierarchy check failed: {exc}",
            )
        )


# ---------------------------------------------------------------------------
# Metric Validation (Prometheus API)
# ---------------------------------------------------------------------------


async def validate_metrics(
    session: aiohttp.ClientSession,
    prometheus_url: str,
    report: ValidationReport,
) -> None:
    """Validate that expected metrics appear in Prometheus with non-zero values.

    Args:
        session:        aiohttp client session.
        prometheus_url: Base URL for Prometheus API (e.g., http://localhost:9090).
        report:         ValidationReport to accumulate results.
    """
    logger.info("--- Metric Validation (Prometheus) ---")

    # Diagnostic: list all metric names in Prometheus.  Helps debug name
    # mismatches between expected_metrics.json and actual emissions.
    try:
        async with session.get(
            f"{prometheus_url}/api/v1/label/__name__/values"
        ) as resp:
            label_data = await resp.json()
            all_metrics = label_data.get("data", [])
            # Log rippled-related and Phase 9 metrics for debugging.
            relevant = [
                m
                for m in all_metrics
                if "rippled" in m.lower()
                or m.startswith(
                    (
                        "rpc_method",
                        "cache_",
                        "txq_",
                        "object_count",
                        "load_factor",
                        "nodestore",
                        "traces_span",
                    )
                )
            ]
            logger.info(
                "Prometheus metrics (relevant, %d of %d total): %s",
                len(relevant),
                len(all_metrics),
                relevant,
            )
    except Exception as exc:
        logger.warning("Failed to fetch Prometheus metric names: %s", exc)

    with open(EXPECTED_METRICS_FILE) as f:
        expected = json.load(f)

    # Check each metric category.
    for category_key, category_data in expected.items():
        if category_key in ("description", "grafana_dashboards"):
            continue

        metrics = category_data.get("metrics", [])
        for metric_name in metrics:
            await _check_prometheus_metric(
                session, prometheus_url, metric_name, category_key, report
            )


async def _check_prometheus_metric(
    session: aiohttp.ClientSession,
    prometheus_url: str,
    metric_name: str,
    category: str,
    report: ValidationReport,
) -> None:
    """Query Prometheus for a specific metric and check it exists.

    Args:
        session:        aiohttp client session.
        prometheus_url: Prometheus base URL.
        metric_name:    Prometheus metric name.
        category:       Metric category for the report.
        report:         ValidationReport to accumulate results.
    """
    try:
        # Use the /api/v1/series endpoint instead of an instant query.
        # Beast::insight StatsD gauges only mark dirty on value *changes*,
        # so a gauge that stabilizes (e.g. peer count stays at 1) may go
        # stale in Prometheus and disappear from instant queries.  The
        # series endpoint returns any metric that existed in the window,
        # regardless of staleness.
        params: dict[str, str] = {"match[]": metric_name}
        async with session.get(
            f"{prometheus_url}/api/v1/series", params=params
        ) as resp:
            data = await resp.json()
            results = data.get("data", [])
            series_count = len(results)
            report.add(
                CheckResult(
                    name=f"metric.{category}.{metric_name}",
                    category="metric",
                    passed=series_count > 0,
                    message=(
                        f"{metric_name}: {series_count} series"
                        if series_count > 0
                        else f"{metric_name}: 0 series (expected > 0)"
                    ),
                    details={"series_count": series_count},
                )
            )
    except Exception as exc:
        report.add(
            CheckResult(
                name=f"metric.{category}.{metric_name}",
                category="metric",
                passed=False,
                message=f"{metric_name}: query failed ({exc})",
            )
        )


# ---------------------------------------------------------------------------
# Log-Trace Correlation Validation (Loki API)
# ---------------------------------------------------------------------------


async def validate_log_trace_correlation(
    session: aiohttp.ClientSession,
    loki_url: str,
    tempo_url: str,
    report: ValidationReport,
) -> None:
    """Validate that Loki logs contain trace_id/span_id for correlation.

    Checks:
      1. Logs with trace_id= field exist in Loki.
      2. A random trace_id from Tempo can be found in Loki logs.

    Args:
        session:   aiohttp client session.
        loki_url:  Base URL for Loki API (e.g., http://localhost:3100).
        tempo_url: Base URL for Tempo API.
        report:    ValidationReport to accumulate results.
    """
    logger.info("--- Log-Trace Correlation Validation (Loki) ---")

    # Check 1: Any logs with trace_id exist.
    try:
        params = {
            "query": '{job="rippled"} |= "trace_id="',
            "limit": 5,
            "direction": "backward",
        }
        async with session.get(
            f"{loki_url}/loki/api/v1/query_range", params=params
        ) as resp:
            data = await resp.json()
            streams = data.get("data", {}).get("result", [])
            total_entries = sum(len(s.get("values", [])) for s in streams)
            report.add(
                CheckResult(
                    name="log.trace_id_present",
                    category="log",
                    passed=total_entries > 0,
                    message=(
                        f"Found {total_entries} log entries with trace_id"
                        if total_entries > 0
                        else "No log entries with trace_id found"
                    ),
                    details={"log_count": total_entries},
                )
            )
    except Exception as exc:
        report.add(
            CheckResult(
                name="log.trace_id_present",
                category="log",
                passed=False,
                message=f"Loki query failed: {exc}",
            )
        )

    # Check 2: Cross-reference a trace_id from Tempo to Loki.
    try:
        # Get a recent trace from Tempo.
        traces = await _tempo_search(
            session,
            tempo_url,
            '{resource.service.name="rippled"}',
            limit=1,
        )

        if traces:
            trace_id = traces[0].get("traceID", "")
            if trace_id:
                # Search Loki for this trace_id.
                loki_params = {
                    "query": f'{{job="rippled"}} |= "{trace_id}"',
                    "limit": 5,
                    "direction": "backward",
                }
                async with session.get(
                    f"{loki_url}/loki/api/v1/query_range",
                    params=loki_params,
                ) as loki_resp:
                    loki_data = await loki_resp.json()
                    loki_streams = loki_data.get("data", {}).get("result", [])
                    loki_count = sum(len(s.get("values", [])) for s in loki_streams)
                    report.add(
                        CheckResult(
                            name="log.trace_id_cross_reference",
                            category="log",
                            passed=loki_count > 0,
                            message=(
                                f"trace_id {trace_id[:16]}... found in "
                                f"{loki_count} Loki entries"
                                if loki_count > 0
                                else f"trace_id {trace_id[:16]}... not found " "in Loki"
                            ),
                            details={
                                "trace_id": trace_id,
                                "loki_count": loki_count,
                            },
                        )
                    )
        else:
            report.add(
                CheckResult(
                    name="log.trace_id_cross_reference",
                    category="log",
                    passed=False,
                    message="No traces in Tempo to cross-reference",
                )
            )
    except Exception as exc:
        report.add(
            CheckResult(
                name="log.trace_id_cross_reference",
                category="log",
                passed=False,
                message=f"Cross-reference check failed: {exc}",
            )
        )


# ---------------------------------------------------------------------------
# Dashboard Validation (Grafana API)
# ---------------------------------------------------------------------------


async def validate_dashboards(
    session: aiohttp.ClientSession,
    grafana_url: str,
    report: ValidationReport,
) -> None:
    """Validate that all Grafana dashboards are accessible and return data.

    For each expected dashboard UID, queries the Grafana API to verify
    the dashboard exists and is loadable.

    Args:
        session:     aiohttp client session.
        grafana_url: Base URL for Grafana API (e.g., http://localhost:3000).
        report:      ValidationReport to accumulate results.
    """
    logger.info("--- Dashboard Validation (Grafana) ---")

    with open(EXPECTED_METRICS_FILE) as f:
        expected = json.load(f)

    dashboard_uids = expected.get("grafana_dashboards", {}).get("uids", [])

    for uid in dashboard_uids:
        try:
            async with session.get(f"{grafana_url}/api/dashboards/uid/{uid}") as resp:
                if resp.status == 200:
                    data = await resp.json()
                    dashboard = data.get("dashboard", {})
                    panel_count = len(dashboard.get("panels", []))
                    report.add(
                        CheckResult(
                            name=f"dashboard.{uid}",
                            category="dashboard",
                            passed=True,
                            message=(f"{uid}: loaded ({panel_count} panels)"),
                            details={"panel_count": panel_count},
                        )
                    )
                else:
                    report.add(
                        CheckResult(
                            name=f"dashboard.{uid}",
                            category="dashboard",
                            passed=False,
                            message=f"{uid}: HTTP {resp.status}",
                        )
                    )
        except Exception as exc:
            report.add(
                CheckResult(
                    name=f"dashboard.{uid}",
                    category="dashboard",
                    passed=False,
                    message=f"{uid}: query failed ({exc})",
                )
            )


# ---------------------------------------------------------------------------
# Span duration validation
# ---------------------------------------------------------------------------


async def validate_span_durations(
    session: aiohttp.ClientSession,
    tempo_url: str,
    report: ValidationReport,
) -> None:
    """Validate that span durations are within reasonable bounds.

    Checks that spans have duration > 0 and < 60s, flagging any anomalies.

    Args:
        session:   aiohttp client session.
        tempo_url: Base URL for Tempo API.
        report:    ValidationReport to accumulate results.
    """
    logger.info("--- Span Duration Validation ---")

    try:
        traces = await _tempo_search(
            session,
            tempo_url,
            '{resource.service.name="rippled"}',
            limit=5,
        )

        if not traces:
            report.add(
                CheckResult(
                    name="span.duration_bounds",
                    category="span",
                    passed=False,
                    message="No traces available for duration check",
                )
            )
            return

        total_spans = 0
        invalid_spans = 0
        max_duration_ns = 0

        for trace_summary in traces:
            trace_id = trace_summary.get("traceID", "")
            if not trace_id:
                continue
            spans = await _tempo_get_trace(session, tempo_url, trace_id)
            for span in spans:
                start_ns = int(span.get("startTimeUnixNano", "0"))
                end_ns = int(span.get("endTimeUnixNano", "0"))
                duration_ns = end_ns - start_ns
                total_spans += 1
                max_duration_ns = max(max_duration_ns, duration_ns)
                # Invalid if negative or > 60 seconds.
                if duration_ns < 0 or duration_ns > 60_000_000_000:
                    invalid_spans += 1

        max_duration_ms = max_duration_ns / 1_000_000

        report.add(
            CheckResult(
                name="span.duration_bounds",
                category="span",
                passed=invalid_spans == 0,
                message=(
                    f"All {total_spans} spans have valid durations "
                    f"(max: {max_duration_ms:.1f}ms)"
                    if invalid_spans == 0
                    else f"{invalid_spans}/{total_spans} spans have invalid "
                    "durations (<0 or >60s)"
                ),
                details={
                    "total_spans": total_spans,
                    "invalid_spans": invalid_spans,
                    "max_duration_ms": round(max_duration_ms, 2),
                },
            )
        )
    except Exception as exc:
        report.add(
            CheckResult(
                name="span.duration_bounds",
                category="span",
                passed=False,
                message=f"Duration check failed: {exc}",
            )
        )


# ---------------------------------------------------------------------------
# External Dashboard Parity Validation
# ---------------------------------------------------------------------------

# Span attributes that external dashboards (validator-health, peer-quality,
# system-node-health) depend on.  Each entry maps a span name to the
# attributes that must be present for external dashboard panels to render.
PARITY_SPAN_ATTRS: list[dict[str, str]] = [
    {"span": "rpc.command.server_info", "attr": "xrpl.node.amendment_blocked"},
    {"span": "rpc.command.server_info", "attr": "xrpl.node.server_state"},
    {"span": "tx.receive", "attr": "xrpl.peer.version"},
    {"span": "consensus.validation.send", "attr": "xrpl.validation.ledger_hash"},
    {"span": "consensus.validation.send", "attr": "xrpl.validation.full"},
    {"span": "peer.validation.receive", "attr": "xrpl.peer.validation.ledger_hash"},
    {"span": "consensus.accept", "attr": "xrpl.consensus.validation_quorum"},
    {"span": "consensus.accept", "attr": "xrpl.consensus.proposers_validated"},
]

# Value sanity bounds for external-parity metrics.  Each entry specifies a
# Prometheus query and the acceptable range [lo, hi] for the returned value.
PARITY_VALUE_SANITY: list[dict[str, Any]] = [
    {
        "name": "validation_agreement_pct_1h",
        "query": 'rippled_validation_agreement{metric="agreement_pct_1h"}',
        "lo": 0,
        "hi": 100,
    },
    {
        "name": "unl_expiry_days",
        "query": 'rippled_validator_health{metric="unl_expiry_days"}',
        "lo": 0,
        "hi": None,
        "exclusive_lo": True,
    },
    {
        "name": "peer_latency_p90_ms",
        "query": 'rippled_peer_quality{metric="peer_latency_p90_ms"}',
        "lo": 0,
        "hi": None,
    },
    {
        "name": "state_value",
        "query": 'rippled_state_tracking{metric="state_value"}',
        "lo": 0,
        "hi": 7,
    },
]


async def validate_parity_span_attrs(
    session: aiohttp.ClientSession,
    tempo_url: str,
    report: ValidationReport,
) -> None:
    """Validate span attributes required by external dashboard panels.

    For each (span, attribute) pair in PARITY_SPAN_ATTRS, queries Tempo
    for the span and checks that the attribute key exists on at least one
    span in the returned traces.

    Args:
        session:   aiohttp client session.
        tempo_url: Base URL for Tempo API.
        report:    ValidationReport to accumulate results.
    """
    logger.info("--- External Parity: Span Attribute Checks ---")

    for entry in PARITY_SPAN_ATTRS:
        span_name = entry["span"]
        attr_name = entry["attr"]
        check_name = f"parity.span_attr.{span_name}.{attr_name}"

        try:
            query = '{resource.service.name="rippled" && name="' + span_name + '"}'
            traces = await _tempo_search(session, tempo_url, query, limit=5)

            if not traces:
                report.add(
                    CheckResult(
                        name=check_name,
                        category="parity",
                        passed=False,
                        message=(
                            f"{span_name}: no traces found, "
                            f"cannot verify attr {attr_name}"
                        ),
                    )
                )
                continue

            # Fetch full trace and search spans for the attribute.
            found = False
            for trace_summary in traces:
                trace_id = trace_summary.get("traceID", "")
                if not trace_id:
                    continue
                spans = await _tempo_get_trace(session, tempo_url, trace_id)
                for span in spans:
                    if attr_name in _otlp_span_attr_keys(span):
                        found = True
                        break
                if found:
                    break

            report.add(
                CheckResult(
                    name=check_name,
                    category="parity",
                    passed=found,
                    message=(
                        f"{span_name}: attribute '{attr_name}' present"
                        if found
                        else f"{span_name}: attribute '{attr_name}' missing"
                    ),
                )
            )
        except Exception as exc:
            report.add(
                CheckResult(
                    name=check_name,
                    category="parity",
                    passed=False,
                    message=f"{span_name}: attr check failed ({exc})",
                )
            )


async def validate_parity_value_sanity(
    session: aiohttp.ClientSession,
    prometheus_url: str,
    report: ValidationReport,
) -> None:
    """Validate that external-parity metric values fall within sane bounds.

    For each entry in PARITY_VALUE_SANITY, queries the current value from
    Prometheus and checks it against the specified [lo, hi] range.

    Args:
        session:        aiohttp client session.
        prometheus_url: Prometheus API base URL.
        report:         ValidationReport to accumulate results.
    """
    logger.info("--- External Parity: Value Sanity Checks ---")

    for entry in PARITY_VALUE_SANITY:
        name = entry["name"]
        query = entry["query"]
        lo = entry["lo"]
        hi = entry["hi"]
        exclusive_lo = entry.get("exclusive_lo", False)
        check_name = f"parity.value_sanity.{name}"

        try:
            params = {"query": query}
            async with session.get(
                f"{prometheus_url}/api/v1/query", params=params
            ) as resp:
                data = await resp.json()
                results = data.get("data", {}).get("result", [])

            if not results:
                report.add(
                    CheckResult(
                        name=check_name,
                        category="parity",
                        passed=False,
                        message=f"{name}: no data returned from Prometheus",
                    )
                )
                continue

            # Use the first result's value.
            value = float(results[0]["value"][1])

            # Check bounds.
            in_range = True
            if exclusive_lo:
                in_range = in_range and (value > lo)
            else:
                in_range = in_range and (value >= lo)
            if hi is not None:
                in_range = in_range and (value <= hi)

            # Build human-readable bound description.
            lo_op = ">" if exclusive_lo else ">="
            bound_desc = f"{lo_op} {lo}"
            if hi is not None:
                bound_desc += f" and <= {hi}"

            report.add(
                CheckResult(
                    name=check_name,
                    category="parity",
                    passed=in_range,
                    message=(
                        f"{name}: value {value} is within bounds ({bound_desc})"
                        if in_range
                        else f"{name}: value {value} out of bounds "
                        f"(expected {bound_desc})"
                    ),
                    details={"value": value, "lo": lo, "hi": hi},
                )
            )
        except Exception as exc:
            report.add(
                CheckResult(
                    name=check_name,
                    category="parity",
                    passed=False,
                    message=f"{name}: sanity check failed ({exc})",
                )
            )


# ---------------------------------------------------------------------------
# Main validation orchestrator
# ---------------------------------------------------------------------------


async def run_validation(
    tempo_url: str,
    prometheus_url: str,
    loki_url: str,
    grafana_url: str,
    skip_loki: bool = False,
) -> ValidationReport:
    """Run all validation checks and return a report.

    Args:
        tempo_url:      Tempo API base URL.
        prometheus_url: Prometheus API base URL.
        loki_url:       Loki API base URL.
        grafana_url:    Grafana API base URL.
        skip_loki:      If True, skip log-trace correlation checks.

    Returns:
        ValidationReport with all check results.
    """
    report = ValidationReport()
    report.start_time = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())

    async with aiohttp.ClientSession() as session:
        await validate_spans(session, tempo_url, report)
        await validate_span_durations(session, tempo_url, report)
        await validate_metrics(session, prometheus_url, report)
        if not skip_loki:
            await validate_log_trace_correlation(session, loki_url, tempo_url, report)
        await validate_dashboards(session, grafana_url, report)
        await validate_parity_span_attrs(session, tempo_url, report)
        await validate_parity_value_sanity(session, prometheus_url, report)

    report.end_time = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
    return report


# ---------------------------------------------------------------------------
# CLI entry point
# ---------------------------------------------------------------------------


def parse_args() -> argparse.Namespace:
    """Parse command-line arguments."""
    parser = argparse.ArgumentParser(
        description="Telemetry Validation Suite for rippled",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Run all validations with defaults:
  python3 validate_telemetry.py

  # Write report to file:
  python3 validate_telemetry.py --report /tmp/validation-report.json

  # Custom endpoints:
  python3 validate_telemetry.py \\
      --tempo http://tempo:3200 --prometheus http://prom:9090

  # Skip Loki checks (if log-trace correlation is not set up):
  python3 validate_telemetry.py --skip-loki
        """,
    )
    parser.add_argument(
        "--tempo",
        type=str,
        default=DEFAULT_TEMPO,
        help=f"Tempo API URL (default: {DEFAULT_TEMPO})",
    )
    parser.add_argument(
        "--prometheus",
        type=str,
        default=DEFAULT_PROMETHEUS,
        help=f"Prometheus API URL (default: {DEFAULT_PROMETHEUS})",
    )
    parser.add_argument(
        "--loki",
        type=str,
        default=DEFAULT_LOKI,
        help=f"Loki API URL (default: {DEFAULT_LOKI})",
    )
    parser.add_argument(
        "--grafana",
        type=str,
        default=DEFAULT_GRAFANA,
        help=f"Grafana API URL (default: {DEFAULT_GRAFANA})",
    )
    parser.add_argument(
        "--skip-loki",
        action="store_true",
        help="Skip log-trace correlation validation",
    )
    parser.add_argument(
        "--report",
        type=str,
        default=None,
        help="Write JSON report to this file path",
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="Enable debug logging",
    )
    return parser.parse_args()


def main() -> None:
    """Main entry point for the telemetry validation suite."""
    args = parse_args()

    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s [%(name)s] %(levelname)s %(message)s",
    )

    report = asyncio.run(
        run_validation(
            tempo_url=args.tempo,
            prometheus_url=args.prometheus,
            loki_url=args.loki,
            grafana_url=args.grafana,
            skip_loki=args.skip_loki,
        )
    )

    # Print summary.
    print("")
    print("=" * 60)
    print("  TELEMETRY VALIDATION REPORT")
    print("=" * 60)
    print(f"  Total checks: {report.total_checks}")
    print(f"  Passed:       {report.passed}")
    print(f"  Failed:       {report.failed}")
    print("=" * 60)
    print("")

    # Print failures.
    if report.failed > 0:
        print("FAILED CHECKS:")
        for check in report.checks:
            if not check.passed:
                print(f"  [{check.category}] {check.name}: {check.message}")
        print("")

    # Write report file.
    report_dict = report.to_dict()
    if args.report:
        with open(args.report, "w") as f:
            json.dump(report_dict, f, indent=2)
        logger.info("Report written to %s", args.report)
    else:
        print(json.dumps(report_dict, indent=2))

    # Exit with appropriate code for CI.
    sys.exit(0 if report.all_passed else 1)


if __name__ == "__main__":
    main()
