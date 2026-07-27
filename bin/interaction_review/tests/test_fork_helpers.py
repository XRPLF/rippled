"""Pure-Python fork_extractor helpers (no libclang / no build dir needed)."""

import json

import pytest

from fork_extractor import (
    _REQUIRED_FORKS,
    _find_tu_entry,
    _parse_args,
    _resource_dir,
    _validate_required,
)

TU = "/repo/src/libxrpl/tx/Transactor.cpp"


def test_parse_args_command_form():
    entry = {
        "file": TU,
        "directory": "/repo/.build",
        "command": f"/usr/bin/c++ -DFOO -I/inc -std=c++20 -c -o CMakeFiles/x.o {TU}",
    }
    args = _parse_args(entry, "/res")
    assert "/usr/bin/c++" not in args  # compiler dropped
    assert "-c" not in args
    assert "CMakeFiles/x.o" not in args  # -o argument dropped
    assert not any(a.endswith("Transactor.cpp") for a in args)  # source dropped
    assert "-DFOO" in args and "-I/inc" in args and "-std=c++20" in args
    assert args[args.index("-working-directory") + 1] == "/repo/.build"
    assert args[args.index("-resource-dir") + 1] == "/res"


def test_parse_args_arguments_form_no_resource_dir():
    entry = {
        "file": TU,
        "directory": "/repo/.build",
        "arguments": ["c++", "-DBAR", "-c", "-o", "x.o", TU],
    }
    args = _parse_args(entry, None)
    assert args.count("x.o") == 0
    assert "-DBAR" in args
    assert "-resource-dir" not in args


def test_find_tu_entry(tmp_path):
    db = tmp_path / "compile_commands.json"
    db.write_text(
        json.dumps(
            [
                {
                    "file": "/repo/src/other.cpp",
                    "directory": "/repo/.build",
                    "command": "c++ /repo/src/other.cpp",
                },
                {"file": TU, "directory": "/repo/.build", "command": f"c++ {TU}"},
            ]
        )
    )
    assert _find_tu_entry(tmp_path)["file"] == TU


def test_find_tu_entry_missing_db(tmp_path):
    with pytest.raises(FileNotFoundError):
        _find_tu_entry(tmp_path)


def test_find_tu_entry_missing_tu(tmp_path):
    (tmp_path / "compile_commands.json").write_text(
        json.dumps(
            [{"file": "/repo/src/other.cpp", "directory": "/repo", "command": "c++ x"}]
        )
    )
    with pytest.raises(ValueError, match="no entry"):
        _find_tu_entry(tmp_path)


def test_resource_dir_picks_highest(tmp_path):
    lib = tmp_path / "lib"
    (lib / "clang" / "18").mkdir(parents=True)
    (lib / "clang" / "22").mkdir(parents=True)
    dylib = lib / "libclang.dylib"
    dylib.write_text("")
    assert _resource_dir(dylib) == str(lib / "clang" / "22")


def test_resource_dir_absent(tmp_path):
    dylib = tmp_path / "lib" / "libclang.dylib"
    dylib.parent.mkdir(parents=True)
    dylib.write_text("")
    assert _resource_dir(dylib) is None


def test_validate_required_passes_when_all_present():
    _validate_required(set(_REQUIRED_FORKS) | {"extra"})  # no raise


def test_validate_required_raises_on_missing_anchor():
    # A high-value fork silently dropping out of discovery must fail loudly.
    partial = set(_REQUIRED_FORKS) - {"checkSign"}
    with pytest.raises(RuntimeError, match="checkSign"):
        _validate_required(partial)
