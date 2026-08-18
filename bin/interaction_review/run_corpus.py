#!/usr/bin/env python3
"""Plan or execute isolated interaction-review corpus replays.

The default mode is deliberately read-only: validate the manifest against the
local Git object database and print the exact worktrees, commands, and artifact
paths that an execution would use.  ``--mode run`` is the only mode that
creates anything.  It uses detached disposable worktrees and never switches or
resets the caller's checkout.

Live judging is a second, independent opt-in.  ``--with-model`` is required in
addition to ``--mode run``; tests and ordinary corpus runs execute only the
deterministic static pipeline.
"""

from __future__ import annotations

import argparse
from dataclasses import asdict, dataclass
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile
from typing import Any, Callable, Protocol, Sequence, TextIO

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parents[1]
DEFAULT_MANIFEST = HERE / "examples" / "corpus" / "manifest.json"
PLAN_ROOT = Path(tempfile.gettempdir()) / "interaction-review-corpus-plan"

CASE_KINDS = {"historical_bug", "synthetic_control"}
CASE_ID_RE = re.compile(r"^[a-z0-9_]+$")
MUTATION_OPERATIONS = {"apply_git_diff", "replace_text"}
PIPELINE_SCRIPTS = {
    "build_graph.py",
    "pr_map.py",
    "select_interactions.py",
    "judge_interactions.py",
    "evaluate_run.py",
    "render_comment.py",
}


class CorpusError(ValueError):
    """A replay cannot proceed safely and reproducibly."""


@dataclass(frozen=True)
class CommandResult:
    stdout: str = ""
    stderr: str = ""
    returncode: int = 0


class CommandExecutor(Protocol):
    """Small injected command boundary used by the runner and its tests."""

    def run(
        self,
        argv: Sequence[str],
        *,
        cwd: Path,
        input_text: str | None = None,
    ) -> CommandResult: ...


class SubprocessExecutor:
    def run(
        self,
        argv: Sequence[str],
        *,
        cwd: Path,
        input_text: str | None = None,
    ) -> CommandResult:
        try:
            process = subprocess.run(
                list(argv),
                cwd=cwd,
                input=input_text,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )
        except OSError as error:
            raise CorpusError(
                f"could not run {_display_command(argv)}: {error}"
            ) from error
        result = CommandResult(
            stdout=process.stdout,
            stderr=process.stderr,
            returncode=process.returncode,
        )
        if process.returncode:
            detail = process.stderr.strip() or process.stdout.strip() or "no output"
            raise CorpusError(
                f"command failed ({process.returncode}): {_display_command(argv)}\n{detail}"
            )
        return result


@dataclass(frozen=True)
class CasePaths:
    worktree: Path
    artifacts: Path
    compile_db: Path
    graph: Path
    interactions: Path
    touched: Path
    selected: Path
    judged: Path
    oracle: Path
    evaluation_json: Path
    evaluation_markdown: Path
    comment: Path
    result: Path


def _display_command(argv: Sequence[str]) -> str:
    return shlex.join(str(part) for part in argv)


def _is_within(path: Path, parent: Path) -> bool:
    try:
        path.relative_to(parent)
        return True
    except ValueError:
        return False


def _safe_repo_path(raw: Any, where: str) -> str:
    if not isinstance(raw, str) or not raw:
        raise CorpusError(f"{where} must be a non-empty repository-relative path")
    path = Path(raw)
    if path.is_absolute() or ".." in path.parts:
        raise CorpusError(f"{where} escapes the repository: {raw!r}")
    if path.parts and path.parts[0] in {".git", "cfg"}:
        raise CorpusError(f"{where} may not target {path.parts[0]}/: {raw!r}")
    return path.as_posix()


def load_manifest(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except OSError as error:
        raise CorpusError(f"could not read corpus manifest {path}: {error}") from error
    except json.JSONDecodeError as error:
        raise CorpusError(
            f"corpus manifest {path} is not valid JSON: {error}"
        ) from error
    if not isinstance(value, dict) or value.get("schema") != 1:
        raise CorpusError("corpus manifest must be a schema-1 JSON object")
    cases = value.get("cases")
    if not isinstance(cases, list) or not cases:
        raise CorpusError("corpus manifest must contain at least one case")
    return value


def select_cases(
    manifest: dict[str, Any],
    case_ids: Sequence[str] = (),
    kinds: Sequence[str] = (),
) -> list[dict[str, Any]]:
    cases = manifest["cases"]
    by_id: dict[str, dict[str, Any]] = {}
    for index, case in enumerate(cases):
        if not isinstance(case, dict):
            raise CorpusError(f"cases[{index}] must be an object")
        case_id = case.get("id")
        if not isinstance(case_id, str) or not CASE_ID_RE.fullmatch(case_id):
            raise CorpusError(
                f"cases[{index}].id must contain only lowercase letters, digits, and underscores"
            )
        if case_id in by_id:
            raise CorpusError(f"duplicate corpus case id: {case_id}")
        by_id[case_id] = case

    unknown_ids = sorted(set(case_ids) - set(by_id))
    if unknown_ids:
        raise CorpusError(f"unknown corpus case(s): {', '.join(unknown_ids)}")
    unknown_kinds = sorted(set(kinds) - CASE_KINDS)
    if unknown_kinds:
        raise CorpusError(f"unknown corpus kind(s): {', '.join(unknown_kinds)}")

    selected = [
        case
        for case in cases
        if (not case_ids or case["id"] in case_ids)
        and (not kinds or case.get("kind") in kinds)
    ]
    if not selected:
        raise CorpusError("case filters selected no corpus cases")
    return selected


def case_base(case: dict[str, Any]) -> str:
    machine_mutation = case.get("machine_mutation", {})
    configured = machine_mutation.get("base")
    if configured is not None:
        if configured == "replay.current_develop.verified_commit":
            configured = (
                case.get("replay", {}).get("current_develop", {}).get("verified_commit")
            )
        if not isinstance(configured, str) or len(configured) != 40:
            raise CorpusError(
                f"{case.get('id')}: machine_mutation.base must resolve to a full commit hash"
            )
        return configured

    provenance = case.get("provenance", {})
    key = "fix_commit" if case.get("kind") == "historical_bug" else "base_commit"
    value = provenance.get(key)
    if not isinstance(value, str) or len(value) != 40:
        raise CorpusError(
            f"{case.get('id')}: provenance.{key} must be a full commit hash"
        )
    return value


def case_paths(run_root: Path, case_id: str) -> CasePaths:
    artifacts = run_root / "artifacts" / case_id
    static = artifacts / "static"
    return CasePaths(
        worktree=run_root / "worktrees" / case_id,
        artifacts=artifacts,
        compile_db=artifacts / "compile-db",
        graph=static / "graph.json",
        interactions=static / "interactions.json",
        touched=static / "touched.json",
        selected=static / "selected.json",
        judged=artifacts / "judged.json",
        oracle=artifacts / "oracle.json",
        evaluation_json=artifacts / "evaluation.json",
        evaluation_markdown=artifacts / "evaluation.md",
        comment=artifacts / "comment.md",
        result=artifacts / "run.json",
    )


def oracle_for_case(case: dict[str, Any]) -> dict[str, Any]:
    target = case["target"]
    primary = target["primary_resource"]
    expected = case["expected_judgement"]
    return {
        "schema": 1,
        "name": case["title"],
        "description": (
            "Corpus oracle for post-judgement evaluation. This file is stored "
            "outside the replay worktree and is never passed to the judge."
        ),
        "cases": [
            {
                "id": case["id"],
                "resource_kind": primary["kind"],
                "resource": primary["name"],
                **(
                    {"match_mode": target["match_mode"]}
                    if target.get("match_mode")
                    else {}
                ),
                "features": [feature["name"] for feature in target["features"]],
                "feature_ids": [feature["id"] for feature in target["features"]],
                "expected_judgement": {
                    field: [value] for field, value in expected.items()
                },
                "expected_localization": {
                    "primary": {
                        "resource_kind": primary["kind"],
                        "resource": primary["name"],
                    },
                    "relevant": [
                        {
                            "resource_kind": resource["kind"],
                            "resource": resource["name"],
                        }
                        for resource in target["relevant_resources"]
                    ],
                },
            }
        ],
    }


def _resolve_operation_ref(case: dict[str, Any], value: Any, where: str) -> str:
    if not isinstance(value, str) or not value:
        raise CorpusError(f"{case['id']}: {where} must be a non-empty string")
    prefix = "provenance."
    if value.startswith(prefix):
        key = value[len(prefix) :]
        resolved = case["provenance"].get(key)
        if not isinstance(resolved, str):
            raise CorpusError(f"{case['id']}: {value} does not resolve to a string")
        return resolved
    return value


def _mutation_operations(case: dict[str, Any]) -> list[dict[str, Any]]:
    mutation = case.get("machine_mutation")
    if not isinstance(mutation, dict):
        raise CorpusError(f"{case['id']}: missing machine_mutation object")
    operations = mutation.get("operations")
    if not isinstance(operations, list) or not operations:
        raise CorpusError(
            f"{case['id']}: machine_mutation.operations must not be empty"
        )
    return operations


def _git(executor: CommandExecutor, repo_root: Path, *args: str) -> str:
    return executor.run(["git", *args], cwd=repo_root).stdout.rstrip("\n")


def validate_case(
    case: dict[str, Any], repo_root: Path, executor: CommandExecutor
) -> dict[str, Any]:
    """Read-only validation, including exact mutation applicability in memory."""
    case_id = case.get("id", "<unknown>")
    if case.get("kind") not in CASE_KINDS:
        raise CorpusError(f"{case_id}: unsupported kind {case.get('kind')!r}")
    base = case_base(case)
    if _git(executor, repo_root, "cat-file", "-t", base) != "commit":
        raise CorpusError(f"{case_id}: replay base is not a commit: {base}")

    replay = case.get("replay")
    if not isinstance(replay, dict):
        raise CorpusError(f"{case_id}: replay must be an object")
    replay_files = {
        _safe_repo_path(path, f"{case_id}.replay.files")
        for path in replay.get("files", [])
    }
    if not replay_files:
        raise CorpusError(f"{case_id}: replay.files must not be empty")
    retained = {
        _safe_repo_path(path, f"{case_id}.replay.retain_tests")
        for path in replay.get("retain_tests", [])
    }
    if replay_files & retained:
        raise CorpusError(f"{case_id}: production mutation overlaps retained tests")

    # Track exact file contents without creating a checkout. A whole-file Git
    # diff moves the named paths to one recorded tree; later text replacements
    # are then verified against that result.
    virtual = {
        path: _git(executor, repo_root, "show", f"{base}:{path}") + "\n"
        for path in replay_files
    }
    changed: set[str] = set()
    operations = _mutation_operations(case)
    for index, operation in enumerate(operations):
        where = f"{case_id}.machine_mutation.operations[{index}]"
        if (
            not isinstance(operation, dict)
            or operation.get("operation") not in MUTATION_OPERATIONS
        ):
            raise CorpusError(f"{where} has an unsupported operation")
        kind = operation["operation"]
        if kind == "apply_git_diff":
            from_ref = _resolve_operation_ref(
                case, operation.get("from"), f"{where}.from"
            )
            to_ref = _resolve_operation_ref(case, operation.get("to"), f"{where}.to")
            direction = operation.get("direction")
            if direction not in {"forward", "reverse"}:
                raise CorpusError(f"{where}.direction must be forward or reverse")
            paths = {
                _safe_repo_path(path, f"{where}.paths")
                for path in operation.get("paths", [])
            }
            if not paths or not paths <= replay_files:
                raise CorpusError(
                    f"{where}.paths must be a non-empty subset of replay.files"
                )
            expected_base = from_ref if direction == "forward" else to_ref
            target = to_ref if direction == "forward" else from_ref
            if expected_base != base:
                raise CorpusError(
                    f"{where} expects worktree {expected_base}, but case base is {base}"
                )
            patch = _git(
                executor,
                repo_root,
                "diff",
                "--binary",
                "--no-ext-diff",
                from_ref,
                to_ref,
                "--",
                *sorted(paths),
            )
            if not patch:
                raise CorpusError(f"{where} resolves to an empty Git diff")
            for path in paths:
                virtual[path] = (
                    _git(executor, repo_root, "show", f"{target}:{path}") + "\n"
                )
            changed |= paths
        else:
            path = _safe_repo_path(operation.get("path"), f"{where}.path")
            if path not in replay_files:
                raise CorpusError(f"{where}.path is not listed in replay.files")
            old = operation.get("old")
            new = operation.get("new")
            count = operation.get("count", 1)
            if not isinstance(old, str) or not isinstance(new, str) or old == new:
                raise CorpusError(f"{where} requires different string old/new values")
            if not isinstance(count, int) or isinstance(count, bool) or count < 1:
                raise CorpusError(f"{where}.count must be a positive integer")
            actual = virtual[path].count(old)
            if actual != count:
                raise CorpusError(
                    f"{where} expected {count} exact match(es), found {actual}"
                )
            virtual[path] = virtual[path].replace(old, new, count)
            changed.add(path)

    if changed != replay_files:
        missing = ", ".join(sorted(replay_files - changed))
        raise CorpusError(
            f"{case_id}: machine mutation leaves replay file(s) untouched: {missing}"
        )
    return {
        "id": case_id,
        "base": base,
        "files": sorted(replay_files),
        "retained_tests": sorted(retained),
        "operations": len(operations),
    }


def _pipeline_commands(
    case: dict[str, Any],
    paths: CasePaths,
    *,
    tool_root: Path,
    python: Path,
    budget: int,
    with_model: bool,
    aws_region: str | None,
    aws_profile: str | None,
    model: str | None,
    effort: str,
) -> list[tuple[str, list[str], Path]]:
    base = case_base(case)
    commands: list[tuple[str, list[str], Path]] = [
        (
            "build_graph",
            [
                str(python),
                str(tool_root / "build_graph.py"),
                "--build-dir",
                str(paths.compile_db),
                "--repo-root",
                str(paths.worktree),
                "--out",
                str(paths.graph.parent),
            ],
            paths.worktree,
        ),
        (
            "map_diff",
            [
                str(python),
                str(tool_root / "pr_map.py"),
                "--graph",
                str(paths.graph),
                "--base",
                base,
                "--repo-root",
                str(paths.worktree),
                "--out",
                str(paths.touched),
            ],
            paths.worktree,
        ),
        (
            "select",
            [
                str(python),
                str(tool_root / "select_interactions.py"),
                "--graph",
                str(paths.graph),
                "--interactions",
                str(paths.interactions),
                "--touched",
                str(paths.touched),
                "--repo-root",
                str(paths.worktree),
                "--out",
                str(paths.selected),
            ],
            paths.worktree,
        ),
    ]
    report_input = paths.selected
    if with_model:
        judge = [
            str(python),
            str(tool_root / "judge_interactions.py"),
            "--selected",
            str(paths.selected),
            "--out",
            str(paths.judged),
            "--repo-root",
            str(paths.worktree),
            "--aws-region",
            str(aws_region),
            "--max-items",
            str(budget),
            "--effort",
            effort,
        ]
        if aws_profile:
            judge += ["--aws-profile", aws_profile]
        if model:
            judge += ["--model", model]
        commands.append(("judge", judge, paths.worktree))
        report_input = paths.judged

    evaluate = [
        str(python),
        str(tool_root / "evaluate_run.py"),
        "--selected",
        str(paths.selected),
        "--oracle",
        str(paths.oracle),
        "--budget",
        str(budget),
        "--markdown-out",
        str(paths.evaluation_markdown),
        "--json-out",
        str(paths.evaluation_json),
    ]
    if with_model:
        evaluate += ["--judged", str(paths.judged)]
    commands += [
        (
            "render",
            [
                str(python),
                str(tool_root / "render_comment.py"),
                "--selected",
                str(report_input),
                "--out",
                str(paths.comment),
            ],
            paths.worktree,
        ),
        ("evaluate", evaluate, paths.worktree),
    ]
    return commands


def build_plan(
    cases: Sequence[dict[str, Any]],
    *,
    repo_root: Path,
    tool_root: Path,
    run_root: Path,
    source_build_dir: Path,
    python: Path,
    budget: int,
    with_model: bool,
    aws_region: str | None,
    aws_profile: str | None,
    model: str | None,
    effort: str,
) -> dict[str, Any]:
    planned: list[dict[str, Any]] = []
    for case in cases:
        paths = case_paths(run_root, case["id"])
        commands = _pipeline_commands(
            case,
            paths,
            tool_root=tool_root,
            python=python,
            budget=budget,
            with_model=with_model,
            aws_region=aws_region,
            aws_profile=aws_profile,
            model=model,
            effort=effort,
        )
        planned.append(
            {
                "id": case["id"],
                "kind": case["kind"],
                "base": case_base(case),
                "worktree": str(paths.worktree),
                "artifacts": {
                    key: str(value)
                    for key, value in asdict(paths).items()
                    if key not in {"worktree", "artifacts", "compile_db"}
                },
                "compile_database": {
                    "source": str(source_build_dir / "compile_commands.json"),
                    "rewritten_copy": str(paths.compile_db / "compile_commands.json"),
                },
                "mutation": case["machine_mutation"],
                "commands": [
                    {
                        "stage": stage,
                        "cwd": str(cwd),
                        "argv": argv,
                        "display": _display_command(argv),
                    }
                    for stage, argv, cwd in commands
                ],
            }
        )
    return {
        "schema": 1,
        "mode": "plan",
        "pipeline": "model" if with_model else "static",
        "repository": str(repo_root),
        "run_root": str(run_root),
        "model_calls": with_model,
        "aggregate_artifacts": {
            "json": str(run_root / "corpus-summary.json"),
            "markdown": str(run_root / "corpus-summary.md"),
        },
        "cases": planned,
    }


def _transform_compile_arg(token: str, old_root: Path, new_root: Path) -> str:
    old = str(old_root)
    # Generated headers and dependency outputs exist only in the configured
    # build tree. Source/include paths must point at the replay worktree.
    if (
        f"{old}{os.sep}.build{os.sep}" in token
        or f"{old}{os.sep}build{os.sep}" in token
    ):
        return token
    return token.replace(old, str(new_root))


def prepare_compile_database(
    source_build_dir: Path,
    destination: Path,
    source_repo: Path,
    worktree: Path,
) -> None:
    source = source_build_dir / "compile_commands.json"
    try:
        database = json.loads(source.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise CorpusError(
            f"could not read compile database {source}: {error}"
        ) from error
    if not isinstance(database, list) or not database:
        raise CorpusError(f"compile database {source} is empty or malformed")

    rewritten: list[dict[str, Any]] = []
    for raw_entry in database:
        if not isinstance(raw_entry, dict):
            raise CorpusError(f"compile database {source} contains a non-object entry")
        entry = dict(raw_entry)
        if "arguments" in entry:
            arguments = list(entry["arguments"])
        elif isinstance(entry.get("command"), str):
            arguments = shlex.split(entry.pop("command"))
        else:
            raise CorpusError(
                f"compile database {source} entry has no command/arguments"
            )
        arguments = [
            _transform_compile_arg(str(token), source_repo, worktree)
            for token in arguments
        ]
        # Put replay source roots before build-module directories, whose
        # symlinks point back at the configured checkout.
        arguments[1:1] = [f"-I{worktree / 'include'}", f"-I{worktree / 'src'}"]
        entry["arguments"] = arguments
        file_value = str(entry.get("file", ""))
        entry["file"] = file_value.replace(str(source_repo), str(worktree))
        rewritten.append(entry)

    destination.mkdir(parents=True, exist_ok=False)
    (destination / "compile_commands.json").write_text(
        json.dumps(rewritten, indent=2) + "\n", encoding="utf-8"
    )


def apply_mutation(
    case: dict[str, Any], repo_root: Path, worktree: Path, executor: CommandExecutor
) -> None:
    for index, operation in enumerate(_mutation_operations(case)):
        kind = operation["operation"]
        if kind == "apply_git_diff":
            from_ref = _resolve_operation_ref(
                case, operation["from"], f"operation[{index}].from"
            )
            to_ref = _resolve_operation_ref(
                case, operation["to"], f"operation[{index}].to"
            )
            paths = [
                _safe_repo_path(path, f"operation[{index}].paths")
                for path in operation["paths"]
            ]
            patch = executor.run(
                [
                    "git",
                    "diff",
                    "--binary",
                    "--no-ext-diff",
                    from_ref,
                    to_ref,
                    "--",
                    *paths,
                ],
                cwd=repo_root,
            ).stdout
            argv = ["git", "apply", "--whitespace=nowarn", "-"]
            if operation["direction"] == "reverse":
                argv.insert(2, "--reverse")
            executor.run(argv, cwd=worktree, input_text=patch)
        else:
            path = worktree / _safe_repo_path(
                operation["path"], f"operation[{index}].path"
            )
            text = path.read_text(encoding="utf-8")
            old = operation["old"]
            expected = operation.get("count", 1)
            actual = text.count(old)
            if actual != expected:
                raise CorpusError(
                    f"{case['id']}: exact replacement in {path} expected "
                    f"{expected} match(es), found {actual}"
                )
            path.write_text(
                text.replace(old, operation["new"], expected), encoding="utf-8"
            )


def _verify_mutated_worktree(
    case: dict[str, Any], worktree: Path, executor: CommandExecutor
) -> None:
    executor.run(["git", "diff", "--check"], cwd=worktree)
    changed = {
        line
        for line in executor.run(
            ["git", "diff", "--name-only", "--"], cwd=worktree
        ).stdout.splitlines()
        if line
    }
    allowed = set(case["replay"]["files"])
    if not changed:
        raise CorpusError(f"{case['id']}: mutation produced no working-tree diff")
    unexpected = changed - allowed
    if unexpected:
        raise CorpusError(
            f"{case['id']}: mutation changed paths outside replay.files: "
            f"{', '.join(sorted(unexpected))}"
        )
    if any(path == "cfg" or path.startswith("cfg/") for path in changed):
        raise CorpusError(f"{case['id']}: mutation attempted to touch cfg/")


MutationApplier = Callable[[dict[str, Any], Path, Path, CommandExecutor], None]
CompileDatabasePreparer = Callable[[Path, Path, Path, Path], None]


def execute_case(
    case: dict[str, Any],
    *,
    paths: CasePaths,
    repo_root: Path,
    tool_root: Path,
    source_build_dir: Path,
    python: Path,
    budget: int,
    with_model: bool,
    aws_region: str | None,
    aws_profile: str | None,
    model: str | None,
    effort: str,
    keep_worktree: bool,
    executor: CommandExecutor,
    mutation_applier: MutationApplier = apply_mutation,
    compile_db_preparer: CompileDatabasePreparer = prepare_compile_database,
) -> dict[str, Any]:
    if paths.worktree.exists() or paths.artifacts.exists():
        raise CorpusError(f"refusing to reuse existing case path for {case['id']}")
    paths.worktree.parent.mkdir(parents=True, exist_ok=True)
    paths.artifacts.parent.mkdir(parents=True, exist_ok=True)

    base = case_base(case)
    worktree_added = False
    stages: list[dict[str, Any]] = []
    status = "failed"
    error: str | None = None
    try:
        executor.run(
            ["git", "worktree", "add", "--detach", str(paths.worktree), base],
            cwd=repo_root,
        )
        worktree_added = True
        if not paths.worktree.is_dir():
            raise CorpusError(f"git did not create replay worktree {paths.worktree}")

        mutation_applier(case, repo_root, paths.worktree, executor)
        _verify_mutated_worktree(case, paths.worktree, executor)
        compile_db_preparer(
            source_build_dir,
            paths.compile_db,
            repo_root,
            paths.worktree,
        )
        paths.artifacts.mkdir(parents=True, exist_ok=True)
        paths.oracle.write_text(
            json.dumps(oracle_for_case(case), indent=2) + "\n", encoding="utf-8"
        )

        for stage, argv, cwd in _pipeline_commands(
            case,
            paths,
            tool_root=tool_root,
            python=python,
            budget=budget,
            with_model=with_model,
            aws_region=aws_region,
            aws_profile=aws_profile,
            model=model,
            effort=effort,
        ):
            script = Path(argv[1]).name if len(argv) > 1 else ""
            if script not in PIPELINE_SCRIPTS:
                raise CorpusError(f"refusing unexpected pipeline command: {argv}")
            result = executor.run(argv, cwd=cwd)
            stages.append(
                {
                    "stage": stage,
                    "argv": argv,
                    "cwd": str(cwd),
                    "stdout": result.stdout,
                    "stderr": result.stderr,
                }
            )
        status = "complete"
    except Exception as caught:
        error = str(caught)
        if isinstance(caught, CorpusError):
            raise
        raise CorpusError(f"{case['id']}: replay failed: {caught}") from caught
    finally:
        if paths.artifacts.exists():
            result_doc = {
                "schema": 1,
                "case": case["id"],
                "status": status,
                "base": base,
                "model_called": with_model,
                "worktree_kept": bool(keep_worktree and worktree_added),
                "error": error,
                "stages": stages,
                "artifacts": {
                    key: str(value)
                    for key, value in asdict(paths).items()
                    if key not in {"worktree", "artifacts", "compile_db"}
                },
            }
            paths.result.write_text(
                json.dumps(result_doc, indent=2) + "\n", encoding="utf-8"
            )
        if worktree_added and not keep_worktree:
            executor.run(
                ["git", "worktree", "remove", "--force", str(paths.worktree)],
                cwd=repo_root,
            )
    return {
        "id": case["id"],
        "status": status,
        "artifacts": str(paths.artifacts),
        "worktree": str(paths.worktree) if keep_worktree else None,
    }


def aggregate_evaluations(
    cases: Sequence[dict[str, Any]], run_root: Path
) -> dict[str, Any]:
    """Collect evaluator outputs without duplicating its scoring rules."""
    rows: list[dict[str, Any]] = []
    routing: dict[str, dict[str, Any]] = {}
    for case in cases:
        expectation = case["routing"]["expectation"]
        bucket = routing.setdefault(expectation, {"cases": 0, "hits": 0})
        bucket["cases"] += 1
        evaluation_path = case_paths(run_root, case["id"]).evaluation_json
        row: dict[str, Any] = {
            "id": case["id"],
            "kind": case["kind"],
            "routing_expectation": expectation,
            "evaluation": str(evaluation_path),
            "cluster_rank": None,
            "resource_row_rank": None,
            "within_cluster_budget": False,
            "judge_status": "not_available",
            "localization_status": "not_available",
        }
        if evaluation_path.is_file():
            try:
                report = json.loads(evaluation_path.read_text(encoding="utf-8"))
                evaluated = next(
                    entry for entry in report["cases"] if entry["id"] == case["id"]
                )
            except (
                OSError,
                json.JSONDecodeError,
                KeyError,
                StopIteration,
                TypeError,
            ) as error:
                row["evaluation_error"] = str(error)
            else:
                row.update(
                    {
                        "cluster_rank": evaluated.get("cluster_rank"),
                        "resource_row_rank": evaluated.get("resource_row_rank"),
                        "within_cluster_budget": bool(
                            evaluated.get("within_cluster_budget")
                        ),
                        "judge_status": evaluated.get("judge_status", "not_available"),
                        "localization_status": evaluated.get("localization", {}).get(
                            "status", "not_available"
                        ),
                    }
                )
        if row["within_cluster_budget"]:
            bucket["hits"] += 1
        rows.append(row)

    for bucket in routing.values():
        bucket["recall_at_budget"] = bucket["hits"] / bucket["cases"]
    total = len(rows)
    hits = sum(bool(row["within_cluster_budget"]) for row in rows)
    return {
        "schema": 1,
        "budget_unit": "feature_pair_cluster",
        "cases": total,
        "hits": hits,
        "recall_at_budget": hits / total if total else 0.0,
        "routing": routing,
        "results": rows,
    }


ROUTE_LABELS = {
    "must_route": "Required route",
    "stretch_route": "Extended route",
    "ambiguity_stress": "Ranking stress case",
}

STATUS_LABELS = {
    "not_judged": "Model not run",
    "not_available": "Not available",
}


def format_corpus_summary(
    metrics: dict[str, Any], budget: int, case_titles: dict[str, str]
) -> str:
    lines = [
        "# Interaction-review corpus summary",
        "",
        f"- **Cluster Recall@{budget}:** {metrics['hits']}/{metrics['cases']} "
        f"({metrics['recall_at_budget']:.1%})",
    ]
    for expectation, bucket in sorted(metrics["routing"].items()):
        label = ROUTE_LABELS.get(expectation, expectation.replace("_", " ").title())
        lines.append(
            f"- **{label}:** {bucket['hits']}/{bucket['cases']} "
            f"({bucket['recall_at_budget']:.1%})"
        )
    lines += [
        "",
        "| Case | Route class | Cluster rank | Row rank | In budget | Model review | Primary location |",
        "| --- | --- | ---: | ---: | :---: | --- | --- |",
    ]
    for row in metrics["results"]:
        cluster_rank = row["cluster_rank"] if row["cluster_rank"] is not None else "—"
        row_rank = (
            row["resource_row_rank"] if row["resource_row_rank"] is not None else "—"
        )
        route_label = ROUTE_LABELS.get(
            row["routing_expectation"],
            row["routing_expectation"].replace("_", " ").title(),
        )
        judge_label = STATUS_LABELS.get(
            row["judge_status"], row["judge_status"].replace("_", " ").title()
        )
        localization_label = STATUS_LABELS.get(
            row["localization_status"],
            row["localization_status"].replace("_", " ").title(),
        )
        lines.append(
            f"| {case_titles.get(row['id'], row['id'])} | {route_label} | "
            f"{cluster_rank} | "
            f"{row_rank} | {'yes' if row['within_cluster_budget'] else 'no'} | "
            f"{judge_label} | {localization_label} |"
        )
    lines.append("")
    return "\n".join(lines)


def _find_build_dir(repo_root: Path) -> Path | None:
    for name in (".build", "build"):
        candidate = repo_root / name
        if (candidate / "compile_commands.json").is_file():
            return candidate
    return None


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--repo-root", type=Path, default=REPO_ROOT)
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument(
        "--case", action="append", default=[], help="case id; repeatable"
    )
    parser.add_argument(
        "--kind",
        action="append",
        default=[],
        choices=sorted(CASE_KINDS),
        help="case kind; repeatable",
    )
    parser.add_argument(
        "--list", action="store_true", help="list selected cases and exit"
    )
    parser.add_argument(
        "--mode",
        choices=("plan", "run"),
        default="plan",
        help="plan is read-only (default); run creates disposable worktrees",
    )
    parser.add_argument(
        "--run-root", type=Path, help="run directory outside the repository"
    )
    parser.add_argument("--budget", type=int, default=6)
    parser.add_argument(
        "--with-model",
        action="store_true",
        help="make live Bedrock calls during --mode run (never implied)",
    )
    parser.add_argument(
        "--aws-region",
        default=os.environ.get("AWS_REGION") or os.environ.get("AWS_DEFAULT_REGION"),
    )
    parser.add_argument("--aws-profile", default=os.environ.get("AWS_PROFILE"))
    parser.add_argument("--model")
    parser.add_argument(
        "--effort", choices=("low", "medium", "high", "xhigh", "max"), default="high"
    )
    parser.add_argument("--keep-worktrees", action="store_true")
    parser.add_argument("--plan-out", type=Path, help="also write the JSON plan here")
    return parser


def main(
    argv: list[str] | None = None,
    *,
    executor: CommandExecutor | None = None,
    stdout: TextIO | None = None,
) -> int:
    parser = _parser()
    args = parser.parse_args(argv)
    output = stdout or sys.stdout
    command_executor = executor or SubprocessExecutor()

    try:
        repo_root = args.repo_root.resolve()
        tool_root = HERE
        manifest = load_manifest(args.manifest.resolve())
        cases = select_cases(manifest, args.case, args.kind)
        if args.list:
            for case in cases:
                print(f"{case['id']}\t{case['kind']}\t{case['title']}", file=output)
            return 0
        if args.budget < 1:
            raise CorpusError("--budget must be at least 1")
        if args.with_model and args.mode != "run":
            raise CorpusError("--with-model requires explicit --mode run")
        if args.with_model and not args.aws_region:
            raise CorpusError("--with-model requires --aws-region or AWS_REGION")
        if args.keep_worktrees and args.mode != "run":
            raise CorpusError("--keep-worktrees applies only to --mode run")

        build_dir = (
            args.build_dir.resolve() if args.build_dir else _find_build_dir(repo_root)
        )
        if build_dir is None or not (build_dir / "compile_commands.json").is_file():
            raise CorpusError("no build directory with compile_commands.json found")

        validations = [
            validate_case(case, repo_root, command_executor) for case in cases
        ]
        if args.mode == "plan":
            run_root = args.run_root.resolve() if args.run_root else PLAN_ROOT
        elif args.run_root:
            run_root = args.run_root.resolve()
            if _is_within(run_root, repo_root) or run_root == repo_root:
                raise CorpusError("--run-root must be outside the shared repository")
            run_root.mkdir(parents=True, exist_ok=True)
        else:
            run_root = Path(tempfile.mkdtemp(prefix="interaction-review-corpus-"))

        plan = build_plan(
            cases,
            repo_root=repo_root,
            tool_root=tool_root,
            run_root=run_root,
            source_build_dir=build_dir,
            python=Path(sys.executable),
            budget=args.budget,
            with_model=args.with_model,
            aws_region=args.aws_region,
            aws_profile=args.aws_profile,
            model=args.model,
            effort=args.effort,
        )
        plan["mode"] = args.mode
        plan["validation"] = validations
        rendered = json.dumps(plan, indent=2) + "\n"
        print(rendered, file=output, end="")
        if args.plan_out:
            plan_out = args.plan_out.resolve()
            for protected in (repo_root / ".git", repo_root / "cfg"):
                if plan_out == protected or _is_within(plan_out, protected):
                    raise CorpusError(f"--plan-out may not write inside {protected}")
            plan_out.parent.mkdir(parents=True, exist_ok=True)
            plan_out.write_text(rendered, encoding="utf-8")
        if args.mode == "plan":
            return 0

        (run_root / "plan.json").write_text(rendered, encoding="utf-8")
        results = []
        for case in cases:
            try:
                results.append(
                    execute_case(
                        case,
                        paths=case_paths(run_root, case["id"]),
                        repo_root=repo_root,
                        tool_root=tool_root,
                        source_build_dir=build_dir,
                        python=Path(sys.executable),
                        budget=args.budget,
                        with_model=args.with_model,
                        aws_region=args.aws_region,
                        aws_profile=args.aws_profile,
                        model=args.model,
                        effort=args.effort,
                        keep_worktree=args.keep_worktrees,
                        executor=command_executor,
                    )
                )
            except CorpusError as error:
                results.append(
                    {
                        "id": case["id"],
                        "status": "failed",
                        "error": str(error),
                        "artifacts": str(case_paths(run_root, case["id"]).artifacts),
                        "worktree": None,
                    }
                )
        summary = {
            "schema": 1,
            "run_root": str(run_root),
            "model_called": args.with_model,
            "results": results,
        }
        metrics = aggregate_evaluations(cases, run_root)
        summary["metrics"] = metrics
        (run_root / "corpus-summary.json").write_text(
            json.dumps(metrics, indent=2) + "\n", encoding="utf-8"
        )
        (run_root / "corpus-summary.md").write_text(
            format_corpus_summary(
                metrics,
                args.budget,
                {case["id"]: case["title"] for case in cases},
            ),
            encoding="utf-8",
        )
        (run_root / "corpus-run.json").write_text(
            json.dumps(summary, indent=2) + "\n", encoding="utf-8"
        )
        print(json.dumps(summary, indent=2), file=output)
        return 1 if any(result["status"] != "complete" for result in results) else 0
    except CorpusError as error:
        print(f"corpus error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
