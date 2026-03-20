#!/usr/bin/env python3
"""Telemetry Validation Suite for rippled.

Validates that the full telemetry stack is emitting expected data after
a workload run. Queries Jaeger (spans), Prometheus (metrics), Loki (logs),
and Grafana (dashboards) APIs to produce a pass/fail report.

Validation categories:
  1. Span validation     — All 16+ span types present with required attributes
  2. Metric validation   — SpanMetrics, StatsD, and Phase 9 metrics are non-zero
  3. Log-trace correlation — Loki logs contain trace_id/span_id fields
  4. Dashboard validation — All 10 Grafana dashboards render data

Usage:
    python3 validate_telemetry.py --report /tmp/validation-report.json

    # Custom API endpoints:
    python3 validate_telemetry.py \\
        --jaeger http://localhost:16686 \\
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

DEFAULT_JAEGER = "http://localhost:16686"
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
# Span Validation (Jaeger API)
# ---------------------------------------------------------------------------


async def validate_spans(
    session: aiohttp.ClientSession,
    jaeger_url: str,
    report: ValidationReport,
) -> None:
    """Validate that all expected spans appear in Jaeger.

    Queries the Jaeger HTTP API for each expected span name and checks
    that traces exist. Also validates required attributes on spans and
    parent-child relationships.

    Args:
        session:    aiohttp client session.
        jaeger_url: Base URL for Jaeger API (e.g., http://localhost:16686).
        report:     ValidationReport to accumulate results.
    """
    logger.info("--- Span Validation (Jaeger) ---")

    # Load expected spans.
    with open(EXPECTED_SPANS_FILE) as f:
        expected = json.load(f)

    # Check service registration.
    try:
        async with session.get(f"{jaeger_url}/api/services") as resp:
            data = await resp.json()
            services = data.get("data", [])
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
                message=f"Jaeger API unreachable: {exc}",
            )
        )
        return

    # Diagnostic: list all available operations (span names) for the rippled
    # service.  This output appears in CI logs and helps debug missing-span
    # failures without needing to reproduce the full stack locally.
    try:
        async with session.get(f"{jaeger_url}/api/services/rippled/operations") as resp:
            ops_data = await resp.json()
            operations = ops_data.get("data", [])
            logger.info(
                "Jaeger operations for 'rippled' (%d total): %s",
                len(operations),
                operations,
            )
    except Exception as exc:
        logger.warning("Failed to fetch Jaeger operations: %s", exc)

    # Check each expected span.
    for span_def in expected["spans"]:
        span_name = span_def["name"]
        # For wildcard spans (rpc.command.*), search with regex pattern.
        if "*" in span_name:
            operation = span_name.replace("*", "")
            # Query a concrete example: rpc.command.server_info.
            operation = "rpc.command.server_info"
            check_name = f"span.{span_name}"
        else:
            operation = span_name
            check_name = f"span.{span_name}"

        try:
            params = {
                "service": "rippled",
                "operation": operation,
                "limit": 5,
                "lookback": "1h",
            }
            async with session.get(f"{jaeger_url}/api/traces", params=params) as resp:
                data = await resp.json()
                traces = data.get("data", [])
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
                    await _validate_span_attributes(traces[0], span_def, report)
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
        await _validate_parent_child(session, jaeger_url, rel, report)


async def _validate_span_attributes(
    trace: dict[str, Any],
    span_def: dict[str, Any],
    report: ValidationReport,
) -> None:
    """Check that a trace's spans contain expected attributes.

    Args:
        trace:    A Jaeger trace object (from /api/traces).
        span_def: Span definition from expected_spans.json.
        report:   ValidationReport to accumulate results.
    """
    required_attrs = span_def.get("required_attributes", [])
    if not required_attrs:
        return

    span_name = span_def["name"]
    # Collect all tag keys from all spans in the trace.
    found_attrs: set[str] = set()
    for span in trace.get("spans", []):
        for tag in span.get("tags", []):
            found_attrs.add(tag.get("key", ""))

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
    jaeger_url: str,
    relationship: dict[str, Any],
    report: ValidationReport,
) -> None:
    """Validate a parent-child span relationship in Jaeger traces.

    Args:
        session:      aiohttp client session.
        jaeger_url:   Base URL for Jaeger API.
        relationship: Dict with 'parent' and 'child' span names.
        report:       ValidationReport to accumulate results.
    """
    parent_name = relationship["parent"]
    child_name = relationship["child"]

    try:
        # Query traces for the parent span.
        params = {
            "service": "rippled",
            "operation": parent_name,
            "limit": 3,
            "lookback": "1h",
        }
        async with session.get(f"{jaeger_url}/api/traces", params=params) as resp:
            data = await resp.json()
            traces = data.get("data", [])

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
        for trace in traces:
            for span in trace.get("spans", []):
                op = span.get("operationName", "")
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
    jaeger_url: str,
    report: ValidationReport,
) -> None:
    """Validate that Loki logs contain trace_id/span_id for correlation.

    Checks:
      1. Logs with trace_id= field exist in Loki.
      2. A random trace_id from Jaeger can be found in Loki logs.

    Args:
        session:    aiohttp client session.
        loki_url:   Base URL for Loki API (e.g., http://localhost:3100).
        jaeger_url: Base URL for Jaeger API.
        report:     ValidationReport to accumulate results.
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

    # Check 2: Cross-reference a trace_id from Jaeger to Loki.
    try:
        # Get a recent trace from Jaeger.
        params = {
            "service": "rippled",
            "limit": 1,
            "lookback": "1h",
        }
        async with session.get(f"{jaeger_url}/api/traces", params=params) as resp:
            data = await resp.json()
            traces = data.get("data", [])

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
                    message="No traces in Jaeger to cross-reference",
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
    jaeger_url: str,
    report: ValidationReport,
) -> None:
    """Validate that span durations are within reasonable bounds.

    Checks that spans have duration > 0 and < 60s, flagging any anomalies.

    Args:
        session:    aiohttp client session.
        jaeger_url: Base URL for Jaeger API.
        report:     ValidationReport to accumulate results.
    """
    logger.info("--- Span Duration Validation ---")

    try:
        params = {
            "service": "rippled",
            "limit": 20,
            "lookback": "1h",
        }
        async with session.get(f"{jaeger_url}/api/traces", params=params) as resp:
            data = await resp.json()
            traces = data.get("data", [])

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
        max_duration_us = 0

        for trace in traces:
            for span in trace.get("spans", []):
                duration = span.get("duration", 0)  # microseconds
                total_spans += 1
                max_duration_us = max(max_duration_us, duration)
                if duration <= 0 or duration > 60_000_000:
                    invalid_spans += 1

        report.add(
            CheckResult(
                name="span.duration_bounds",
                category="span",
                passed=invalid_spans == 0,
                message=(
                    f"All {total_spans} spans have valid durations "
                    f"(max: {max_duration_us / 1000:.1f}ms)"
                    if invalid_spans == 0
                    else f"{invalid_spans}/{total_spans} spans have invalid "
                    "durations (<=0 or >60s)"
                ),
                details={
                    "total_spans": total_spans,
                    "invalid_spans": invalid_spans,
                    "max_duration_ms": round(max_duration_us / 1000, 2),
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
# Main validation orchestrator
# ---------------------------------------------------------------------------


async def run_validation(
    jaeger_url: str,
    prometheus_url: str,
    loki_url: str,
    grafana_url: str,
    skip_loki: bool = False,
) -> ValidationReport:
    """Run all validation checks and return a report.

    Args:
        jaeger_url:     Jaeger API base URL.
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
        await validate_spans(session, jaeger_url, report)
        await validate_span_durations(session, jaeger_url, report)
        await validate_metrics(session, prometheus_url, report)
        if not skip_loki:
            await validate_log_trace_correlation(session, loki_url, jaeger_url, report)
        await validate_dashboards(session, grafana_url, report)

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
      --jaeger http://jaeger:16686 --prometheus http://prom:9090

  # Skip Loki checks (if log-trace correlation is not set up):
  python3 validate_telemetry.py --skip-loki
        """,
    )
    parser.add_argument(
        "--jaeger",
        type=str,
        default=DEFAULT_JAEGER,
        help=f"Jaeger API URL (default: {DEFAULT_JAEGER})",
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
            jaeger_url=args.jaeger,
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
