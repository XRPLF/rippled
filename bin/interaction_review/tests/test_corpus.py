from __future__ import annotations

from functools import cache
import json
from pathlib import Path
import re
import subprocess

TOOL_ROOT = Path(__file__).resolve().parents[1]
REPO_ROOT = Path(__file__).resolve().parents[3]
MANIFEST_PATH = TOOL_ROOT / "examples" / "corpus" / "manifest.json"

COMMIT_RE = re.compile(r"^[0-9a-f]{40}$")
FEATURE_ID_RE = re.compile(r"^feature:(amendment|transactor):([^:]+)$")
RESOURCE_KINDS = {"fork", "invariant", "shared_sfield"}


def _manifest() -> dict:
    return json.loads(MANIFEST_PATH.read_text())


@cache
def _git(*args: str) -> str:
    return subprocess.run(
        ["git", *args],
        cwd=REPO_ROOT,
        check=True,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    ).stdout.rstrip("\n")


def _tree_commit(case: dict) -> str:
    provenance = case["provenance"]
    if case["kind"] == "historical_bug":
        return provenance["fix_commit"]
    return provenance["head_commit"]


def test_corpus_has_historical_bugs_and_clean_controls() -> None:
    manifest = _manifest()
    assert manifest["schema"] == 1
    assert manifest["replay_contract"]["oracle_isolation"]

    cases = manifest["cases"]
    ids = [case["id"] for case in cases]
    assert len(ids) == len(set(ids))
    assert all(re.fullmatch(r"[a-z0-9_]+", case_id) for case_id in ids)

    historical = [case for case in cases if case["kind"] == "historical_bug"]
    controls = [case for case in cases if case["kind"] == "synthetic_control"]
    assert len(historical) >= 4
    assert len(controls) >= 2

    for case in historical:
        assert case["expected_judgement"] == {
            "behavior": "broken",
            "coverage": "covered",
            "verdict": "gap",
        }
    for case in controls:
        assert case["expected_judgement"] == {
            "behavior": "correct",
            "coverage": "covered",
            "verdict": "handled",
        }


def test_targets_use_canonical_feature_ids_and_typed_resources() -> None:
    for case in _manifest()["cases"]:
        target = case["target"]
        match_mode = target.get("match_mode", "pair_location")
        assert match_mode in {"pair_location", "invariant_route"}
        features = target["features"]
        assert len(features) == 2
        assert len({feature["id"] for feature in features}) == 2

        for feature in [*features, *target.get("context_features", [])]:
            match = FEATURE_ID_RE.fullmatch(feature["id"])
            assert match, (case["id"], feature)
            assert match.group(2) == feature["name"]

        primary = target["primary_resource"]
        if match_mode == "invariant_route":
            assert primary["kind"] == "invariant"
        resources = target["relevant_resources"]
        assert primary["kind"] in RESOURCE_KINDS
        assert primary in resources
        assert resources
        assert all(resource["kind"] in RESOURCE_KINDS for resource in resources)
        assert all(resource["name"] for resource in resources)

        routing = case["routing"]
        assert routing["expectation"] in {
            "must_route",
            "stretch_route",
            "ambiguity_stress",
        }
        assert routing["why"]
        assert routing["signals"]


def test_feature_names_exist_in_protocol_macros() -> None:
    feature_macro = (
        REPO_ROOT / "include/xrpl/protocol/detail/features.macro"
    ).read_text()
    transaction_macro = (
        REPO_ROOT / "include/xrpl/protocol/detail/transactions.macro"
    ).read_text()
    amendments = set(
        re.findall(r"XRPL_(?:FEATURE|FIX)\s*\(\s*([A-Za-z0-9_]+)", feature_macro)
    )
    transactors = set(
        re.findall(
            r"TRANSACTION\s*\(\s*[^,]+,\s*[^,]+,\s*([A-Za-z0-9_]+)",
            transaction_macro,
        )
    )

    for case in _manifest()["cases"]:
        target = case["target"]
        for feature in [
            *target["features"],
            *target.get("context_features", []),
        ]:
            kind = feature["id"].split(":", 2)[1]
            catalog = amendments if kind == "amendment" else transactors
            assert feature["name"] in catalog, (case["id"], feature)


def test_provenance_commits_are_direct_windows_with_matching_subjects() -> None:
    for case in _manifest()["cases"]:
        provenance = case["provenance"]
        if case["kind"] == "historical_bug":
            base = provenance["buggy_commit"]
            head = provenance["fix_commit"]
            assert provenance["source"] == "git_history"
        else:
            base = provenance["base_commit"]
            head = provenance["head_commit"]
            assert provenance["source"] == "local_synthetic_seed"

        assert COMMIT_RE.fullmatch(base)
        assert COMMIT_RE.fullmatch(head)
        assert _git("cat-file", "-t", base) == "commit"
        assert _git("cat-file", "-t", head) == "commit"
        assert _git("rev-parse", f"{head}^") == base
        assert _git("show", "-s", "--format=%s", head) == provenance["subject"]


def test_replay_paths_and_ground_truth_anchors_exist_in_git() -> None:
    for case in _manifest()["cases"]:
        tree_commit = _tree_commit(case)
        replay = case["replay"]
        assert replay["steps"]
        assert all(step["instruction"] for step in replay["steps"])
        current_develop = replay["current_develop"]
        assert current_develop["status"] == "clean"
        assert COMMIT_RE.fullmatch(current_develop["verified_commit"])
        assert _git("cat-file", "-t", current_develop["verified_commit"]) == "commit"
        assert current_develop["note"]

        current_sources = "\n".join(
            _git(
                "show",
                f"{current_develop['verified_commit']}:{path}",
            )
            for path in replay["files"]
        )
        current_tests = "\n".join(
            _git(
                "show",
                f"{current_develop['verified_commit']}:{path}",
            )
            for path in replay["retain_tests"]
        )
        assert current_develop["source_anchor"] in current_sources
        assert current_develop["test_anchor"] in current_tests

        for path in [*replay["files"], *replay["retain_tests"]]:
            assert not Path(path).is_absolute()
            assert _git("cat-file", "-e", f"{tree_commit}:{path}") == ""

        ground_truth = case["ground_truth"]
        assert ground_truth["source_citations"]
        assert ground_truth["tests"]
        for citation in [*ground_truth["source_citations"], *ground_truth["tests"]]:
            commit = citation["commit"]
            path = citation["path"]
            assert COMMIT_RE.fullmatch(commit)
            assert not Path(path).is_absolute()
            contents = _git("show", f"{commit}:{path}").splitlines()
            line = citation["line"]
            assert 1 <= line <= len(contents)
            assert citation["anchor"] in contents[line - 1], (
                case["id"],
                path,
                line,
                contents[line - 1],
            )


def test_each_case_has_distinct_pair_and_resource_coverage() -> None:
    cases = _manifest()["cases"]
    historical = [case for case in cases if case["kind"] == "historical_bug"]

    pairs = {
        tuple(sorted(feature["id"] for feature in case["target"]["features"]))
        for case in historical
    }
    primary_resources = {
        (
            case["target"]["primary_resource"]["kind"],
            case["target"]["primary_resource"]["name"],
        )
        for case in historical
    }

    assert len(pairs) >= 4
    assert len(primary_resources) >= 4
    assert {resource[0] for resource in primary_resources} >= {"fork", "invariant"}
