#!/usr/bin/env python3
"""Pre-commit hook that runs clang-tidy on staged files using run-clang-tidy.

The script determines the staged files itself (see `pass_filenames: false` in
.pre-commit-config.yaml) so run-clang-tidy is run once and handles parallelism
internally: pre-commit would otherwise split the files across parallel hook
invocations that race when fixes edit a shared header.

Fixes are collected with `-export-fixes` and applied by clang-apply-replacements
in a separate step rather than with run-clang-tidy's `-fix`. The `add_module`
build isolates each module's headers behind a per-module symlink directory
(build/modules/<module>/...), so a header reachable from several translation
units is referenced through different paths that all resolve to the same source
file. clang-apply-replacements deduplicates identical replacements by their
literal path, so those paths must be canonicalised to the real source path
first; otherwise the same fix is applied once per path and corrupts the header.
"""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

CLANG_TIDY_VERSION = 22

# Extensions run-clang-tidy can analyse: `.cpp` translation units and, thanks to
# the `verify_headers` build option, `.h`/`.hpp` headers (each has its own
# compile_commands.json entry). `.ipp` fragments have no entry and are skipped.
TIDY_EXTENSIONS = {".cpp", ".h", ".hpp"}

# A single-quoted `FilePath:` entry in an -export-fixes YAML file, allowing the
# `- ` marker that precedes it inside a `Replacements:` sequence. clang-tidy
# emits paths single-quoted and doubles any embedded quote per YAML rules.
FILEPATH_RE = re.compile(r"^(\s*(?:-\s+)?FilePath:\s*)'((?:[^']|'')*)'\s*$")


def find_tool(name: str) -> str | None:
    for candidate in (f"{name}-{CLANG_TIDY_VERSION}", name):
        if path := shutil.which(candidate):
            return path
    return None


def find_build_dir(repo_root: Path) -> Path | None:
    for name in (".build", "build"):
        candidate = repo_root / name
        if (candidate / "compile_commands.json").exists():
            return candidate
    return None


def staged_files(repo_root: Path) -> list[Path]:
    """Return absolute paths of staged, lint-able C/C++ files.

    `--diff-filter=d` excludes deletions so we never lint a removed file.
    """
    output = subprocess.check_output(
        ["git", "diff", "--staged", "--name-only", "--diff-filter=d", "--"]
        + [f"*{ext}" for ext in TIDY_EXTENSIONS],
        text=True,
        cwd=repo_root,
    )
    return [repo_root / rel for rel in output.splitlines() if rel]


def canonicalize_fix_paths(fixes_dir: Path) -> None:
    """Rewrite every `FilePath` in the exported fixes to its real source path.

    A header included through a module's isolation symlink is recorded under that
    symlink's path; collapsing all paths to the same real file lets
    clang-apply-replacements recognise the per-translation-unit duplicates and
    apply each fix once.
    """
    for yaml in fixes_dir.glob("*.yaml"):
        lines = []
        for line in yaml.read_text().splitlines():
            if m := FILEPATH_RE.match(line):
                path = m.group(2).replace("''", "'")
                real = os.path.realpath(path).replace("'", "''")
                line = f"{m.group(1)}'{real}'"
            lines.append(line)
        yaml.write_text("\n".join(lines) + "\n")


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

    run_clang_tidy = find_tool("run-clang-tidy")
    clang_apply_replacements = find_tool("clang-apply-replacements")
    missing = [
        name
        for name, path in (
            ("run-clang-tidy", run_clang_tidy),
            ("clang-apply-replacements", clang_apply_replacements),
        )
        if not path
    ]
    if missing:
        print(
            f"clang-tidy check failed: TIDY is enabled but {' and '.join(missing)} "
            f"was not found in PATH (tried the '-{CLANG_TIDY_VERSION}' suffix too).",
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

    with tempfile.TemporaryDirectory() as fixes_dir:
        result = subprocess.run(
            [
                run_clang_tidy,
                "-quiet",
                "-p",
                build_dir,
                "-export-fixes",
                fixes_dir,
                "-allow-no-checks",
            ]
            + files
        )
        canonicalize_fix_paths(Path(fixes_dir))
        # `FormatStyle` in .clang-tidy does not reach this path,
        # so ask for the repository style here.
        applied = subprocess.run(
            [clang_apply_replacements, "--format", "--style=file", fixes_dir]
        )

    return result.returncode or applied.returncode


if __name__ == "__main__":
    sys.exit(main())
