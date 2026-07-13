#!/usr/bin/env python3
"""Pre-commit hook that runs clang-tidy on staged files using run-clang-tidy.

The script determines the staged files itself (see `pass_filenames: false` in
.pre-commit-config.yaml) so run-clang-tidy is run once and handles parallelism
internally: pre-commit would otherwise split the files across parallel hook
invocations that race when `--fix` edits a shared header.
"""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path

CLANG_TIDY_VERSION = 22

# Extensions run-clang-tidy can analyse: `.cpp` translation units and, thanks to
# the `verify_headers` build option, `.h`/`.hpp` headers (each has its own
# compile_commands.json entry). `.ipp` fragments have no entry and are skipped.
TIDY_EXTENSIONS = {".cpp", ".h", ".hpp"}


def find_run_clang_tidy() -> str | None:
    for candidate in (f"run-clang-tidy-{CLANG_TIDY_VERSION}", "run-clang-tidy"):
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
            f"clang-tidy check failed: TIDY is enabled but neither "
            f"'run-clang-tidy-{CLANG_TIDY_VERSION}' nor 'run-clang-tidy' was found in PATH.",
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

    result = subprocess.run(
        [
            run_clang_tidy,
            "-quiet",
            "-p",
            build_dir,
            "-j",
            str(os.cpu_count()),
            "-fix",
            "-allow-no-checks",
        ]
        + files
    )
    return result.returncode


if __name__ == "__main__":
    sys.exit(main())
