"""Contract tests for immutable, source-only model access."""

from __future__ import annotations

import subprocess
from pathlib import Path

import pytest

import judge_agent
import judge_interactions
from evidence_trace import EvidenceTrace
from source_snapshot import (
    HEAD_WORKTREE,
    SnapshotAccessError,
    SnapshotBindingError,
    SnapshotDriftError,
    SourceSnapshot,
)


def git(repo: Path, *args: str) -> str:
    completed = subprocess.run(
        ["git", "-C", str(repo), *args],
        check=True,
        capture_output=True,
        text=True,
    )
    return completed.stdout.strip()


def write(repo: Path, relative: str, content: str) -> Path:
    path = repo / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")
    return path


def commit(repo: Path, message: str) -> str:
    git(repo, "add", "--all")
    git(repo, "commit", "-q", "-m", message)
    return git(repo, "rev-parse", "HEAD")


@pytest.fixture
def small_repo(tmp_path: Path) -> Path:
    repo = tmp_path / "repo"
    repo.mkdir()
    git(repo, "init", "-q")
    git(repo, "config", "user.name", "Snapshot Test")
    git(repo, "config", "user.email", "snapshot@example.invalid")
    return repo


def test_committed_head_is_read_from_git_not_the_stale_checkout(small_repo: Path):
    source = write(
        small_repo,
        "src/example.cpp",
        "int value = 1;\nint unchanged = 7;\n",
    )
    base = commit(small_repo, "base")
    source.write_text("int value = 2; // HEAD_ONLY\nint unchanged = 7;\n")
    head = commit(small_repo, "head")

    # Deliberately leave the checkout at the base. Every operation must still
    # observe the selected head commit.
    git(small_repo, "checkout", "-q", "--detach", base)
    snapshot = SourceSnapshot.from_selected(small_repo, {"base": base, "head": head})

    opened = snapshot.read_file("src/example.cpp", start_line=1, end_line=2)
    assert "1\tint value = 2; // HEAD_ONLY" in opened
    assert "int value = 1" not in opened
    assert snapshot.grep("HEAD_ONLY") == (
        "src/example.cpp:1:int value = 2; // HEAD_ONLY"
    )

    diff = snapshot.git_diff("src/example.cpp")
    assert "-int value = 1;" in diff
    assert "+int value = 2; // HEAD_ONLY" in diff
    assert snapshot.citation_line_exists("src/example.cpp", 2) is True
    assert snapshot.citation_line_exists("src/example.cpp", 3) is False
    assert snapshot.binding_metadata()["head"] == head


def test_model_access_denies_oracles_credentials_metadata_and_untracked_files(
    small_repo: Path,
):
    write(small_repo, "src/main.cpp", "int visible = 1; // SAFE_MATCH\n")
    write(
        small_repo,
        "src/test/heldout.oracle.json",
        '{"expected_judgement":"LEAK_MATCH"}\n',
    )
    write(small_repo, "cfg/secret.cfg", "password=LEAK_MATCH\n")
    write(
        small_repo,
        "bin/interaction_review/out/oracle.json",
        '{"verdict":"LEAK_MATCH"}\n',
    )
    base = commit(small_repo, "tracked tree")

    # Both names are under an allowed root, but neither is a tracked reviewable
    # source file. A guessed local credential must never become model context.
    write(small_repo, "src/credentials.pem", "LEAK_MATCH\n")
    write(small_repo, "src/untracked.cpp", "int secret = 1; // LEAK_MATCH\n")
    snapshot = SourceSnapshot.capture(small_repo, base=base, head=HEAD_WORKTREE)

    for denied in (
        "../../../etc/passwd",
        "/etc/passwd",
        ".git/config",
        "cfg/secret.cfg",
        "bin/interaction_review/out/oracle.json",
        "src/test/heldout.oracle.json",
        "src/credentials.pem",
    ):
        with pytest.raises(SnapshotAccessError):
            snapshot.read_file(denied)

    with pytest.raises(SnapshotAccessError):
        snapshot.read_file("src/untracked.cpp")

    # Default grep is source-root scoped and filters denied artifact matches.
    assert snapshot.grep("LEAK_MATCH") == "no matches"
    assert "SAFE_MATCH" in snapshot.grep("SAFE_MATCH")
    with pytest.raises(SnapshotAccessError):
        snapshot.grep("LEAK_MATCH", path="src/test/heldout.oracle.json")
    assert snapshot.citation_line_exists("src/test/heldout.oracle.json", 1) is False
    assert snapshot.citation_line_exists(".git/config", 1) is False


def test_worktree_binding_is_required_and_detects_pre_access_drift(small_repo: Path):
    source = write(small_repo, "src/example.cpp", "int version = 1;\n")
    base = commit(small_repo, "base")
    source.write_text("int version = 2;\n", encoding="utf-8")

    captured = SourceSnapshot.capture(small_repo, base=base, head=HEAD_WORKTREE)
    selected = {
        "base": base,
        "head": HEAD_WORKTREE,
        "source_binding": captured.binding_metadata(),
    }
    bound = SourceSnapshot.from_selected(small_repo, selected)
    assert "int version = 2" in bound.read_file("src/example.cpp")
    assert bound.fingerprint == captured.fingerprint

    with pytest.raises(SnapshotBindingError, match="no source binding"):
        SourceSnapshot.from_selected(small_repo, {"base": base, "head": HEAD_WORKTREE})

    source.write_text("int version = 3;\n", encoding="utf-8")
    with pytest.raises(SnapshotDriftError, match="worktree changed"):
        bound.validate_post()
    with pytest.raises(SnapshotDriftError):
        bound.read_file("src/example.cpp")
    with pytest.raises(SnapshotDriftError):
        SourceSnapshot.from_selected(small_repo, selected)


def test_worktree_operation_detects_drift_that_happens_during_the_read(
    small_repo: Path, monkeypatch: pytest.MonkeyPatch
):
    source = write(small_repo, "src/example.cpp", "int version = 1;\n")
    base = commit(small_repo, "base")
    source.write_text("int version = 2;\n", encoding="utf-8")
    snapshot = SourceSnapshot.capture(small_repo, base=base, head=HEAD_WORKTREE)
    original = snapshot._read_source_bytes

    def drifting_read(path: str) -> bytes:
        result = original(path)
        source.write_text("int version = 3;\n", encoding="utf-8")
        return result

    monkeypatch.setattr(snapshot, "_read_source_bytes", drifting_read)

    with pytest.raises(SnapshotDriftError, match="after source access"):
        snapshot.read_file("src/example.cpp")


def test_worktree_diff_and_citations_use_tracked_current_content(small_repo: Path):
    source = write(small_repo, "include/example.h", "#define VALUE 1\n")
    base = commit(small_repo, "base")
    source.write_text("#define VALUE 9 // WORKTREE_ONLY\n", encoding="utf-8")
    snapshot = SourceSnapshot.capture(small_repo, base=base, head=HEAD_WORKTREE)

    assert "WORKTREE_ONLY" in snapshot.read_file("include/example.h")
    assert snapshot.grep("WORKTREE_ONLY", path="include") == (
        "include/example.h:1:#define VALUE 9 // WORKTREE_ONLY"
    )
    diff = snapshot.git_diff("include/example.h", context=0)
    assert "-#define VALUE 1" in diff
    assert "+#define VALUE 9 // WORKTREE_ONLY" in diff
    assert snapshot.citation_exists(
        {"file": "include/example.h", "line": 1, "what": "new value"}
    )
    assert not snapshot.citation_exists(
        {"file": "include/example.h", "line": 2, "what": "invented"}
    )


def test_judge_tools_and_citations_use_the_same_committed_snapshot(small_repo: Path):
    source = write(small_repo, "src/example.cpp", "int base_only = 1;\n")
    base = commit(small_repo, "base")
    source.write_text(
        "int base_only = 1;\nint selected_head_only = 2;\n", encoding="utf-8"
    )
    head = commit(small_repo, "selected head")
    git(small_repo, "checkout", "-q", "--detach", base)
    snapshot = SourceSnapshot.from_selected(small_repo, {"base": base, "head": head})

    args = {"path": "src/example.cpp", "start_line": 2, "end_line": 2}
    exposed = judge_agent._dispatch_snapshot(snapshot, "read_file", args)
    trace = EvidenceTrace(small_repo)
    trace.record("read_file", args, exposed)
    verdict, dropped = judge_interactions.verify_citations(
        {
            "verdict": judge_agent.VERDICT_HANDLED,
            "behavior": judge_agent.BEHAVIOR_CORRECT,
            "coverage": judge_agent.COVERAGE_COVERED,
            "confidence": "high",
            "summary": "The selected head contains the cited second source line.",
            "detail": "The stale checkout deliberately lacks it; only the bound head has it.",
            "primary_location": None,
            "location_assessments": [],
            "states_reached": [],
            "states_unreached": [],
            "citations": [
                {
                    "file": "src/example.cpp",
                    "line": 2,
                    "what": "selected-head-only line",
                }
            ],
        },
        snapshot,
        trace,
    )

    assert "selected_head_only" in exposed
    assert verdict["behavior"] == judge_agent.BEHAVIOR_CORRECT
    assert verdict["citations"][0]["line"] == 2
    assert dropped == []
