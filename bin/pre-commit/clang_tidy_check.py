#!/usr/bin/env python3
"""Pre-commit hook that runs clang-tidy on changed files using run-clang-tidy.

The set of files is chosen by pre-commit (see .pre-commit-config.yaml), which
filters to C/C++ sources and excludes `.ipp` fragments. Headers are linted
directly: the `verify_headers` build option (ON by default) compiles every
`.h`/`.hpp` on its own, so each header is the main file of its own
compile_commands.json entry and run-clang-tidy can analyse it just like a
`.cpp`.
"""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path


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


def main():
    if not os.environ.get("TIDY"):
        return 0

    files = sys.argv[1:]
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

    repo_root = Path(
        subprocess.check_output(
            ["git", "rev-parse", "--show-toplevel"],
            cwd=Path(__file__).parent,
            text=True,
        ).strip()
    )
    build_dir = find_build_dir(repo_root)
    if not build_dir:
        print(
            "clang-tidy check failed: no build directory with compile_commands.json found "
            "(looked for .build/ and build/)",
            file=sys.stderr,
        )
        return 1

    result = subprocess.run(
        [run_clang_tidy, "-quiet", "-p", str(build_dir), "-fix", "-allow-no-checks"]
        + files
    )
    return result.returncode


if __name__ == "__main__":
    sys.exit(main())
