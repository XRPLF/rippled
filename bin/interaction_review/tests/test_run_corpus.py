from __future__ import annotations

from io import StringIO
import json
from pathlib import Path
from typing import Sequence

import pytest

import run_corpus as rc

MANIFEST = rc.HERE / "examples" / "corpus" / "manifest.json"


def _cases() -> list[dict]:
    return rc.load_manifest(MANIFEST)["cases"]


def test_every_machine_mutation_validates_without_a_checkout(repo_root: Path) -> None:
    executor = rc.SubprocessExecutor()
    validations = [rc.validate_case(case, repo_root, executor) for case in _cases()]

    assert len(validations) >= 7
    assert {validation["id"] for validation in validations} == {
        case["id"] for case in _cases()
    }
    assert all(validation["files"] for validation in validations)
    assert all(validation["retained_tests"] for validation in validations)


def test_oracle_is_primary_cluster_only_and_wraps_expected_values() -> None:
    case = next(
        case for case in _cases() if case["id"] == "historical_batch_delegate_consent"
    )

    oracle = rc.oracle_for_case(case)

    assert oracle["cases"] == [
        {
            "id": "historical_batch_delegate_consent",
            "resource_kind": "fork",
            "resource": "checkSign",
            "features": ["Batch", "PermissionDelegationV1_1"],
            "feature_ids": [
                "feature:transactor:Batch",
                "feature:amendment:PermissionDelegationV1_1",
            ],
            "expected_judgement": {
                "behavior": ["broken"],
                "coverage": ["covered"],
                "verdict": ["gap"],
            },
            "expected_localization": {
                "primary": {"resource_kind": "fork", "resource": "checkSign"},
                "relevant": [
                    {"resource_kind": "fork", "resource": "checkSign"},
                    {
                        "resource_kind": "fork",
                        "resource": "checkPermission",
                    },
                    {"resource_kind": "fork", "resource": "preflight1"},
                ],
            },
        }
    ]
    assert "ground_truth" not in json.dumps(oracle)


def test_invariant_oracle_is_labeled_as_a_route_not_exact_pair_recall() -> None:
    case = next(
        case
        for case in _cases()
        if case["id"] == "historical_ammclawback_deepfreeze_override"
    )

    oracle = rc.oracle_for_case(case)

    assert oracle["cases"][0]["match_mode"] == "invariant_route"
    assert oracle["cases"][0]["resource_kind"] == "invariant"


def test_subset_selection_preserves_manifest_order() -> None:
    manifest = rc.load_manifest(MANIFEST)
    selected = rc.select_cases(
        manifest,
        case_ids=(
            "control_named_sponsor_presence_predicate",
            "historical_batch_delegate_consent",
        ),
    )
    assert [case["id"] for case in selected] == [
        "historical_batch_delegate_consent",
        "control_named_sponsor_presence_predicate",
    ]

    controls = rc.select_cases(manifest, kinds=("synthetic_control",))
    assert controls
    assert all(case["kind"] == "synthetic_control" for case in controls)

    with pytest.raises(rc.CorpusError, match="unknown corpus case"):
        rc.select_cases(manifest, case_ids=("does_not_exist",))

    unsafe = json.loads(json.dumps(manifest))
    unsafe["cases"][0]["id"] = "../../cfg"
    with pytest.raises(rc.CorpusError, match="lowercase letters"):
        rc.select_cases(unsafe)


def test_static_plan_has_commands_and_artifacts_but_no_judge(tmp_path: Path) -> None:
    case = _cases()[0]
    plan = rc.build_plan(
        [case],
        repo_root=rc.REPO_ROOT,
        tool_root=rc.HERE,
        run_root=tmp_path / "run",
        source_build_dir=rc.REPO_ROOT / ".build",
        python=Path("/test/python"),
        budget=6,
        with_model=False,
        aws_region=None,
        aws_profile=None,
        model=None,
        effort="high",
    )

    planned = plan["cases"][0]
    assert [command["stage"] for command in planned["commands"]] == [
        "build_graph",
        "map_diff",
        "select",
        "render",
        "evaluate",
    ]
    assert "judge_interactions.py" not in json.dumps(plan)
    assert planned["artifacts"]["selected"].endswith("/static/selected.json")
    assert planned["artifacts"]["oracle"].endswith("/oracle.json")
    select_command = next(
        command for command in planned["commands"] if command["stage"] == "select"
    )
    assert select_command["argv"][
        select_command["argv"].index("--repo-root") + 1
    ] == str(tmp_path / "run" / "worktrees" / case["id"])
    assert plan["aggregate_artifacts"]["markdown"].endswith("/corpus-summary.md")
    assert plan["model_calls"] is False


def test_model_command_exists_only_after_explicit_opt_in(tmp_path: Path) -> None:
    plan = rc.build_plan(
        [_cases()[0]],
        repo_root=rc.REPO_ROOT,
        tool_root=rc.HERE,
        run_root=tmp_path / "run",
        source_build_dir=rc.REPO_ROOT / ".build",
        python=Path("/test/python"),
        budget=4,
        with_model=True,
        aws_region="us-east-1",
        aws_profile="demo",
        model="anthropic.test",
        effort="xhigh",
    )

    commands = plan["cases"][0]["commands"]
    judge = next(command for command in commands if command["stage"] == "judge")
    assert "--aws-region" in judge["argv"]
    assert "--aws-profile" in judge["argv"]
    assert "--model" in judge["argv"]
    assert "--max-items" in judge["argv"]
    assert plan["model_calls"] is True


def test_compile_database_retargets_source_but_keeps_build_outputs(
    tmp_path: Path,
) -> None:
    source_repo = tmp_path / "source"
    source_build = source_repo / ".build"
    destination = tmp_path / "copy"
    worktree = tmp_path / "worktree"
    source_build.mkdir(parents=True)
    database = [
        {
            "directory": str(source_build),
            "command": (
                f"clang++ -I{source_build}/generated -I{source_repo}/include "
                f"-c {source_repo}/src/libxrpl/tx/Transactor.cpp"
            ),
            "file": str(source_repo / "src/libxrpl/tx/Transactor.cpp"),
        }
    ]
    (source_build / "compile_commands.json").write_text(json.dumps(database))

    rc.prepare_compile_database(source_build, destination, source_repo, worktree)

    rewritten = json.loads((destination / "compile_commands.json").read_text())[0]
    assert rewritten["arguments"][1:3] == [
        f"-I{worktree / 'include'}",
        f"-I{worktree / 'src'}",
    ]
    assert f"-I{source_build}/generated" in rewritten["arguments"]
    assert f"-I{worktree}/include" in rewritten["arguments"]
    assert rewritten["file"] == str(worktree / "src/libxrpl/tx/Transactor.cpp")
    assert "command" not in rewritten


def test_aggregate_summary_keeps_core_and_stretch_routing_separate(
    tmp_path: Path,
) -> None:
    core = _cases()[0]
    stretch = next(
        case for case in _cases() if case["routing"]["expectation"] == "stretch_route"
    )
    core_path = rc.case_paths(tmp_path, core["id"]).evaluation_json
    core_path.parent.mkdir(parents=True)
    core_path.write_text(
        json.dumps(
            {
                "cases": [
                    {
                        "id": core["id"],
                        "cluster_rank": 1,
                        "resource_row_rank": 3,
                        "within_cluster_budget": True,
                        "judge_status": "not_judged",
                        "localization": {"status": "not_judged"},
                    }
                ]
            }
        )
    )

    metrics = rc.aggregate_evaluations([core, stretch], tmp_path)

    assert metrics["hits"] == 1
    assert metrics["recall_at_budget"] == 0.5
    assert metrics["routing"] == {
        "must_route": {"cases": 1, "hits": 1, "recall_at_budget": 1.0},
        "stretch_route": {"cases": 1, "hits": 0, "recall_at_budget": 0.0},
    }
    markdown = rc.format_corpus_summary(
        metrics,
        6,
        {case["id"]: case["title"] for case in (core, stretch)},
    )
    assert "Cluster Recall@6" in markdown
    assert core["title"] in markdown
    assert stretch["title"] in markdown
    assert "Required route" in markdown
    assert "Extended route" in markdown
    assert "Model not run" in markdown
    assert "Primary location" in markdown
    assert "must_route" not in markdown
    assert "stretch_route" not in markdown
    assert "not_judged" not in markdown
    assert "Localization" not in markdown


def test_corpus_summary_names_the_ranking_stress_case(tmp_path: Path) -> None:
    stress = next(
        case
        for case in _cases()
        if case["routing"]["expectation"] == "ambiguity_stress"
    )
    metrics = rc.aggregate_evaluations([stress], tmp_path)

    markdown = rc.format_corpus_summary(
        metrics,
        6,
        {stress["id"]: stress["title"]},
    )

    assert stress["title"] in markdown
    assert "Ranking stress case" in markdown
    assert "Not available" in markdown
    assert stress["id"] not in markdown
    assert "ambiguity_stress" not in markdown


class FakeExecutor:
    def __init__(self, changed_path: str) -> None:
        self.changed_path = changed_path
        self.calls: list[tuple[list[str], Path, str | None]] = []

    def run(
        self,
        argv: Sequence[str],
        *,
        cwd: Path,
        input_text: str | None = None,
    ) -> rc.CommandResult:
        command = list(argv)
        self.calls.append((command, cwd, input_text))
        if command[:3] == ["git", "worktree", "add"]:
            Path(command[-2]).mkdir(parents=True)
        if command[:3] == ["git", "diff", "--name-only"]:
            return rc.CommandResult(stdout=f"{self.changed_path}\n")
        return rc.CommandResult(stdout=f"ran {command[0]}\n")


def test_execute_uses_disposable_worktree_and_static_commands_only(
    tmp_path: Path,
) -> None:
    case = _cases()[0]
    paths = rc.case_paths(tmp_path / "run", case["id"])
    executor = FakeExecutor(case["replay"]["files"][0])

    def fake_mutation(
        replay_case: dict,
        repo_root: Path,
        worktree: Path,
        command_executor: rc.CommandExecutor,
    ) -> None:
        assert replay_case is case
        assert worktree == paths.worktree

    def fake_compile_db(
        source: Path, destination: Path, repo_root: Path, worktree: Path
    ) -> None:
        destination.mkdir(parents=True)
        (destination / "compile_commands.json").write_text("[]")

    result = rc.execute_case(
        case,
        paths=paths,
        repo_root=rc.REPO_ROOT,
        tool_root=rc.HERE,
        source_build_dir=rc.REPO_ROOT / ".build",
        python=Path("/test/python"),
        budget=6,
        with_model=False,
        aws_region=None,
        aws_profile=None,
        model=None,
        effort="high",
        keep_worktree=False,
        executor=executor,
        mutation_applier=fake_mutation,
        compile_db_preparer=fake_compile_db,
    )

    commands = [call[0] for call in executor.calls]
    assert result["status"] == "complete"
    assert ["git", "worktree", "add"] == commands[0][:3]
    assert ["git", "worktree", "remove"] == commands[-1][:3]
    assert not any(
        command[:2] in (["git", "switch"], ["git", "checkout"], ["git", "reset"])
        for command in commands
    )
    assert not any("judge_interactions.py" in " ".join(command) for command in commands)
    assert not any("cfg/" in " ".join(command) for command in commands)
    assert paths.oracle.is_file()
    assert paths.result.is_file()
    run_doc = json.loads(paths.result.read_text())
    assert run_doc["status"] == "complete"
    assert [stage["stage"] for stage in run_doc["stages"]] == [
        "build_graph",
        "map_diff",
        "select",
        "render",
        "evaluate",
    ]


def test_default_cli_plan_creates_no_run_directory(
    tmp_path: Path, repo_root: Path, build_dir: Path
) -> None:
    proposed = tmp_path / "not-created"
    output = StringIO()

    status = rc.main(
        [
            "--repo-root",
            str(repo_root),
            "--build-dir",
            str(build_dir),
            "--run-root",
            str(proposed),
            "--case",
            "historical_batch_delegate_consent",
        ],
        stdout=output,
    )

    assert status == 0
    assert not proposed.exists()
    plan = json.loads(output.getvalue())
    assert plan["model_calls"] is False
    assert plan["validation"][0]["id"] == "historical_batch_delegate_consent"


def test_manifest_cannot_mutate_cfg(repo_root: Path) -> None:
    case = json.loads(json.dumps(_cases()[0]))
    case["replay"]["files"] = ["cfg/xrpld-standalone.cfg"]
    case["machine_mutation"]["operations"][0]["paths"] = ["cfg/xrpld-standalone.cfg"]

    with pytest.raises(rc.CorpusError, match="may not target cfg"):
        rc.validate_case(case, repo_root, rc.SubprocessExecutor())
