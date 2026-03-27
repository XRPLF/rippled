#!/usr/bin/env python3
"""Pre-commit hook that runs clang-tidy on changed files using run-clang-tidy."""

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


def find_build_dir() -> Path | None:
    repo_root = Path(__file__).parent.parent
    for name in (".build", "build"):
        candidate = repo_root / name
        if (candidate / "compile_commands.json").exists():
            return candidate
    return None


def main():
    if not os.environ.get("TIDY"):
        return 0

    files = sys.argv[1:]

    run_clang_tidy = find_run_clang_tidy()
    if not run_clang_tidy:
        print("clang-tidy check skipped: run-clang-tidy not found", file=sys.stderr)
        return 0

    build_dir = find_build_dir()
    if not build_dir:
        print(
            "clang-tidy check failed: no build directory with compile_commands.json found "
            "(looked for .build/ and build/)",
            file=sys.stderr,
        )
        return 1

    if not files:
        return 0

    cmd = [run_clang_tidy, "-quiet", "-p", str(build_dir), "-fix"] + files
    result = subprocess.run(cmd)
    return result.returncode


if __name__ == "__main__":
    sys.exit(main())
