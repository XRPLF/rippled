#!/usr/bin/env python3
"""Tests for validate_telemetry.py's hierarchy check.

Run with plain python3 -- there is no pytest in the harness requirements, and
this file is deliberately runnable with nothing but the standard library plus
the aiohttp that validate_telemetry.py already imports:

    python3 docker/telemetry/workload/test_validate_telemetry.py

Why a stub Tempo rather than the real one: the behaviour under test is which
traces the check ASKS FOR, which a live backend cannot demonstrate -- a passing
query against real data proves the data happened to co-operate, not that the
query was right. The stub records every request, so a test can assert on the
query itself and on the answer the check derives from a known corpus.
"""

import asyncio
import json
import sys
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).parent))

import validate_telemetry as vt  # noqa: E402


class FakeResponse:
    """Minimal stand-in for an aiohttp response used as an async context manager."""

    def __init__(self, payload: dict[str, Any], status: int = 200) -> None:
        self._payload = payload
        self.status = status

    async def __aenter__(self) -> "FakeResponse":
        return self

    async def __aexit__(self, *exc: object) -> bool:
        return False

    async def json(self) -> dict[str, Any]:
        return self._payload

    async def text(self) -> str:
        return json.dumps(self._payload)


class FakeTempo:
    """A Tempo whose corpus is fixed and whose queries are recorded.

    Args:
        traces: Maps a trace id to the list of span names that trace contains,
                ordered newest first, which is the order /api/search returns.
    """

    def __init__(self, traces: dict[str, list[str]]) -> None:
        self.traces = traces
        self.queries: list[tuple[str, int]] = []

    def get(self, url: str, params: dict[str, str] | None = None) -> FakeResponse:
        params = params or {}
        if "/api/search" in url:
            query, limit = params["q"], int(params.get("limit", 20))
            self.queries.append((query, limit))
            matched = [
                tid
                for tid, names in self.traces.items()
                if _query_matches_trace(query, names)
            ]
            return FakeResponse({"traces": [{"traceID": t} for t in matched[:limit]]})
        if "/api/traces/" in url:
            tid = url.rsplit("/", 1)[-1]
            spans = [{"name": n, "attributes": []} for n in self.traces.get(tid, [])]
            return FakeResponse({"batches": [{"scopeSpans": [{"spans": spans}]}]})
        raise AssertionError(f"unexpected request: {url}")


def _query_matches_trace(query: str, names: list[str]) -> bool:
    """Evaluate the subset of TraceQL this suite uses against one trace.

    Supports the trace-level conjunction of name predicates the hierarchy check
    builds: every `name="X"` (or `name=~"X"`) term must be satisfied by some span
    in the trace. That is the whole semantic the check relies on, so the stub
    models exactly it and nothing more.
    """
    import re

    # `name` only as a bare intrinsic. The lookbehind is load-bearing: without it
    # this also matches the resource.service.name="xrpld" term every query
    # carries, and then demands a span literally named "xrpld" -- which made the
    # first run of these tests fail with "No <parent> traces" instead of the
    # sampling failure they exist to demonstrate.
    terms = re.findall(r'(?<![.\w])name\s*(=~|=)\s*"([^"]+)"', query)
    assert terms, f"no name predicate found in query: {query}"
    for op, value in terms:
        if op == "=~":
            if not any(re.fullmatch(value, n) for n in names):
                return False
        elif not any(n == value for n in names):
            return False
    return True


class Report:
    """Collects CheckResults the way ValidationReport does, without the logging."""

    def __init__(self) -> None:
        self.results: list[Any] = []

    def add(self, result: Any) -> None:
        self.results.append(result)


def run(coro: Any) -> Any:
    return asyncio.run(coro)


def test_child_found_in_a_trace_outside_the_newest_three() -> None:
    """The check must find a child that co-occurs only in an older trace.

    This is the txq.accept_tx / ledger.acquire.txtree shape: the parent fires on
    every ledger close, the child only when a rarely-met condition holds, so the
    newest traces carry the parent alone. Sampling the newest N parent traces
    reports "not found" on a corpus that plainly contains the relationship.

    The production change that makes this fail: reverting the hierarchy check to
    search the parent alone and inspect only the first N results.
    """
    tempo = FakeTempo(
        {
            # Newest first, as /api/search returns. The child is only in the oldest.
            "t5": ["txq.accept"],
            "t4": ["txq.accept"],
            "t3": ["txq.accept"],
            "t2": ["txq.accept"],
            "t1": ["txq.accept", "txq.accept_tx"],
        }
    )
    report = Report()
    run(
        vt._validate_parent_child(
            tempo,
            "http://tempo",
            {"parent": "txq.accept", "child": "txq.accept_tx"},
            report,
        )
    )
    assert len(report.results) == 1, report.results
    result = report.results[0]
    assert result.passed, f"expected PASS, got: {result.message}"
    assert result.name == "span.hierarchy.txq.accept->txq.accept_tx"


def test_absent_child_still_fails() -> None:
    """A child that co-occurs in no trace must still fail.

    Guards the obvious way to "fix" the test above -- making the check pass
    whenever the parent exists. Without this, a harness that silently stopped
    emitting a child would go green.
    """
    tempo = FakeTempo({"t2": ["txq.accept"], "t1": ["txq.accept"]})
    report = Report()
    run(
        vt._validate_parent_child(
            tempo,
            "http://tempo",
            {"parent": "txq.accept", "child": "txq.accept_tx"},
            report,
        )
    )
    assert len(report.results) == 1
    assert not report.results[0].passed
    assert "txq.accept_tx" in report.results[0].message


def test_missing_parent_reports_the_parent_not_the_child() -> None:
    """No parent traces at all is a distinct failure from a missing child.

    The two mean different things to whoever reads the report -- a missing parent
    says the span stopped being emitted, a missing child says the relationship
    broke -- so the messages must not collapse into one.
    """
    tempo = FakeTempo({"t1": ["ledger.build"]})
    report = Report()
    run(
        vt._validate_parent_child(
            tempo,
            "http://tempo",
            {"parent": "txq.accept", "child": "txq.accept_tx"},
            report,
        )
    )
    assert len(report.results) == 1
    assert not report.results[0].passed
    assert "No txq.accept traces" in report.results[0].message


def test_wildcard_child_matches_any_family_member() -> None:
    """A wildcard child must be satisfied by any concrete member.

    rpc.command.* names vary per request, so pinning one literal would make the
    check depend on which command the sampled traces happened to carry.
    """
    tempo = FakeTempo({"t1": ["rpc.ws_message", "rpc.command.fee"]})
    report = Report()
    run(
        vt._validate_parent_child(
            tempo,
            "http://tempo",
            {"parent": "rpc.ws_message", "child": "rpc.command.*"},
            report,
        )
    )
    assert report.results[0].passed, report.results[0].message


def test_wildcard_predicate_carries_no_backslash_escape() -> None:
    """The TraceQL predicate must not contain a backslash escape.

    Tempo's string lexer rejects `\\.` outright -- run 33062418036 returned
    HTTP 400, "invalid TraceQL query: parse error at line 1, col 68: invalid char
    escape", on the predicate re.escape produced. Asserting the absence of a
    backslash rather than a specific spelling keeps this test about the property
    the lexer enforces instead of about one way of satisfying it.

    Note the earlier stub could not have caught this: it evaluated the pattern
    with Python's re, which accepts `\\.` happily, so it modelled the regex engine
    rather than the query lexer in front of it.
    """
    predicate = vt._traceql_name_predicate("rpc.command.*")
    assert "\\" not in predicate, f"backslash escape reaches Tempo: {predicate}"


def test_wildcard_predicate_matches_the_family_but_not_near_misses() -> None:
    """The pattern must still mean what the glob meant.

    Dropping the escaping must not be done by making the dots match any
    character: `rpc.command.*` should accept rpc.command.fee and reject a name
    that differs in the separator positions, which is the looseness
    _span_name_matches exists to avoid.
    """
    import re as _re

    pattern = vt._traceql_name_predicate("rpc.command.*").split('"')[1]
    assert _re.fullmatch(pattern, "rpc.command.fee")
    assert _re.fullmatch(pattern, "rpc.command.server_info")
    # Separators deliberately not dots: if the pattern left its dots bare they
    # would match these too. Colons rather than a made-up letter so the spell
    # checker still sees three real words.
    assert not _re.fullmatch(pattern, "rpc:command:fee")
    assert not _re.fullmatch(pattern, "other.command.fee")


def test_literal_predicate_uses_equality() -> None:
    """A non-glob child must use `=`, not a regex.

    Equality is what makes a longer emitted name unable to satisfy a shorter
    contract, the same guarantee _span_name_matches gives on the client side.
    """
    assert vt._traceql_name_predicate("txq.accept_tx") == 'name="txq.accept_tx"'


def main() -> int:
    tests = [v for k, v in sorted(globals().items()) if k.startswith("test_")]
    failed = 0
    for test in tests:
        try:
            test()
        except AssertionError as exc:
            failed += 1
            print(f"FAIL {test.__name__}: {exc}")
        except Exception as exc:  # noqa: BLE001 - report any error as a failure
            failed += 1
            print(f"ERROR {test.__name__}: {type(exc).__name__}: {exc}")
        else:
            print(f"PASS {test.__name__}")
    print(f"\n{len(tests) - failed}/{len(tests)} passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
