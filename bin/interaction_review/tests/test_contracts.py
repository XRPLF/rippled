"""Properties the design leans on but no single module owns.

Determinism, schema/constant agreement, and the git plumbing — all things that
were verified by hand during review and so are pinned here instead.
"""

import json
import os
import subprocess
import sys
from pathlib import Path

import pytest

import build_graph as bg
import graph as g
import pr_map
from diffparse import diff_ranges, merge_base, resolve_rev

TOOL_DIR = Path(__file__).resolve().parents[1]


# --- schema / constant agreement -------------------------------------------


def _schema(name: str) -> dict:
    return json.loads((TOOL_DIR / name).read_text())


def test_location_roles_match_between_code_and_schemas():
    """The role enum is written in three places; drift would let the code emit a
    value its own schema rejects."""
    expected = sorted(g.LOC_ROLES)
    graph_schema = _schema("graph.schema.json")
    assert (
        sorted(
            graph_schema["$defs"]["locations"]["items"]["properties"]["role"]["enum"]
        )
        == expected
    )
    touched = _schema("touched.schema.json")
    role = touched["properties"]["touched"]["items"]["properties"]["evidence"]["items"][
        "properties"
    ]["role"]["enum"]
    assert sorted(role) == expected


def test_node_kinds_match_between_code_and_schemas():
    graph_schema = _schema("graph.schema.json")
    feature_kinds = graph_schema["properties"]["features"]["items"]["properties"][
        "kind"
    ]["enum"]
    resource_kinds = graph_schema["properties"]["resources"]["items"]["properties"][
        "kind"
    ]["enum"]
    assert sorted(feature_kinds) == sorted([g.FEATURE_TRANSACTOR, g.FEATURE_AMENDMENT])
    assert sorted(resource_kinds) == sorted(
        [g.RESOURCE_FORK, g.RESOURCE_INVARIANT, g.RESOURCE_SFIELD]
    )
    touched_kinds = _schema("touched.schema.json")["properties"]["touched"]["items"][
        "properties"
    ]["kind"]["enum"]
    assert sorted(touched_kinds) == sorted(feature_kinds + resource_kinds)


def test_match_strengths_match_between_code_and_schema():
    enum = _schema("touched.schema.json")["properties"]["touched"]["items"][
        "properties"
    ]["match"]["enum"]
    assert sorted(enum) == sorted([pr_map.MATCH_SPAN, pr_map.MATCH_FILE])


def test_schema_version_constant_matches_both_schemas():
    assert _schema("graph.schema.json")["properties"]["schema"]["const"] == (
        g.SCHEMA_VERSION
    )
    assert _schema("touched.schema.json")["properties"]["schema"]["const"] == 1


# --- determinism ------------------------------------------------------------


def test_graph_build_is_byte_identical_across_runs(tmp_path, libclang_dylib, build_dir):
    """Head-vs-base diffing is meaningless unless an unchanged tree produces an
    unchanged artifact. Verified across hash seeds because dict and set
    iteration order is what would break it."""
    outputs = []
    for seed in ("0", "1", "999"):
        out = tmp_path / f"run{seed}"
        proc = subprocess.run(
            [
                sys.executable,
                str(TOOL_DIR / "build_graph.py"),
                "--build-dir",
                str(build_dir),
                "--out",
                str(out),
            ],
            capture_output=True,
            text=True,
            env={**os.environ, "PYTHONHASHSEED": seed},
            cwd=str(TOOL_DIR),
        )
        assert proc.returncode == 0, proc.stderr
        outputs.append(
            (
                (out / "graph.json").read_bytes(),
                (out / "interactions.json").read_bytes(),
            )
        )
    assert outputs[0] == outputs[1] == outputs[2]


# --- git plumbing -----------------------------------------------------------


@pytest.fixture()
def tiny_repo(tmp_path):
    """A throwaway repo with a branch, so the merge-base claim is exercised."""

    def git(*args):
        subprocess.run(
            ["git", "-C", str(tmp_path), *args], check=True, capture_output=True
        )

    git("init", "-q", "-b", "main")
    git("config", "user.email", "t@example.com")
    git("config", "user.name", "T")
    (tmp_path / "a.cpp").write_text("one\ntwo\nthree\n")
    git("add", "-A")
    git("commit", "-qm", "base")
    git("checkout", "-q", "-b", "topic")
    (tmp_path / "a.cpp").write_text("one\nCHANGED\nthree\n")
    git("add", "-A")
    git("commit", "-qm", "change")
    # A commit on main after the branch point: it must NOT appear in the diff.
    git("checkout", "-q", "main")
    (tmp_path / "unrelated.cpp").write_text("noise\n")
    git("add", "-A")
    git("commit", "-qm", "unrelated")
    git("checkout", "-q", "topic")
    return tmp_path


def test_diff_ranges_uses_the_merge_base(tiny_repo):
    diffs = diff_ranges(tiny_repo, "main", "topic")
    assert set(diffs) == {"a.cpp"}, "a later commit on main leaked into the diff"
    assert diffs["a.cpp"].ranges == [(2, 2)]
    assert diffs["a.cpp"].added_lines == {2: "CHANGED"}


def test_diff_ranges_against_the_worktree_includes_untracked(tiny_repo):
    """`git diff` reports only tracked files, so a brand-new transactor source
    would be invisible until it was staged."""
    (tiny_repo / "b.cpp").write_text("x\ny\n")
    (tiny_repo / "a.cpp").write_text("one\ntwo\nEDITED\n")
    diffs = diff_ranges(tiny_repo, "main")
    assert "b.cpp" in diffs
    assert diffs["b.cpp"].ranges == [(1, 2)]
    assert diffs["b.cpp"].added_lines == {1: "x", 2: "y"}
    assert diffs["a.cpp"].ranges == [(3, 3)]


@pytest.mark.parametrize("setting", ["diff.mnemonicPrefix", "diff.noprefix"])
def test_diff_ranges_survives_prefix_config(tiny_repo, setting):
    """These configs only affect the *worktree* diff, so the commit-to-commit
    form cannot detect them — an earlier version of this test used that form and
    passed with the overrides removed. With `diff.mnemonicPrefix` git writes
    `+++ w/a.cpp`; unstripped, every path fails to match the graph and the tool
    reports zero touched nodes with no error."""
    subprocess.run(["git", "-C", str(tiny_repo), "config", setting, "true"], check=True)
    (tiny_repo / "a.cpp").write_text("one\ntwo\nEDITED\n")
    assert set(diff_ranges(tiny_repo, "main")) == {"a.cpp"}


def test_merge_base_and_resolve_rev(tiny_repo):
    base = merge_base(tiny_repo, "main", "topic")
    assert len(base) == 40
    assert resolve_rev(tiny_repo, "topic") != base


def test_git_failure_is_loud(tmp_path):
    (tmp_path / "notrepo").mkdir()
    with pytest.raises(RuntimeError, match="git .* failed"):
        merge_base(tmp_path / "notrepo", "main", "HEAD")


# --- end-to-end CLI ---------------------------------------------------------


def test_pr_map_main_writes_a_valid_report(tmp_path, libclang_dylib, build_dir):
    out = tmp_path / "out"
    assert bg.main(["--build-dir", str(build_dir), "--out", str(out)]) == 0
    rc = pr_map.main(
        [
            "--graph",
            str(out / "graph.json"),
            "--base",
            "HEAD",
            "--out",
            str(out / "touched.json"),
        ]
    )
    assert rc == 0
    report = json.loads((out / "touched.json").read_text())
    pr_map.validate_report(report, TOOL_DIR / "touched.schema.json")
    assert report["schema"] == 1
