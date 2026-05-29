#!/usr/bin/env python3
"""Pre-commit hook that runs clang-tidy on changed files using run-clang-tidy."""

from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
import sys
from collections import defaultdict
from pathlib import Path

HEADER_EXTENSIONS = {".h", ".hpp", ".ipp"}
SOURCE_EXTENSIONS = {".cpp"}
INCLUDE_RE = re.compile(r"^\s*#\s*include\s*[<\"]([^>\"]+)[>\"]")


def find_run_clang_tidy() -> str | None:
    for candidate in ("run-clang-tidy-21", "run-clang-tidy"):
        if path := shutil.which(candidate):
            return path
    return None


def find_build_dir(repo_root: Path) -> Path | None:
    for name in (".build", "build"):
        candidate = repo_root / name
        if (candidate / "compile_commands.json").exists():
            return candidate
    return None


def build_include_graph(build_dir: Path, repo_root: Path) -> tuple[dict, set]:
    """
    Scan all files reachable from compile_commands.json and build an inverted include graph.

    Returns:
        inverted: header_path -> set of files that include it
        source_files: set of all TU paths from compile_commands.json
    """
    with open(build_dir / "compile_commands.json") as f:
        db = json.load(f)

    source_files = {Path(e["file"]).resolve() for e in db}
    include_roots = [repo_root / "include", repo_root / "src"]
    inverted: dict[Path, set[Path]] = defaultdict(set)

    to_scan: set[Path] = set(source_files)
    scanned: set[Path] = set()

    while to_scan:
        file = to_scan.pop()
        if file in scanned or not file.exists():
            continue
        scanned.add(file)

        content = file.read_text()

        for line in content.splitlines():
            m = INCLUDE_RE.match(line)
            if not m:
                continue
            for root in include_roots:
                candidate = (root / m.group(1)).resolve()
                if candidate.exists():
                    inverted[candidate].add(file)
                    if candidate not in scanned:
                        to_scan.add(candidate)
                    break

    return inverted, source_files


def find_tus_for_headers(
    headers: list[Path],
    inverted: dict[Path, set[Path]],
    source_files: set[Path],
) -> set[Path]:
    """
    For each header, pick one TU that transitively includes it.
    Prefers a TU whose stem matches the header's stem, otherwise picks the first found.
    """
    result: set[Path] = set()

    for header in headers:
        preferred: Path | None = None
        visited: set[Path] = {header}
        stack: list[Path] = [header]

        while stack:
            h = stack.pop()
            for inc in inverted.get(h, ()):
                if inc in source_files:
                    if inc.stem == header.stem:
                        preferred = inc
                        break
                    if preferred is None:
                        preferred = inc
                if inc not in visited:
                    visited.add(inc)
                    stack.append(inc)
            if preferred is not None and preferred.stem == header.stem:
                break

        if preferred is not None:
            result.add(preferred)

    return result


def resolve_files(
    input_files: list[str], build_dir: Path, repo_root: Path
) -> list[str]:
    """
    Split input into source files and headers. Source files are passed through;
    headers are resolved to the TUs that transitively include them.
    """
    sources: list[Path] = []
    headers: list[Path] = []

    for f in input_files:
        p = Path(f).resolve()
        if p.suffix in SOURCE_EXTENSIONS:
            sources.append(p)
        elif p.suffix in HEADER_EXTENSIONS:
            headers.append(p)

    if not headers:
        return [str(p) for p in sources]

    print(
        f"Resolving {len(headers)} header(s) to compilation units...", file=sys.stderr
    )
    inverted, source_files = build_include_graph(build_dir, repo_root)
    tus = find_tus_for_headers(headers, inverted, source_files)

    if not tus:
        print(
            "Warning: no compilation units found that include the modified headers; "
            "skipping clang-tidy for headers.",
            file=sys.stderr,
        )

    return sorted({str(p) for p in (*sources, *tus)})


def staged_files(repo_root: Path) -> list[str]:
    result = subprocess.run(
        ["git", "diff", "--staged", "--name-only", "--diff-filter=d"],
        capture_output=True,
        text=True,
        cwd=repo_root,
    )
    if result.returncode != 0:
        print(
            "clang-tidy check failed: 'git diff --staged' command failed.",
            file=sys.stderr,
        )
        if result.stderr:
            print(result.stderr, file=sys.stderr)
        sys.exit(result.returncode or 1)
    return [str(repo_root / p) for p in result.stdout.splitlines() if p]


def main():
    if not os.environ.get("TIDY"):
        return 0

    repo_root = Path(
        subprocess.check_output(
            ["git", "rev-parse", "--show-toplevel"],
            cwd=Path(__file__).parent,
            text=True,
        ).strip()
    )
    files = staged_files(repo_root)
    if not files:
        return 0

    run_clang_tidy = find_run_clang_tidy()
    if not run_clang_tidy:
        print(
            "clang-tidy check failed: TIDY is enabled but neither "
            "'run-clang-tidy-21' nor 'run-clang-tidy' was found in PATH.",
            file=sys.stderr,
        )
        return 1

    build_dir = find_build_dir(repo_root)
    if not build_dir:
        print(
            "clang-tidy check failed: no build directory with compile_commands.json found "
            "(looked for .build/ and build/)",
            file=sys.stderr,
        )
        return 1

    tidy_files = resolve_files(files, build_dir, repo_root)
    if not tidy_files:
        return 0

    result = subprocess.run(
        [run_clang_tidy, "-quiet", "-p", str(build_dir), "-fix", "-allow-no-checks"]
        + tidy_files
    )
    return result.returncode


if __name__ == "__main__":
    sys.exit(main())
