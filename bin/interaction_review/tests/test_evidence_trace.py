"""Evidence-grounding tests for the model's read-only tool transcript."""

from __future__ import annotations

import hashlib
import json

import pytest

from evidence_trace import (
    REVISION_NEW,
    REVISION_OLD,
    REVISION_WORKING,
    EvidenceTrace,
    normalize_repo_path,
)


def test_read_file_credits_only_numbered_lines_actually_returned(tmp_path):
    trace = EvidenceTrace(tmp_path)
    trace.record(
        "read_file",
        {"path": "./src/../src/example.cpp", "start_line": 8, "end_line": 20},
        "src/example.cpp lines 10-12 of 40:\n10\tone\n11\ttwo\n12\tthree",
    )

    assert trace.was_observed("src/example.cpp", 10)
    assert trace.was_observed("src/example.cpp", 12, revision=REVISION_WORKING)
    assert not trace.was_observed("src/example.cpp", 8)
    assert not trace.was_observed("src/example.cpp", 13)
    assert trace.citation_observed(
        {"file": "./src/example.cpp", "line": 11, "what": "the branch"}
    )

    call = trace.to_dict()["calls"][0]
    assert call["normalized_path"] == "src/example.cpp"
    assert call["spans"] == [
        {
            "file": "src/example.cpp",
            "start_line": 10,
            "end_line": 12,
            "revision": REVISION_WORKING,
        }
    ]


def test_read_file_eof_message_does_not_credit_the_requested_range(tmp_path):
    trace = EvidenceTrace(tmp_path)
    trace.record(
        "read_file",
        {"path": "src/example.cpp", "start_line": 99},
        "src/example.cpp has 12 lines; 99 is past the end.",
    )

    assert not trace.was_observed("src/example.cpp", 99)
    assert trace.to_dict()["calls"][0]["spans"] == []


def test_grep_records_sparse_matches_and_detects_its_result_cap(tmp_path):
    trace = EvidenceTrace(tmp_path)
    trace.record(
        "grep",
        {"pattern": "check(Sign|Permission)", "path": "./src"},
        "src/a.cpp:7:checkSign();\n"
        "src/a.cpp:9:checkPermission();\n"
        "src/b.cpp:2:checkSign();\n"
        "... 13 more matches; narrow the pattern or path.",
    )

    assert trace.was_observed("src/a.cpp", 7)
    assert trace.was_observed("src/a.cpp", 9)
    assert trace.was_observed("src/b.cpp", 2)
    assert not trace.was_observed("src/a.cpp", 8)
    assert trace.to_dict()["calls"][0]["truncated"] is True


def test_unified_diff_maps_context_removals_and_additions_to_both_sides(tmp_path):
    trace = EvidenceTrace(tmp_path)
    trace.record(
        "git_diff",
        {"path": "src/example.cpp"},
        "diff --git a/src/example.cpp b/src/example.cpp\n"
        "--- a/src/example.cpp\n"
        "+++ b/src/example.cpp\n"
        "@@ -10,3 +10,4 @@ void f()\n"
        " context\n"
        "-old value\n"
        "+new value\n"
        "+another value\n"
        " tail\n",
    )

    # Context lines advance independently after the insertion.
    assert trace.was_observed("src/example.cpp", 10, revision=REVISION_OLD)
    assert trace.was_observed("src/example.cpp", 10, revision=REVISION_NEW)
    assert trace.was_observed("src/example.cpp", 11, revision=REVISION_OLD)
    assert trace.was_observed("src/example.cpp", 11, revision=REVISION_NEW)
    assert trace.was_observed("src/example.cpp", 12, revision=REVISION_NEW)
    assert trace.was_observed("src/example.cpp", 12, revision=REVISION_OLD)
    assert trace.was_observed("src/example.cpp", 13, revision=REVISION_NEW)
    assert not trace.was_observed("src/example.cpp", 13, revision=REVISION_OLD)

    assert trace.to_dict()["calls"][0]["spans"] == [
        {
            "file": "src/example.cpp",
            "start_line": 10,
            "end_line": 13,
            "revision": REVISION_NEW,
        },
        {
            "file": "src/example.cpp",
            "start_line": 10,
            "end_line": 12,
            "revision": REVISION_OLD,
        },
    ]


def test_unified_diff_uses_distinct_paths_for_a_rename(tmp_path):
    trace = EvidenceTrace(tmp_path)
    trace.record(
        "git_diff",
        {"path": "new name.cpp"},
        'diff --git "a/old name.cpp" "b/new name.cpp"\n'
        '--- "a/old name.cpp"\n'
        '+++ "b/new name.cpp"\n'
        "@@ -4 +4 @@\n"
        "-before\n"
        "+after\n",
    )

    assert trace.was_observed("old name.cpp", 4, revision=REVISION_OLD)
    assert not trace.was_observed("old name.cpp", 4, revision=REVISION_NEW)
    assert trace.was_observed("new name.cpp", 4, revision=REVISION_NEW)
    assert not trace.was_observed("new name.cpp", 4, revision=REVISION_OLD)


def test_disjoint_diff_hunks_remain_disjoint_observed_spans(tmp_path):
    trace = EvidenceTrace(tmp_path)
    trace.record(
        "git_diff",
        {"path": "f.cpp"},
        "--- a/f.cpp\n"
        "+++ b/f.cpp\n"
        "@@ -2 +2 @@\n"
        "-a\n"
        "+b\n"
        "@@ -20 +20 @@\n"
        "-c\n"
        "+d\n",
    )

    new_spans = [
        span
        for span in trace.to_dict()["calls"][0]["spans"]
        if span["revision"] == REVISION_NEW
    ]
    assert [(span["start_line"], span["end_line"]) for span in new_spans] == [
        (2, 2),
        (20, 20),
    ]
    assert not trace.was_observed("f.cpp", 10)


def test_an_error_is_audited_but_never_becomes_evidence(tmp_path):
    trace = EvidenceTrace(tmp_path)
    call = trace.record(
        "read_file",
        {"path": "secret.cpp", "start_line": 1},
        "secret.cpp lines 1-1 of 1:\n1\tpretend source",
        is_error=True,
    )

    assert call["is_error"] is True
    assert call["spans"] == []
    assert not trace.was_observed("secret.cpp", 1)


def test_a_truncated_result_credits_only_the_visible_prefix(tmp_path):
    result = (
        "src/example.cpp lines 30-35 of 100:\n"
        "30\tvisible\n"
        "31\tpartly visible\n"
        "... truncated; request a narrower range."
    )
    trace = EvidenceTrace(tmp_path)
    call = trace.record(
        "read_file", {"path": "src/example.cpp", "start_line": 30}, result
    )

    assert call["truncated"] is True
    assert trace.was_observed("src/example.cpp", 30)
    assert trace.was_observed("src/example.cpp", 31)
    assert not trace.was_observed("src/example.cpp", 32)
    assert call["result_sha256"] == hashlib.sha256(result.encode()).hexdigest()


def test_audit_metadata_is_serializable_content_addressed_and_detached(tmp_path):
    trace = EvidenceTrace(tmp_path)
    returned = trace.record("grep", {"pattern": "needle"}, "src/a.cpp:3:needle")
    returned["spans"].clear()

    audit = trace.audit_metadata()
    assert json.loads(json.dumps(audit)) == audit
    assert len(audit["trace_sha256"]) == 64
    assert len(audit["calls"][0]["args_sha256"]) == 64
    assert len(audit["calls"][0]["result_sha256"]) == 64
    assert len(audit["calls"][0]["record_sha256"]) == 64
    assert "result" not in audit["calls"][0]
    assert audit["calls"][0]["spans"], "the returned record must be detached"

    other = EvidenceTrace(tmp_path)
    other.record("grep", {"pattern": "needle"}, "src/a.cpp:4:needle")
    assert other.to_dict()["trace_sha256"] != audit["trace_sha256"]


def test_paths_are_normalized_and_escapes_never_match(tmp_path):
    inside = tmp_path / "src" / "example.cpp"
    inside.parent.mkdir()
    inside.write_text("line\n")

    assert normalize_repo_path(tmp_path, "./src/../src/example.cpp") == (
        "src/example.cpp"
    )
    assert normalize_repo_path(tmp_path, str(inside)) == "src/example.cpp"
    with pytest.raises(ValueError, match="escapes the repository"):
        normalize_repo_path(tmp_path, "../outside.cpp")

    trace = EvidenceTrace(tmp_path)
    trace.record(
        "read_file",
        {"path": "src/example.cpp"},
        "./src/../src/example.cpp lines 1-1 of 1:\n1\tline",
    )
    assert trace.was_observed(str(inside), 1)
    assert not trace.was_observed("../outside.cpp", 1)
    assert not trace.citation_observed({"file": "src/example.cpp", "line": True})


def test_explicit_truncation_flag_handles_transport_level_caps(tmp_path):
    trace = EvidenceTrace(tmp_path)
    call = trace.record(
        "grep",
        {"pattern": "x"},
        "src/a.cpp:1:x",
        truncated=True,
    )

    assert call["truncated"] is True
    assert trace.was_observed("src/a.cpp", 1)
