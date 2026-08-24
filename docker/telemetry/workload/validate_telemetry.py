#!/usr/bin/env python3
"""Telemetry Validation Suite for xrpld.

Validates that the full telemetry stack is emitting expected data after
a workload run. Queries Tempo (spans), Prometheus (metrics), Loki (logs),
and Grafana (dashboards) APIs to produce a pass/fail report.

Validation categories:
  1. Span validation     — Every required span type in expected_spans.json, each
                           carrying its required attributes
  2. Metric validation   — SpanMetrics, StatsD, and MetricsRegistry OTLP metrics
                           are non-zero, and each group's required_labels reach
                           Prometheus with non-empty values
  3. Log-trace correlation — Loki logs contain trace_id/span_id fields
  4. Dashboard validation — Every dashboard uid in expected_metrics.json
                           provisions and loads (panel count only, not panel data)
  5. External parity     — Span attrs, metric existence, and value sanity for
                           external dashboard parity (validator-health,
                           peer-quality, node-health)

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
import fnmatch
import json
import logging
import re
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import aiohttp

# Loki's default query window is the last hour. A validation run finishes in
# minutes, but bounding the range explicitly keeps the query reproducible when
# someone re-runs it later to investigate a result.
LOG_QUERY_WINDOW_SECONDS = 4 * 60 * 60

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

# Some beast::insight gauges/counters (ledger-age, peer-finder, overlay
# traffic) only populate after the node validates ledgers and sustains peer
# traffic, then travel a 1s periodic OTLP export + a 15s Prometheus scrape
# before they are queryable. On a slow CI runner the fixed post-workload wait
# can end before that pipeline settles, so a single query races and reports 0
# series. Poll each missing metric for up to this long (covering two scrape
# cycles) before failing, so the check is robust to runner speed.
METRIC_POLL_TIMEOUT_SEC = 45.0
METRIC_POLL_INTERVAL_SEC = 5.0

# All metrics are polled concurrently against ONE shared deadline, so the
# metric phase costs a single poll window instead of one per metric. This caps
# how many /api/v1/series requests are in flight at a time, so the fan-out does
# not hammer the single-container Prometheus the harness runs.
METRIC_POLL_CONCURRENCY = 8


# ---------------------------------------------------------------------------
# Data classes
# ---------------------------------------------------------------------------


@dataclass
class CheckResult:
    """Result of a single validation check.

    Attributes:
        name:     Check identifier (e.g., "span.rpc.ws_message").
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


def _log_query_window() -> dict[str, str]:
    """Loki query_range bounds covering a validation run.

    Returns:
        start/end parameters in nanoseconds since the epoch.
    """
    now = time.time()
    return {
        "start": str(int((now - LOG_QUERY_WINDOW_SECONDS) * 1_000_000_000)),
        "end": str(int(now * 1_000_000_000)),
    }


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


def _span_name_matches(emitted_name: str, expected_name: str) -> bool:
    """Test an emitted span name against a name from expected_spans.json.

    Contract names are either literals or globs containing "*" (for example
    "rpc.command.*"). Literals are compared for exact equality so a longer
    emitted name cannot satisfy a shorter contract: "consensus.accept.apply"
    must not stand in for "consensus.accept".

    Args:
        emitted_name:  Span name as reported by Tempo.
        expected_name: Span name or glob pattern from expected_spans.json.

    Returns:
        True when the emitted name satisfies the expected name.
    """
    if "*" in expected_name:
        return fnmatch.fnmatchcase(emitted_name, expected_name)
    return emitted_name == expected_name


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
            has_xrpld = "xrpld" in services
            report.add(
                CheckResult(
                    name="span.service_registration",
                    category="span",
                    passed=has_xrpld,
                    message=(
                        f"Service 'xrpld' registered (found: {services})"
                        if has_xrpld
                        else f"Service 'xrpld' NOT found (found: {services})"
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

    # Diagnostic: list all available operations (span names) for the xrpld
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

    # Concrete probe names for wildcard span entries. Exact-match TraceQL can't
    # match a literal "*", so a representative operation name is substituted.
    # Wildcards without a known concrete example (e.g. grpc.<MethodName> when no
    # gRPC client runs) are skipped when marked optional.
    wildcard_probes = {"rpc.command.*": "rpc.command.server_info"}

    # Check each expected span.
    for span_def in expected["spans"]:
        span_name = span_def["name"]
        is_optional = span_def.get("optional", False)
        check_name = f"span.{span_name}"

        if "*" in span_name:
            operation = wildcard_probes.get(span_name)
            if operation is None:
                # No concrete probe. Optional wildcards (e.g. grpc.*) are skipped;
                # a required one would be a config error worth surfacing.
                if is_optional:
                    logger.info(
                        "[SKIP] %s: optional wildcard span with no concrete "
                        "probe (not exercised by the workload)",
                        check_name,
                    )
                    continue
                report.add(
                    CheckResult(
                        name=check_name,
                        category="span",
                        passed=False,
                        message=f"{span_name}: required wildcard has no probe name",
                    )
                )
                continue
        else:
            operation = span_name

        try:
            query = '{resource.service.name="xrpld" && name="' + operation + '"}'
            traces = await _tempo_search(session, tempo_url, query, limit=5)
            count = len(traces)
            # Optional spans only fire under specific traffic (mode changes,
            # missing-ledger fetch, fee escalation). Absence is not a failure —
            # mirror the parent-child "skip" handling so CI stays green.
            if count == 0 and is_optional:
                logger.info(
                    "[SKIP] %s: optional span not emitted under this workload",
                    check_name,
                )
                report.add(
                    CheckResult(
                        name=check_name,
                        category="span",
                        passed=True,
                        message=f"{span_name}: optional, not emitted (skipped)",
                        details={"trace_count": 0, "optional": True},
                    )
                )
                continue
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
                await _check_attributes_on_first_trace(
                    session, tempo_url, traces, span_def, report
                )
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


async def _check_attributes_on_first_trace(
    session: aiohttp.ClientSession,
    tempo_url: str,
    traces: list[dict[str, Any]],
    span_def: dict[str, Any],
    report: ValidationReport,
) -> None:
    """Fetch the first trace and check the span's required attributes.

    Fetching the trace is a second network call, so it carries its own error
    handling. Letting it fall through to the caller's handler would add a
    second result under the span's own check name, which has already recorded
    the trace as found -- one entry passing and one failing for the same name,
    inflating the check total and blaming the trace-existence check for an
    attribute-fetch failure.

    Args:
        session:    aiohttp client session.
        tempo_url:  Base URL for the Tempo API.
        traces:     Traces returned for this span, most recent first.
        span_def:   The span's entry from expected_spans.json.
        report:     ValidationReport to accumulate results.
    """
    span_name = span_def["name"]
    try:
        trace_id = traces[0].get("traceID", "")
        if not trace_id:
            return
        spans = await _tempo_get_trace(session, tempo_url, trace_id)
        await _validate_span_attributes_otlp(spans, span_def, report)
    except Exception as exc:
        report.add(
            CheckResult(
                name=f"span.attrs.{span_name}",
                category="span",
                passed=False,
                message=f"{span_name}: attribute check failed ({exc})",
            )
        )


async def _validate_span_attributes_otlp(
    spans: list[dict[str, Any]],
    span_def: dict[str, Any],
    report: ValidationReport,
) -> None:
    """Check that the contract's own span carries its required attributes.

    Only spans whose name matches ``span_def["name"]`` are inspected.
    Attributes are never borrowed from siblings: many span types share keys
    such as ledger_seq or tx_hash, so a trace-wide scan would satisfy every
    one of those contracts from a single carrier span and make the per-span
    contract unenforceable.

    A span type passes when at least one instance of it carries every required
    attribute. When none does, the closest instance's missing keys are
    reported.

    Args:
        spans:    Every OTLP span dict in the fetched trace.
        span_def: Span definition from expected_spans.json.
        report:   ValidationReport to accumulate results.
    """
    required_attrs = span_def.get("required_attributes", [])
    if not required_attrs:
        return

    span_name = span_def["name"]
    check_name = f"span.attrs.{span_name}"
    matching = [s for s in spans if _span_name_matches(s.get("name", ""), span_name)]

    if not matching:
        report.add(
            CheckResult(
                name=check_name,
                category="span",
                passed=False,
                message=(
                    f"{span_name}: no span named '{span_name}' in the fetched "
                    "trace, cannot verify its attributes"
                ),
                details={"required": required_attrs, "instances": 0},
            )
        )
        return

    # Keep the instance that is missing the fewest required attributes, so the
    # failure message names the closest witness rather than an arbitrary one.
    best_found: set[str] = set()
    best_missing: list[str] = list(required_attrs)
    for span in matching:
        found = _otlp_span_attr_keys(span)
        missing = [a for a in required_attrs if a not in found]
        if len(missing) < len(best_missing):
            best_found, best_missing = found, missing
        if not best_missing:
            break

    report.add(
        CheckResult(
            name=check_name,
            category="span",
            passed=not best_missing,
            message=(
                f"{span_name}: all {len(required_attrs)} attributes present"
                if not best_missing
                else f"{span_name}: no '{span_name}' span carried all "
                f"{len(required_attrs)} required attributes; closest of "
                f"{len(matching)} instance(s) missing {best_missing}"
            ),
            details={
                "required": required_attrs,
                "found": sorted(best_found),
                "missing": best_missing,
                "instances": len(matching),
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
        query = '{resource.service.name="xrpld" && name="' + parent_name + '"}'
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

        # Check if child spans exist within parent traces. Names are matched
        # exactly (globs for wildcard contracts) — a substring test let a
        # longer emitted name satisfy a shorter contract, so
        # consensus.round -> consensus.accept passed on a
        # consensus.accept.apply span alone.
        found_child = False
        for trace_summary in traces:
            trace_id = trace_summary.get("traceID", "")
            if not trace_id:
                continue
            spans = await _tempo_get_trace(session, tempo_url, trace_id)
            if any(
                _span_name_matches(span.get("name", ""), child_name) for span in spans
            ):
                found_child = True
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


async def _log_prometheus_metric_names(
    session: aiohttp.ClientSession, prometheus_url: str
) -> None:
    """Log the harness-relevant metric names Prometheus currently knows.

    Diagnostic only — this output appears in CI logs and helps debug name
    mismatches between expected_metrics.json and actual emissions. Failures
    are warnings, never check failures.

    Args:
        session:        aiohttp client session.
        prometheus_url: Prometheus base URL.
    """
    try:
        async with session.get(
            f"{prometheus_url}/api/v1/label/__name__/values"
        ) as resp:
            label_data = await resp.json()
            all_metrics = label_data.get("data", [])
            relevant = [
                m
                for m in all_metrics
                if m.startswith(
                    (
                        "span_",
                        "rpc_method",
                        "cache_",
                        "txq_",
                        "object_count",
                        "load_factor",
                        "nodestore",
                        "ledgermaster",
                        "peer_finder",
                        "jobq_",
                        "total_bytes",
                        "total_messages",
                        "validation_agreement",
                        "validator_health",
                        "peer_quality",
                        "ledger_economy",
                        "state_tracking",
                        "storage_detail",
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


def _metric_check_targets(
    expected: dict[str, Any],
) -> tuple[list[tuple[str, str]], list[tuple[str, str, list[str]]]]:
    """Flatten expected_metrics.json into the two lists of check targets.

    Args:
        expected: The parsed expected_metrics.json contract.

    Returns:
        A (metric targets, label targets) pair. Metric targets are
        (group, metric selector) for every name under a group's "metrics".
        Label targets are (group, label, that group's metric selectors) for
        every name under a group's "required_labels" — read for every group
        that declares it, not just spanmetrics. Nothing read the key at all
        until this was added, so the four labels the spanmetrics group
        documented as required had never actually been checked.
    """
    groups = [
        (category_key, category_data)
        for category_key, category_data in expected.items()
        if category_key not in ("description", "grafana_dashboards")
    ]
    targets = [
        (category_key, metric_name)
        for category_key, category_data in groups
        for metric_name in category_data.get("metrics", [])
    ]
    label_targets = [
        (category_key, label, category_data.get("metrics", []))
        for category_key, category_data in groups
        for label in category_data.get("required_labels", [])
    ]
    return targets, label_targets


async def validate_metrics(
    session: aiohttp.ClientSession,
    prometheus_url: str,
    report: ValidationReport,
) -> None:
    """Validate that expected metrics appear in Prometheus with non-zero values.

    Two kinds of check come out of expected_metrics.json: every name under a
    group's "metrics" must have at least one series, and every label under a
    group's "required_labels" must reach at least one of that group's series
    with a non-empty value.

    Args:
        session:        aiohttp client session.
        prometheus_url: Base URL for Prometheus API (e.g., http://localhost:9090).
        report:         ValidationReport to accumulate results.
    """
    logger.info("--- Metric Validation (Prometheus) ---")

    await _log_prometheus_metric_names(session, prometheus_url)

    with open(EXPECTED_METRICS_FILE) as f:
        expected = json.load(f)

    # Flatten the contract, then poll every target concurrently against ONE
    # shared deadline. Polling them serially made each metric own its own
    # timeout, so the waits were additive: 58 metrics x 45 s = 43.5 min, which
    # overran the CI job budget and lost the artifact-upload and summary
    # diagnostics. Sharing the deadline bounds the whole phase to a single
    # poll window.
    targets, label_targets = _metric_check_targets(expected)

    deadline = time.monotonic() + METRIC_POLL_TIMEOUT_SEC
    sem = asyncio.Semaphore(METRIC_POLL_CONCURRENCY)
    # Both kinds of check share the one deadline and the one concurrency bound,
    # so the label checks cost no extra poll window and add no extra load.
    metric_checks, label_checks = await asyncio.gather(
        asyncio.gather(
            *(
                _check_prometheus_metric(
                    session, prometheus_url, metric_name, category, deadline, sem
                )
                for category, metric_name in targets
            )
        ),
        asyncio.gather(
            *(
                _check_metric_label(
                    session,
                    prometheus_url,
                    category,
                    label,
                    metric_selectors,
                    deadline,
                    sem,
                )
                for category, label, metric_selectors in label_targets
            )
        ),
    )

    # Add in contract order, not completion order, so the report and its log
    # lines stay deterministic across runs. Existence checks keep their place
    # ahead of the label checks, so no existing check's position moves.
    for check in [*metric_checks, *label_checks]:
        report.add(check)


def _selector_with_label(metric_selector: str, label: str) -> str:
    """Add a "label is present and non-empty" matcher to a metric selector.

    ``<label>!=""`` is the only matcher that expresses the requirement. An
    absent Prometheus label is indistinguishable from an empty-string one, so
    ``<label>=~".*"`` matches series that never carried the label at all and a
    check written that way would pass on a node that lost the attribute
    entirely. ``!=""`` rejects both the absent and the blank case.

    Selectors in expected_metrics.json are usually bare names, but some already
    carry a matcher (``ledger_economy{metric="base_fee_xrp"}``), so the matcher
    is merged into an existing brace group rather than appended after it.

    Args:
        metric_selector: A metric name, optionally with a brace matcher group.
        label:           Prometheus label name that must be present, non-empty.

    Returns:
        The selector with the label matcher merged in.
    """
    matcher = label + '!=""'
    if metric_selector.endswith("}"):
        head = metric_selector[:-1].rstrip()
        separator = "" if head.endswith("{") else ", "
        return head + separator + matcher + "}"
    return metric_selector + "{" + matcher + "}"


async def _poll_series_count(
    session: aiohttp.ClientSession,
    prometheus_url: str,
    selectors: list[str],
    deadline: float,
    sem: asyncio.Semaphore,
) -> int:
    """Poll Prometheus until a selector has series or the deadline passes.

    Uses the /api/v1/series endpoint instead of an instant query.
    Beast::insight StatsD gauges only mark dirty on value *changes*, so a gauge
    that stabilizes (e.g. peer count stays at 1) may go stale in Prometheus and
    disappear from instant queries.  The series endpoint returns any metric
    that existed in the window, regardless of staleness.

    Polls rather than querying once: late-populating gauges/counters may not
    have completed the export+scrape pipeline when this runs, so a single query
    races. A metric that never appears still fails once the deadline passes.

    Args:
        session:        aiohttp client session.
        prometheus_url: Prometheus base URL.
        selectors:      One or more Prometheus selectors. /api/v1/series takes
                        repeated match[] parameters and unions their results,
                        so several selectors answer "does ANY of these have a
                        series" in a single request.
        deadline:       Monotonic deadline shared by every metric in the run.
        sem:            Bounds how many requests reach Prometheus at once. It
                        is held only across the request, never across the
                        sleep, so one absent metric cannot starve the others.

    Returns:
        Number of series found, or 0 if nothing ever appeared.
    """
    params: list[tuple[str, str]] = [("match[]", s) for s in selectors]
    while True:
        async with sem:
            async with session.get(
                f"{prometheus_url}/api/v1/series", params=params
            ) as resp:
                data = await resp.json()
                series_count = len(data.get("data", []))
        if series_count > 0 or time.monotonic() >= deadline:
            return series_count
        # Never sleep past the shared deadline.
        await asyncio.sleep(min(METRIC_POLL_INTERVAL_SEC, deadline - time.monotonic()))


async def _check_prometheus_metric(
    session: aiohttp.ClientSession,
    prometheus_url: str,
    metric_name: str,
    category: str,
    deadline: float,
    sem: asyncio.Semaphore,
) -> CheckResult:
    """Query Prometheus for a specific metric and check it exists.

    Args:
        session:        aiohttp client session.
        prometheus_url: Prometheus base URL.
        metric_name:    Prometheus metric name.
        category:       Metric category for the report.
        deadline:       Monotonic deadline shared by every metric in the run.
        sem:            Bounds how many requests reach Prometheus at once.

    Returns:
        The CheckResult for this metric. The caller adds it to the report so
        report order follows the contract file rather than completion order.
    """
    try:
        series_count = await _poll_series_count(
            session, prometheus_url, [metric_name], deadline, sem
        )
        return CheckResult(
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
    except Exception as exc:
        return CheckResult(
            name=f"metric.{category}.{metric_name}",
            category="metric",
            passed=False,
            message=f"{metric_name}: query failed ({exc})",
        )


async def _check_metric_label(
    session: aiohttp.ClientSession,
    prometheus_url: str,
    category: str,
    label: str,
    metric_selectors: list[str],
    deadline: float,
    sem: asyncio.Semaphore,
) -> CheckResult:
    """Check that a group's required label reaches Prometheus non-empty.

    A "required_labels" entry names a label that at least one series of that
    group must carry with a non-empty value. It is checked per group rather
    than per metric because the requirement is about the label reaching
    Prometheus at all, and one series carrying it proves the pipeline works.

    The failure this guards against is a label going missing while every metric
    in the group still exists, so the existence checks stay green. xrpl_node_id
    is the case that motivated it: losing it makes Grafana Cloud trace ingest
    fold distinct nodes into one.

    Args:
        session:          aiohttp client session.
        prometheus_url:   Prometheus base URL.
        category:         Group key from expected_metrics.json.
        label:            Label name that must be present and non-empty.
        metric_selectors: The group's asserted metric selectors.
        deadline:         Monotonic deadline shared by every metric in the run.
        sem:              Bounds how many requests reach Prometheus at once.

    Returns:
        The CheckResult for this (group, label) pair. The caller adds it to the
        report so report order follows the contract file.
    """
    check_name = f"metric.{category}.label.{label}"

    if not metric_selectors:
        # required_labels on a group with no asserted metrics would be a check
        # that silently never runs — the exact failure mode it exists to catch.
        return CheckResult(
            name=check_name,
            category="metric",
            passed=False,
            message=(
                f"{label}: group '{category}' declares required_labels but no "
                "metrics, so there is nothing to check the label against"
            ),
        )

    selectors = [_selector_with_label(m, label) for m in metric_selectors]
    try:
        series_count = await _poll_series_count(
            session, prometheus_url, selectors, deadline, sem
        )
        return CheckResult(
            name=check_name,
            category="metric",
            passed=series_count > 0,
            message=(
                f"{label}: present and non-empty on {series_count} "
                f"{category} series"
                if series_count > 0
                else f"{label}: no {category} series carries it with a "
                "non-empty value (an absent label and an empty one are the "
                "same thing in Prometheus, so this covers both). If this "
                "group's own metric checks also failed, the metrics are "
                "missing rather than the label."
            ),
            details={"series_count": series_count, "selectors": selectors},
        )
    except Exception as exc:
        return CheckResult(
            name=check_name,
            category="metric",
            passed=False,
            message=f"{label}: query failed ({exc})",
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
            # Loki's OTLP ingestion promotes service.name to the label
            # `service_name`. A `job` attribute is structured metadata, which a
            # stream selector cannot match — see otel-collector-config.yaml.
            "query": '{service_name="xrpld"} |= "trace_id="',
            "limit": 5,
            "direction": "backward",
            **_log_query_window(),
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

    # Check 2: Cross-reference a trace_id from a log line back to Tempo.
    #
    # Driven from the log side on purpose. A trace_id only reaches a log line
    # when that line is emitted inside a sampled span, and at `warning` level
    # most spans produce no log output at all — so picking an arbitrary trace
    # from Tempo and expecting it in Loki fails even when correlation works.
    # Starting from a logged trace_id tests the invariant that matters: an id
    # written to a log must resolve to a trace that was actually exported.
    try:
        loki_params = {
            "query": '{service_name="xrpld"} |= "trace_id="',
            "limit": 5,
            "direction": "backward",
            **_log_query_window(),
        }
        async with session.get(
            f"{loki_url}/loki/api/v1/query_range", params=loki_params
        ) as resp:
            data = await resp.json()
            streams = data.get("data", {}).get("result", [])

        logged_ids = [
            match.group(1)
            for stream in streams
            for _, line in stream.get("values", [])
            if (match := re.search(r"trace_id=([0-9a-f]{32})", line))
        ]

        if not logged_ids:
            report.add(
                CheckResult(
                    name="log.trace_id_cross_reference",
                    category="log",
                    passed=False,
                    message=(
                        "No logged trace_id to cross-reference. Log lines carry one only "
                        "when emitted inside a sampled span; raise the log level or widen "
                        "the workload if this persists."
                    ),
                )
            )
        else:
            # Try every id found, not just the first: one unexported trace
            # should not fail the check while correlation demonstrably works.
            resolved: str | None = None
            span_count = 0
            unique_ids = list(dict.fromkeys(logged_ids))
            for candidate in unique_ids:
                try:
                    spans = await _tempo_get_trace(session, tempo_url, candidate)
                except Exception:  # noqa: BLE001 - a 404 is "not found", not an error
                    continue
                if spans:
                    resolved, span_count = candidate, len(spans)
                    break

            report.add(
                CheckResult(
                    name="log.trace_id_cross_reference",
                    category="log",
                    passed=resolved is not None,
                    message=(
                        f"logged trace_id {resolved[:16]}... resolves to "
                        f"{span_count} spans in Tempo"
                        if resolved
                        else f"none of {len(unique_ids)} logged trace_id(s) resolve in "
                        "Tempo; the spans they name were not exported"
                    ),
                    details={
                        "trace_id": resolved,
                        "span_count": span_count,
                        "candidates": len(unique_ids),
                    },
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


def _leaf_panel_count(dashboard: dict[str, Any]) -> int:
    """Count the panels a dashboard actually renders.

    Grafana models a row as an entry of ``type: "row"`` in the top-level
    ``panels`` list, and a collapsed row carries its children in its own
    nested ``panels`` list. So ``len(dashboard["panels"])`` counts rows as
    though they were panels and misses everything inside a collapsed one --
    on ``node-health`` that reads 55 where the true figure is 51, and a
    dashboard consisting only of collapsed rows would report a positive
    count while rendering nothing.

    Args:
        dashboard: The ``dashboard`` object from the Grafana API response.

    Returns:
        The number of non-row panels, including those nested inside rows.
    """
    total = 0
    for panel in dashboard.get("panels", []):
        if panel.get("type") == "row":
            total += len(panel.get("panels", []))
        else:
            total += 1
    return total


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
                    panel_count = _leaf_panel_count(dashboard)
                    report.add(
                        CheckResult(
                            name=f"dashboard.{uid}",
                            category="dashboard",
                            passed=panel_count > 0,
                            message=(
                                f"{uid}: loaded ({panel_count} panels)"
                                if panel_count
                                else f"{uid}: loaded but renders no panels"
                            ),
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
            '{resource.service.name="xrpld"}',
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
# node-health) depend on.  Each entry maps a span name to the
# attributes that must be present for external dashboard panels to render.
# Keys follow the 2026-05-13 span-attr naming redesign (bare/underscore form;
# dotted xrpl.* reserved for resource attributes). The amendment_blocked,
# server_state, and proposers_validated values that earlier external-dashboard
# work tracked are NOT span attributes — they exist only as MetricsRegistry
# metrics (validator_health{metric="amendment_blocked"},
# state_tracking{metric="state_value"}, etc.), so they are validated by
# PARITY_VALUE_SANITY below rather than as span attributes here.
PARITY_SPAN_ATTRS: list[dict[str, str]] = [
    {"span": "tx.receive", "attr": "peer_version"},
    {"span": "consensus.validation.send", "attr": "ledger_hash"},
    {"span": "consensus.validation.send", "attr": "full_validation"},
    # peer.validation.receive shares the ledger_hash / full_validation keys with
    # consensus.validation.send (same keys, told apart by span name).
    {"span": "peer.validation.receive", "attr": "ledger_hash"},
    {"span": "peer.validation.receive", "attr": "full_validation"},
    {"span": "consensus.accept", "attr": "quorum"},
]

# Value sanity bounds for external-parity metrics.  Each entry specifies a
# Prometheus query and the acceptable range [lo, hi] for the returned value.
PARITY_VALUE_SANITY: list[dict[str, Any]] = [
    {
        "name": "validation_agreement_pct_1h",
        "query": 'validation_agreement{metric="agreement_pct_1h"}',
        "lo": 0,
        "hi": 100,
    },
    {
        "name": "unl_expiry_days",
        "query": 'validator_health{metric="unl_expiry_days"}',
        "lo": 0,
        "hi": None,
        "exclusive_lo": True,
    },
    {
        "name": "peer_latency_p90_ms",
        "query": 'peer_quality{metric="peer_latency_p90_ms"}',
        "lo": 0,
        "hi": None,
    },
    {
        "name": "state_value",
        "query": 'state_tracking{metric="state_value"}',
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
            query = '{resource.service.name="xrpld" && name="' + span_name + '"}'
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


def _series_label(series: dict[str, Any]) -> str:
    """Name a Prometheus series for use in a failure message.

    Args:
        series: One entry from a Prometheus query result.

    Returns:
        The series' service_instance_id when it carries one (the label that
        tells harness cluster nodes apart), else its full label set.
    """
    metric = series.get("metric", {})
    instance = metric.get("service_instance_id")
    if instance:
        return f"service_instance_id={instance}"
    return str(metric) if metric else "<unlabelled series>"


def _value_in_bounds(
    value: float, lo: float, hi: float | None, exclusive_lo: bool
) -> bool:
    """Test one sample against a sanity range.

    Args:
        value:        Sample value.
        lo:           Lower bound.
        hi:           Upper bound, or None when unbounded above.
        exclusive_lo: True when the lower bound is exclusive.

    Returns:
        True when the value is inside the range.
    """
    lo_ok = value > lo if exclusive_lo else value >= lo
    return lo_ok and (hi is None or value <= hi)


def _bounds_description(lo: float, hi: float | None, exclusive_lo: bool) -> str:
    """Build the human-readable bound text used in check messages.

    Args:
        lo:           Lower bound.
        hi:           Upper bound, or None when unbounded above.
        exclusive_lo: True when the lower bound is exclusive.

    Returns:
        A phrase such as "> 0 and <= 100".
    """
    desc = f"{'>' if exclusive_lo else '>='} {lo}"
    if hi is not None:
        desc += f" and <= {hi}"
    return desc


async def _check_parity_value(
    session: aiohttp.ClientSession,
    prometheus_url: str,
    entry: dict[str, Any],
) -> CheckResult:
    """Bounds-check every series returned by one parity sanity query.

    Args:
        session:        aiohttp client session.
        prometheus_url: Prometheus API base URL.
        entry:          One PARITY_VALUE_SANITY entry.

    Returns:
        A CheckResult that fails if any series is out of bounds, naming each
        offending series.
    """
    name = entry["name"]
    lo = entry["lo"]
    hi = entry["hi"]
    exclusive_lo = entry.get("exclusive_lo", False)
    check_name = f"parity.value_sanity.{name}"

    try:
        async with session.get(
            f"{prometheus_url}/api/v1/query", params={"query": entry["query"]}
        ) as resp:
            data = await resp.json()
            results = data.get("data", {}).get("result", [])

        if not results:
            return CheckResult(
                name=check_name,
                category="parity",
                passed=False,
                message=f"{name}: no data returned from Prometheus",
            )

        values: list[float] = []
        offenders: list[str] = []
        for series in results:
            value = float(series["value"][1])
            values.append(value)
            if not _value_in_bounds(value, lo, hi, exclusive_lo):
                offenders.append(f"{_series_label(series)} value {value}")

        bound_desc = _bounds_description(lo, hi, exclusive_lo)
        return CheckResult(
            name=check_name,
            category="parity",
            passed=not offenders,
            message=(
                f"{name}: all {len(values)} series within bounds ({bound_desc})"
                if not offenders
                else f"{name}: {len(offenders)} of {len(values)} series out of "
                f"bounds (expected {bound_desc}): " + "; ".join(offenders)
            ),
            details={
                "values": values,
                "series_count": len(values),
                "out_of_bounds": offenders,
                "lo": lo,
                "hi": hi,
            },
        )
    except Exception as exc:
        return CheckResult(
            name=check_name,
            category="parity",
            passed=False,
            message=f"{name}: sanity check failed ({exc})",
        )


async def validate_parity_value_sanity(
    session: aiohttp.ClientSession,
    prometheus_url: str,
    report: ValidationReport,
) -> None:
    """Validate that external-parity metric values fall within sane bounds.

    For each entry in PARITY_VALUE_SANITY, queries Prometheus and checks
    *every* returned series against the specified [lo, hi] range. These
    queries are bare selectors with no aggregation, so a multi-node harness
    cluster returns one series per service_instance_id; checking only the
    first would let an out-of-range node pass silently.

    Args:
        session:        aiohttp client session.
        prometheus_url: Prometheus API base URL.
        report:         ValidationReport to accumulate results.
    """
    logger.info("--- External Parity: Value Sanity Checks ---")

    for entry in PARITY_VALUE_SANITY:
        report.add(await _check_parity_value(session, prometheus_url, entry))


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
        description="Telemetry Validation Suite for xrpld",
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
