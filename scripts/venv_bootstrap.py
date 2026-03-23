#!/usr/bin/env python3
"""
Dependency bootstrap utility for code generation scripts.

Call ensure_venv() at the very top of a script, before importing any
third-party packages. Installs required packages into a local directory
and adds it to sys.path so imports work without a full venv.
"""

import subprocess
import sys
from pathlib import Path


def ensure_venv(deps_dir: Path) -> None:
    """Ensure code generation dependencies are installed in deps_dir."""
    requirements_file = Path(__file__).parent / "requirements.txt"
    stamp = deps_dir / ".deps_ready"

    needs_setup = (
        not stamp.exists()
        or requirements_file.stat().st_mtime > stamp.stat().st_mtime
    )

    if needs_setup:
        print(f"Installing code generation dependencies to {deps_dir}...", flush=True)
        deps_dir.mkdir(parents=True, exist_ok=True)
        subprocess.run(
            [sys.executable, "-m", "pip", "install",
             "--target", str(deps_dir),
             "-r", str(requirements_file)],
            check=True,
        )
        stamp.touch()

    # Make installed packages importable in this process.
    deps_str = str(deps_dir)
    if deps_str not in sys.path:
        sys.path.insert(0, deps_str)
