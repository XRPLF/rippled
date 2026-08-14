"""Shared pytest fixtures for the interaction-review tests."""

import sys
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
TOOL_DIR = HERE.parent
REPO_ROOT = TOOL_DIR.parents[1]

# Make the tool modules importable as top-level modules.
sys.path.insert(0, str(TOOL_DIR))

_DEFAULT_DYLIB = Path("/opt/homebrew/opt/llvm/lib/libclang.dylib")


@pytest.fixture(scope="session")
def repo_root() -> Path:
    return REPO_ROOT


@pytest.fixture(scope="session")
def build_dir() -> Path:
    import build_graph

    found = build_graph.find_build_dir(REPO_ROOT)
    if found is None:
        pytest.skip("no build directory with compile_commands.json")
    return found


@pytest.fixture(scope="session")
def libclang_dylib() -> Path:
    if not _DEFAULT_DYLIB.exists():
        pytest.skip(f"libclang dylib not found at {_DEFAULT_DYLIB}")
    import clang.cindex as ci

    # set_library_file must be called at most once per process.
    if not ci.Config.loaded:
        ci.Config.set_library_file(str(_DEFAULT_DYLIB))
    return _DEFAULT_DYLIB
