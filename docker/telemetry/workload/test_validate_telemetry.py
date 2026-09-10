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
        traces: Maps a trace id to the spans that trace contains, ordered newest
                first, which is the order /api/search returns. Each entry is
                either a bare span name (a root span, no parent) or a
                ``(name, parent_name)`` pair. A parent_name that no span in the
                trace carries yields a parentSpanId pointing at a span the trace
                does not hold, which is how a dangling chain is expressed.

    Span ids are generated as ``<trace id>-<index>``. Their spelling does not
    matter: the code under test compares parentSpanId to spanId as opaque
    strings, exactly because Tempo's own encoding of those fields (hex or
    base64) is not something the validator should depend on.
    """

    def __init__(self, traces: dict[str, list[Any]]) -> None:
        self.traces = traces
        self.queries: list[tuple[str, int]] = []

    def _spans_for(self, tid: str) -> list[dict[str, Any]]:
        """Build the OTLP span dicts for one trace, resolving parents by name."""
        entries = self.traces.get(tid, [])
        names = [_entry_name(e) for e in entries]
        ids = [f"{tid}-{i}" for i in range(len(entries))]
        spans = []
        for i, entry in enumerate(entries):
            span: dict[str, Any] = {
                "name": names[i],
                "spanId": ids[i],
                "attributes": [],
            }
            parent = entry[1] if isinstance(entry, tuple) else None
            if parent is not None:
                # An unknown parent name deliberately produces an id no span in
                # this trace owns, so the walk up the chain hits a gap.
                span["parentSpanId"] = (
                    ids[names.index(parent)]
                    if parent in names
                    else f"{tid}-absent-{parent}"
                )
            spans.append(span)
        return spans

    def get(self, url: str, params: dict[str, str] | None = None) -> FakeResponse:
        params = params or {}
        if "/api/search" in url:
            query, limit = params["q"], int(params.get("limit", 20))
            self.queries.append((query, limit))
            matched = [
                tid
                for tid, entries in self.traces.items()
                if _query_matches_trace(query, [_entry_name(e) for e in entries])
            ]
            return FakeResponse({"traces": [{"traceID": t} for t in matched[:limit]]})
        if "/api/traces/" in url:
            tid = url.rsplit("/", 1)[-1]
            return FakeResponse(
                {"batches": [{"scopeSpans": [{"spans": self._spans_for(tid)}]}]}
            )
        raise AssertionError(f"unexpected request: {url}")


def _entry_name(entry: Any) -> str:
    """The span name of a corpus entry, whether bare or a (name, parent) pair."""
    return entry[0] if isinstance(entry, tuple) else entry


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
            "t1": ["txq.accept", ("txq.accept_tx", "txq.accept")],
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
    tempo = FakeTempo({"t1": ["rpc.ws_message", ("rpc.command.fee", "rpc.ws_message")]})
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


def test_co_occurring_child_that_is_not_a_descendant_fails() -> None:
    """Sharing a trace is not a hierarchy -- the check must reject it.

    The whole point of a check named span.hierarchy.A->B is that B hangs under
    A. Here both spans are in one trace but tx.apply hangs off an unrelated
    root, so the relationship the report claims does not hold. Accepting this
    would let the check pass on any trace wide enough to contain both names,
    including two spans that merely happen to share a request.

    The production change that makes this fail: verifying co-occurrence only,
    without walking parentSpanId up to the parent's spanId.
    """
    tempo = FakeTempo(
        {
            "t1": [
                "ledger.build",
                "unrelated.root",
                ("tx.apply", "unrelated.root"),
            ]
        }
    )
    report = Report()
    run(
        vt._validate_parent_child(
            tempo,
            "http://tempo",
            {"parent": "ledger.build", "child": "tx.apply"},
            report,
        )
    )
    assert len(report.results) == 1, report.results
    result = report.results[0]
    assert (
        not result.passed
    ), f"co-occurrence was accepted as hierarchy: {result.message}"
    assert "not under" in result.message, result.message


def test_direct_child_passes() -> None:
    """The ordinary case: the child's parent is the parent span itself."""
    tempo = FakeTempo({"t1": ["ledger.build", ("tx.apply", "ledger.build")]})
    report = Report()
    run(
        vt._validate_parent_child(
            tempo,
            "http://tempo",
            {"parent": "ledger.build", "child": "tx.apply"},
            report,
        )
    )
    assert report.results[0].passed, report.results[0].message


def test_child_under_an_intermediate_span_still_passes() -> None:
    """A grandchild satisfies "contains" -- the contract is ancestry, not an edge.

    Every relationship in expected_spans.json is worded as the parent
    "containing" the child, so an extra span appearing in between is not a
    broken relationship. Requiring a direct edge would turn a refactor that
    introduces an intermediate scope into a false failure.

    The production change that makes this fail: comparing the child's
    parentSpanId to the parent's spanId only, instead of walking the chain.
    """
    tempo = FakeTempo(
        {
            "t1": [
                "consensus.round",
                ("consensus.establish", "consensus.round"),
                ("consensus.check", "consensus.establish"),
            ]
        }
    )
    report = Report()
    run(
        vt._validate_parent_child(
            tempo,
            "http://tempo",
            {"parent": "consensus.round", "child": "consensus.check"},
            report,
        )
    )
    assert report.results[0].passed, report.results[0].message


def test_broken_chain_is_reported_as_such_not_as_a_missing_child() -> None:
    """A chain that runs into a span the trace lacks is its own diagnosis.

    This is the dangling-parent shape: the child names a parent the trace does
    not hold, so ancestry cannot be established either way. Reporting it as "not
    under the parent" would send whoever reads it looking for a hierarchy bug in
    the instrumentation, when the actual problem is a span that never reached
    Tempo or a synthetic parent id.

    The production change that makes this fail: collapsing the broken-chain case
    into the plain not-a-descendant message.
    """
    tempo = FakeTempo(
        {
            "t1": [
                "consensus.establish",
                ("consensus.check", "never.exported"),
            ]
        }
    )
    report = Report()
    run(
        vt._validate_parent_child(
            tempo,
            "http://tempo",
            {"parent": "consensus.establish", "child": "consensus.check"},
            report,
        )
    )
    result = report.results[0]
    assert not result.passed, result.message
    assert "chain" in result.message, result.message
    # The id of the span the chain ran into is the whole diagnostic value here,
    # so the message must name it. Without this the detail could be dropped and
    # the message would still read "runs into span , which the trace..."
    assert "t1-absent-never.exported" in result.message, result.message


def test_one_child_not_under_outranks_another_childs_broken_chain() -> None:
    """A definite negative beats an unprovable one within the same trace.

    Two spans share the child's name: one dangles off a span the trace lacks,
    the other is cleanly parented by an unrelated root. The second answers the
    question -- the child really is not under the parent -- so that is what the
    report must say, whichever order the spans arrive in.

    The production change that makes this fail: returning broken_chain whenever
    any child hit a gap, without checking whether another child gave a definite
    answer.
    """
    tempo = FakeTempo(
        {
            "t1": [
                "ledger.build",
                "unrelated.root",
                ("tx.apply", "never.exported"),
                ("tx.apply", "unrelated.root"),
            ]
        }
    )
    report = Report()
    run(
        vt._validate_parent_child(
            tempo,
            "http://tempo",
            {"parent": "ledger.build", "child": "tx.apply"},
            report,
        )
    )
    result = report.results[0]
    assert not result.passed, result.message
    assert "not under" in result.message, result.message


def test_a_later_trace_can_prove_what_an_earlier_one_disproved() -> None:
    """Every candidate trace is examined, not just the first.

    The first trace carries the child parented elsewhere; the second has it
    correctly nested. One trace proving the relationship is enough, so the
    report must pass. This is the conditional-child shape: whether a given trace
    nests the child depends on which branch the code took.

    The production change that makes this fail: reporting the first trace's
    verdict, or breaking out of the loop on the first negative.
    """
    tempo = FakeTempo(
        {
            "t2": ["ledger.build", "elsewhere", ("tx.apply", "elsewhere")],
            "t1": ["ledger.build", ("tx.apply", "ledger.build")],
        }
    )
    report = Report()
    run(
        vt._validate_parent_child(
            tempo,
            "http://tempo",
            {"parent": "ledger.build", "child": "tx.apply"},
            report,
        )
    )
    assert report.results[0].passed, report.results[0].message


def test_parent_without_a_span_id_is_not_reported_as_a_missing_child() -> None:
    """An unusable parent span is its own diagnosis.

    A trace selected for containing the parent, whose parent span carries no
    spanId, cannot be used to establish ancestry. Saying "child not found" would
    send the reader after a child that is plainly present, which is the kind of
    laundering this file's Tempo error handling exists to avoid.

    The production change that makes this fail: ranking no_parent below the
    starting verdict, which makes it unreachable and falls back to the
    missing-child message.
    """
    tempo = FakeTempo({"t1": ["ledger.build", ("tx.apply", "ledger.build")]})
    # Drop the parent's id after the stub built the trace, which is the one thing
    # the corpus format cannot express.
    original = tempo._spans_for

    def without_parent_id(tid: str) -> list[dict[str, Any]]:
        spans = original(tid)
        for span in spans:
            if span["name"] == "ledger.build":
                del span["spanId"]
        return spans

    tempo._spans_for = without_parent_id  # type: ignore[method-assign]
    report = Report()
    run(
        vt._validate_parent_child(
            tempo,
            "http://tempo",
            {"parent": "ledger.build", "child": "tx.apply"},
            report,
        )
    )
    result = report.results[0]
    assert not result.passed, result.message
    assert "ledger.build" in result.message, result.message
    assert "not found" not in result.message, result.message


def test_a_cyclic_parent_chain_terminates() -> None:
    """A chain that loops must fail rather than spin.

    Malformed data can point two spans at each other. The walk keeps a seen set
    so it gives up instead of looping. Note the failure mode this guards is a
    HANG, so removing the guard makes this test time out rather than report a
    failure -- red either way, just slower.
    """
    tempo = FakeTempo(
        {
            "t1": [
                "consensus.round",
                ("consensus.check", "loop.b"),
                ("loop.b", "consensus.check"),
            ]
        }
    )
    report = Report()
    run(
        vt._validate_parent_child(
            tempo,
            "http://tempo",
            {"parent": "consensus.round", "child": "consensus.check"},
            report,
        )
    )
    assert not report.results[0].passed, report.results[0].message


def test_literal_predicate_uses_equality() -> None:
    """A non-glob child must use `=`, not a regex.

    Equality is what stops a longer emitted name satisfying a shorter contract.
    Both sides are asserted because both enforce it: the query Tempo runs, and
    _span_name_matches when the fetched spans are re-checked.
    """
    assert vt._traceql_name_predicate("txq.accept_tx") == 'name="txq.accept_tx"'
    assert vt._span_name_matches("txq.accept_tx", "txq.accept_tx")
    assert not vt._span_name_matches("txq.accept_tx_extra", "txq.accept_tx")


def main() -> int:
    tests = [v for k, v in sorted(globals().items()) if k.startswith("test_")]
    # Collecting nothing is a failure, not a pass. A rename of the test_ prefix,
    # or running this file from a context where the globals are not populated,
    # would otherwise print "0/0 passed" and exit 0 -- exactly the silent green
    # these tests exist to prevent, reproduced in the runner itself.
    if not tests:
        print("FAIL: no tests were collected")
        return 1
    failed = 0
    for test in tests:
        try:
            test()
        except AssertionError as exc:
            failed += 1
            print(f"FAIL {test.__name__}: {exc}")
        except SystemExit as exc:
            # argparse and other sys.exit() paths raise SystemExit, which is NOT
            # an Exception subclass. Uncaught it aborts the whole file, so the
            # remaining tests never run and nothing prints a FAIL line.
            failed += 1
            print(f"ERROR {test.__name__}: SystemExit({exc.code})")
        except Exception as exc:  # noqa: BLE001 - report any error as a failure
            failed += 1
            print(f"ERROR {test.__name__}: {type(exc).__name__}: {exc}")
        else:
            print(f"PASS {test.__name__}")
    print(f"\n{len(tests) - failed}/{len(tests)} passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
