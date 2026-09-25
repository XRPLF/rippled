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
import tempfile
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).parent))

import validate_telemetry as vt  # noqa: E402

# The node a corpus entry belongs to unless it says otherwise. Named rather than
# repeated, because a test that writes it out for one span and relies on the
# default for another is asserting the two are the same node.
DEFAULT_INSTANCE = "node-1"

# Arbitrary, and only the relative order matters. Non-zero so that a span whose
# start time went missing somewhere reads as earlier than every real one rather
# than tying with them.
START_TIME_BASE_NANOS = 1_000_000_000

# A corpus entry naming this as its parent gets the all-zero span id written out
# as its parentSpanId, instead of an id resolved from another span's name. That is
# OTLP's second spelling of "no parent": Tempo omits the field, but the field is
# optional rather than forbidden, so a root can arrive spelled this way.
ROOT_PARENT_SPAN_ID = "AAAAAAAAAAA="


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
                either a bare span name (a root span, no parent) or a tuple
                ``(name, parent_name[, instance[, attributes]])``. A parent_name
                that no span in the trace carries yields a parentSpanId pointing
                at a span the trace does not hold, which is how a dangling chain
                is expressed, and the sentinel ROOT_PARENT_SPAN_ID writes OTLP's
                all-zero "no parent" id verbatim. ``instance`` is the exporting
                node's service.instance.id and defaults to DEFAULT_INSTANCE, so
                a cross-node parent is written by giving the two spans different
                ones. ``attributes`` is a plain str-to-str mapping, emitted in
                OTLP stringValue form.

    Span ids are generated as ``<trace id>-<index>``. Their spelling does not
    matter: the code under test compares parentSpanId to spanId as opaque
    strings, exactly because Tempo's own encoding of those fields (hex or
    base64) is not something the validator should depend on.

    Start times follow the corpus order, one nanosecond apart, so listing spans
    in the order they ran is how a test states that order. That is what lets the
    round-shape check's phase-order assertion be exercised at all.
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
                "attributes": [
                    {"key": k, "value": {"stringValue": v}}
                    for k, v in _entry_attributes(entry).items()
                ],
                "startTimeUnixNano": str(START_TIME_BASE_NANOS + i),
            }
            parent = entry[1] if isinstance(entry, tuple) else None
            if parent == ROOT_PARENT_SPAN_ID:
                span["parentSpanId"] = ROOT_PARENT_SPAN_ID
            elif parent is not None:
                # An unknown parent name deliberately produces an id no span in
                # this trace owns, so the walk up the chain hits a gap.
                span["parentSpanId"] = (
                    ids[names.index(parent)]
                    if parent in names
                    else f"{tid}-absent-{parent}"
                )
            spans.append(span)
        return spans

    def _batches_for(self, tid: str) -> list[dict[str, Any]]:
        """Group one trace's spans into one OTLP batch per exporting node.

        Tempo carries service.instance.id on the batch resource, not on the
        span, so a trace that spans two nodes genuinely arrives as two batches.
        The parent gate reads that field to tell a same-node parent from a
        cross-node one, and a single batch could not express the difference --
        every span would claim the same node and a cross-node parent would read
        as a mis-parenting.
        """
        spans = self._spans_for(tid)
        instances = [_entry_instance(e) for e in self.traces.get(tid, [])]
        grouped: dict[str, list[dict[str, Any]]] = {}
        for span, instance in zip(spans, instances):
            grouped.setdefault(instance, []).append(span)
        return [
            {
                "resource": {
                    "attributes": [
                        {
                            "key": "service.instance.id",
                            "value": {"stringValue": instance},
                        }
                    ]
                },
                "scopeSpans": [{"spans": batch}],
            }
            for instance, batch in grouped.items()
        ]

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
            return FakeResponse({"batches": self._batches_for(tid)})
        raise AssertionError(f"unexpected request: {url}")


def _entry_name(entry: Any) -> str:
    """The span name of a corpus entry, whether bare or a tuple."""
    return entry[0] if isinstance(entry, tuple) else entry


def _entry_instance(entry: Any) -> str:
    """The exporting node of a corpus entry, defaulting to DEFAULT_INSTANCE."""
    if isinstance(entry, tuple) and len(entry) > 2 and entry[2] is not None:
        return str(entry[2])
    return DEFAULT_INSTANCE


def _entry_attributes(entry: Any) -> dict[str, str]:
    """The span attributes of a corpus entry, defaulting to none."""
    if isinstance(entry, tuple) and len(entry) > 3 and entry[3] is not None:
        return dict(entry[3])
    return {}


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


def test_span_declared_root_that_is_a_same_node_child_fails() -> None:
    """The audit finding: ledger.build declared ROOT, emitted under accept."""
    tempo = FakeTempo(
        {"t1": ["consensus.accept", ("ledger.build", "consensus.accept")]}
    )
    report = Report()
    run(
        vt._validate_span_parents_for(
            tempo,
            "http://tempo",
            {"name": "ledger.build", "allowed_parents": ["ROOT"]},
            report,
        )
    )
    assert len(report.results) == 1, report.results
    assert not report.results[0].passed, report.results[0].message
    assert "consensus.accept" in report.results[0].message


def test_cross_node_parent_is_not_a_violation() -> None:
    """Review Focus 1: tx.receive's parent is the sender's span, on another node."""
    tempo = FakeTempo(
        {"t1": [("tx.process", None, "node-2"), ("tx.receive", "tx.process", "node-1")]}
    )
    report = Report()
    run(
        vt._validate_span_parents_for(
            tempo,
            "http://tempo",
            {"name": "tx.receive", "allowed_parents": ["ROOT"]},
            report,
        )
    )
    assert len(report.results) == 1, report.results
    assert report.results[0].passed, report.results[0].message


def test_same_node_parent_of_a_receive_span_is_a_violation() -> None:
    """The control for the test above: the node id is what excuses the parent.

    Identical corpus except that both spans came from one node, which is the
    shape a genuine mis-parenting of tx.receive would have. Without this, the
    cross-node test passes just as happily against a gate that never reads
    _instance at all and treats every in-trace parent as unprovable.
    """
    tempo = FakeTempo(
        {"t1": [("tx.process", None, "node-1"), ("tx.receive", "tx.process", "node-1")]}
    )
    report = Report()
    run(
        vt._validate_span_parents_for(
            tempo,
            "http://tempo",
            {"name": "tx.receive", "allowed_parents": ["ROOT"]},
            report,
        )
    )
    assert len(report.results) == 1, report.results
    assert not report.results[0].passed, report.results[0].message
    assert "tx.process" in report.results[0].message


def test_second_call_path_parent_is_allowed_when_listed() -> None:
    """Review Focus 2: txq.accept is a child on one path and a root on the other."""
    tempo = FakeTempo(
        {
            "t1": ["consensus.accept.apply", ("txq.accept", "consensus.accept.apply")],
            "t2": ["txq.accept"],
        }
    )
    report = Report()
    run(
        vt._validate_span_parents_for(
            tempo,
            "http://tempo",
            {
                "name": "txq.accept",
                "allowed_parents": ["consensus.accept.apply", "ROOT"],
            },
            report,
        )
    )
    assert len(report.results) == 1, report.results
    assert report.results[0].passed, report.results[0].message


def test_absent_optional_span_skips_rather_than_fails() -> None:
    """Review Focus 3: a span the harness never emits has nothing to judge."""
    tempo = FakeTempo({"t1": ["consensus.round"]})
    report = Report()
    run(
        vt._validate_span_parents_for(
            tempo,
            "http://tempo",
            {
                "name": "nodestore.rotate.swap",
                "allowed_parents": ["nodestore.rotate"],
                "optional": True,
            },
            report,
        )
    )
    assert len(report.results) == 1, report.results
    assert report.results[0].passed, report.results[0].message
    assert "not emitted" in report.results[0].message


def test_absent_required_span_fails_rather_than_skips() -> None:
    """A span the contract does NOT mark optional must fail when absent.

    The control for the skip above. A gate that returned passed=True for every
    absent span would report green on a node that stopped emitting consensus
    spans entirely, which is the loudest failure the harness exists to catch.
    """
    tempo = FakeTempo({"t1": ["consensus.round"]})
    report = Report()
    run(
        vt._validate_span_parents_for(
            tempo,
            "http://tempo",
            {"name": "ledger.build", "allowed_parents": ["ROOT"]},
            report,
        )
    )
    assert len(report.results) == 1, report.results
    assert not report.results[0].passed, report.results[0].message
    assert "not emitted" in report.results[0].message


def test_parent_id_absent_from_the_trace_is_inconclusive() -> None:
    """Review Focus 4: a rotation still in flight has not exported its root."""
    tempo = FakeTempo({"t1": [("nodestore.rotate.copy", "nodestore.rotate")]})
    report = Report()
    run(
        vt._validate_span_parents_for(
            tempo,
            "http://tempo",
            {
                "name": "nodestore.rotate.copy",
                "allowed_parents": ["nodestore.rotate"],
                "optional": True,
            },
            report,
        )
    )
    assert len(report.results) == 1, report.results
    assert report.results[0].passed, report.results[0].message
    assert "nothing was provable" in report.results[0].message


def test_a_glob_in_allowed_parents_matches_the_family() -> None:
    """pathfind.request's only lawful parent is written as rpc.command.*.

    The contract allows a glob on the parent side as well as on the span's own
    name, and the concrete command varies per request, so plain set membership
    would read every real parent as a violation. Asserted here because the
    pathfinding family is never emitted on the harness, so a live run cannot
    reach this path and would not notice it being wrong.
    """
    tempo = FakeTempo(
        {"t1": ["rpc.command.fee", ("pathfind.request", "rpc.command.fee")]}
    )
    report = Report()
    run(
        vt._validate_span_parents_for(
            tempo,
            "http://tempo",
            {"name": "pathfind.request", "allowed_parents": ["rpc.command.*"]},
            report,
        )
    )
    assert len(report.results) == 1, report.results
    assert report.results[0].passed, report.results[0].message


def test_round_missing_a_required_child_fails() -> None:
    """A round whose phases are incomplete must fail, naming the phase.

    consensus.accept is present on purpose: it is the trace-selection predicate,
    so a corpus without it exercises the "no trace holds both" path instead and
    the test would be red for the wrong reason. The genuinely missing phase here
    is consensus.ledger_close.
    """
    tempo = FakeTempo(
        {
            "t1": [
                "consensus.round",
                ("consensus.phase.open", "consensus.round"),
                ("consensus.establish", "consensus.round"),
                ("consensus.accept", "consensus.round"),
            ]
        }
    )
    report = Report()
    run(vt.validate_consensus_round_shape(tempo, "http://tempo", report))
    children = next(r for r in report.results if r.name == "span.round.children")
    assert not children.passed, children.message
    assert "consensus.ledger_close" in children.message
    # The node id is the whole point of a red here: a five-node cluster gives no
    # way to act on "1 of 5 rounds" without it.
    assert DEFAULT_INSTANCE in children.message, children.message


def test_round_without_accept_in_the_newest_trace_is_not_a_missing_child() -> None:
    """Item 3: a round exported before its accept span must not read as broken.

    The accept span always outlives the round span, and with a two-second export
    batch delay the newest round can reach Tempo while its consensus.accept child
    is still in the exporter. Selecting the newest rounds reports a missing child
    on a healthy cluster; selecting traces that hold both does not.

    The production change that makes this fail: dropping the
    `&& {name="consensus.accept"}` term from the search query.
    """
    tempo = FakeTempo(
        {
            # Newest first, as /api/search returns. The newest round has not had
            # its accept exported yet.
            "t2": ["consensus.round", ("consensus.phase.open", "consensus.round")],
            "t1": [
                "consensus.round",
                ("consensus.phase.open", "consensus.round"),
                ("consensus.ledger_close", "consensus.round"),
                ("consensus.establish", "consensus.round"),
                ("consensus.accept", "consensus.round"),
            ],
        }
    )
    report = Report()
    run(vt.validate_consensus_round_shape(tempo, "http://tempo", report))
    assert all(r.passed for r in report.results), [r.message for r in report.results]


def test_no_trace_holding_both_round_and_accept_is_its_own_message() -> None:
    """Nothing to judge is a distinct failure from a badly shaped round.

    Reporting it as a missing child would send whoever reads it looking for a
    consensus bug when the real state is that Tempo holds no usable trace.
    """
    tempo = FakeTempo({"t1": ["ledger.build"]})
    report = Report()
    run(vt.validate_consensus_round_shape(tempo, "http://tempo", report))
    assert len(report.results) == 1, report.results
    assert not report.results[0].passed
    assert "No trace holds both" in report.results[0].message


def test_a_round_is_not_shaped_from_another_nodes_phase_spans() -> None:
    """A round's children are its OWN node's children, not the trace's.

    The deterministic trace strategy derives the trace_id from the previous
    ledger hash, so all five validators' round spans arrive in one trace. Here
    node-1's round span has no phases of its own and every phase span in the
    trace was exported by node-2 while naming node-1's round as its parent --
    which is precisely the case a parentSpanId-only filter cannot tell apart from
    a healthy round. node-1's round is missing all four phases, and that is what
    the gate must report.

    The production change that makes this fail: dropping the _instance
    comparison from the child filter, which makes node-1's round look complete.
    """
    tempo = FakeTempo(
        {
            "t1": [
                "consensus.round",
                ("consensus.phase.open", "consensus.round", "node-2"),
                ("consensus.ledger_close", "consensus.round", "node-2"),
                ("consensus.establish", "consensus.round", "node-2"),
                ("consensus.accept", "consensus.round", "node-2"),
            ]
        }
    )
    report = Report()
    run(vt.validate_consensus_round_shape(tempo, "http://tempo", report))
    children = next(r for r in report.results if r.name == "span.round.children")
    assert not children.passed, children.message
    assert children.details["missing"] == {
        "consensus.phase.open": 1,
        "consensus.ledger_close": 1,
        "consensus.establish": 1,
        "consensus.accept": 1,
    }, children.details
    assert DEFAULT_INSTANCE in children.message, children.message


def test_round_with_all_children_in_order_passes() -> None:
    tempo = FakeTempo(
        {
            "t1": [
                "consensus.round",
                ("consensus.phase.open", "consensus.round"),
                ("consensus.ledger_close", "consensus.round"),
                ("consensus.establish", "consensus.round"),
                ("consensus.accept", "consensus.round"),
            ]
        }
    )
    report = Report()
    run(vt.validate_consensus_round_shape(tempo, "http://tempo", report))
    assert all(r.passed for r in report.results), [r.message for r in report.results]


def test_round_with_phases_out_of_order_fails() -> None:
    """The control for the pass above: the order has to be read, not assumed.

    Every required child is present, so span.round.children passes; only
    span.round.phase_order can catch accept having started before open. Without
    this test the positive case above passes against a gate that never compares
    start times at all.
    """
    tempo = FakeTempo(
        {
            "t1": [
                "consensus.round",
                ("consensus.accept", "consensus.round"),
                ("consensus.phase.open", "consensus.round"),
                ("consensus.ledger_close", "consensus.round"),
                ("consensus.establish", "consensus.round"),
            ]
        }
    )
    report = Report()
    run(vt.validate_consensus_round_shape(tempo, "http://tempo", report))
    children = next(r for r in report.results if r.name == "span.round.children")
    assert children.passed, children.message
    order = next(r for r in report.results if r.name == "span.round.phase_order")
    assert not order.passed, order.message
    assert "out of order" in order.message


def test_mode_change_with_equal_modes_fails() -> None:
    """Finding 2: a mode_change span that records no change."""
    tempo = FakeTempo(
        {
            "t1": [
                "consensus.round",
                ("consensus.phase.open", "consensus.round"),
                ("consensus.ledger_close", "consensus.round"),
                ("consensus.establish", "consensus.round"),
                ("consensus.accept", "consensus.round"),
                (
                    "consensus.mode_change",
                    "consensus.round",
                    "node-1",
                    {"mode_old": "Observing", "mode_new": "Observing"},
                ),
            ]
        }
    )
    report = Report()
    run(vt.validate_consensus_round_shape(tempo, "http://tempo", report))
    mc = next(
        r for r in report.results if r.name == "span.mode_change.records_a_real_change"
    )
    assert not mc.passed, mc.message


def test_mode_change_recording_a_real_change_passes() -> None:
    """The control: a transition must not be reported as a defect.

    Same corpus as the failing case with one mode differing, so the gate is
    shown to be reading the two attributes rather than failing on the span's
    mere presence -- which would make every real mode transition red.
    """
    tempo = FakeTempo(
        {
            "t1": [
                "consensus.round",
                ("consensus.phase.open", "consensus.round"),
                ("consensus.ledger_close", "consensus.round"),
                ("consensus.establish", "consensus.round"),
                ("consensus.accept", "consensus.round"),
                (
                    "consensus.mode_change",
                    "consensus.round",
                    "node-1",
                    {"mode_old": "Observing", "mode_new": "Proposing"},
                ),
            ]
        }
    )
    report = Report()
    run(vt.validate_consensus_round_shape(tempo, "http://tempo", report))
    mc = next(
        r for r in report.results if r.name == "span.mode_change.records_a_real_change"
    )
    assert mc.passed, mc.message
    assert "1 mode_change span(s)" in mc.message


def test_root_written_as_the_all_zero_span_id_still_counts_as_root() -> None:
    """OTLP's other spelling of "no parent" must not read as unprovable.

    Tempo omits parentSpanId for a root, so this shape does not occur against it
    today. An exporter or backend that writes the all-zero id instead would make
    EVERY root unprovable, and an unprovable parent passes -- so the gate would
    go quietly fail-open on exactly the spans it exists to judge.

    The message is asserted, not just the verdict: without the all-zero handling
    this test still sees passed=True, because the id matches no span in the trace
    and the span is counted as unprovable instead.
    """
    tempo = FakeTempo({"t1": [("ledger.build", ROOT_PARENT_SPAN_ID)]})
    report = Report()
    run(
        vt._validate_span_parents_for(
            tempo,
            "http://tempo",
            {"name": "ledger.build", "allowed_parents": ["ROOT"]},
            report,
        )
    )
    assert len(report.results) == 1, report.results
    assert report.results[0].passed, report.results[0].message
    message = report.results[0].message
    assert "every provable parent" in message, message
    assert report.results[0].details["observed"] == {"ROOT": 1}, report.results[
        0
    ].details
    assert report.results[0].details["unprovable"] == 0, report.results[0].details


def test_validate_span_parents_checks_every_contract_entry() -> None:
    """The sweep must visit the whole inventory, one result per entry.

    _validate_span_parents_for is well covered on its own, but nothing proved
    that the caller iterates -- a loop that returned after the first entry, or
    read a different key than 'spans', would leave 40 spans unchecked while the
    report still looked healthy. Driven through a real file so the loader is
    exercised too, rather than by stubbing _load_expected_spans.
    """
    contract = {
        "spans": [
            {"name": "consensus.round", "allowed_parents": ["ROOT"]},
            {
                "name": "consensus.accept",
                "allowed_parents": ["consensus.round"],
            },
        ]
    }
    tempo = FakeTempo(
        {"t1": ["consensus.round", ("consensus.accept", "consensus.round")]}
    )
    report = Report()
    original = vt.EXPECTED_SPANS_FILE
    with tempfile.TemporaryDirectory() as tmp:
        scratch = Path(tmp) / "expected_spans.json"
        scratch.write_text(json.dumps(contract))
        vt.EXPECTED_SPANS_FILE = scratch
        try:
            run(vt.validate_span_parents(tempo, "http://tempo", report))
        finally:
            vt.EXPECTED_SPANS_FILE = original
    assert [r.name for r in report.results] == [
        "span.parent.consensus.round",
        "span.parent.consensus.accept",
    ], [r.name for r in report.results]
    assert all(r.passed for r in report.results), [r.message for r in report.results]


def test_a_contract_entry_with_no_name_fails_only_itself() -> None:
    """A malformed entry must not abort the sweep over the rest.

    Reading span_def["name"] outside the try raised KeyError out of the loop, so
    one bad entry silently cost every later span its check. Now it is one failed
    result and the sweep continues.
    """
    contract = {
        "spans": [
            {"allowed_parents": ["ROOT"]},
            {"name": "consensus.round", "allowed_parents": ["ROOT"]},
        ]
    }
    tempo = FakeTempo({"t1": ["consensus.round"]})
    report = Report()
    original = vt.EXPECTED_SPANS_FILE
    with tempfile.TemporaryDirectory() as tmp:
        scratch = Path(tmp) / "expected_spans.json"
        scratch.write_text(json.dumps(contract))
        vt.EXPECTED_SPANS_FILE = scratch
        try:
            run(vt.validate_span_parents(tempo, "http://tempo", report))
        finally:
            vt.EXPECTED_SPANS_FILE = original
    assert len(report.results) == 2, [r.name for r in report.results]
    assert not report.results[0].passed, report.results[0].message
    assert report.results[0].name == "span.parent.<unnamed>"
    assert report.results[1].passed, report.results[1].message


def test_every_contract_span_declares_allowed_parents() -> None:
    """The contract itself: no entry may be left without the new key.

    validate_span_parents returns early on an entry with an empty or missing
    allowed_parents, so a span that kept the old `parent` key would be silently
    unchecked -- the exact failure mode this change exists to remove. Asserted
    against the real file rather than a fixture, because the file is the thing
    that can drift.
    """
    contract = vt._load_expected_spans()
    spans = contract["spans"]
    assert spans, "expected_spans.json declares no spans"
    missing = [s["name"] for s in spans if not s.get("allowed_parents")]
    assert not missing, f"entries with no allowed_parents: {missing}"
    stale = [s["name"] for s in spans if "parent" in s]
    assert not stale, f"entries still carrying the old parent key: {stale}"


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
