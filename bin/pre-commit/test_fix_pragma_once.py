#!/usr/bin/env python3
"""
Tests for fix_pragma_once.py.

Run directly (no test framework needed):
    ./bin/pre-commit/test_fix_pragma_once.py
or under pytest:
    pytest bin/pre-commit/test_fix_pragma_once.py
"""

import sys
import tempfile
from pathlib import Path

from fix_pragma_once import fix_pragma_once


def run_on(content: str) -> tuple[bool, str]:
    """Run the hook over a header holding ``content``.

    Returns ``(already_had_it, content_afterwards)``, mirroring the hook's own
    return value: true when the file was left alone.
    """
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / "header.h"
        path.write_text(content, encoding="utf-8")
        already_had_it = fix_pragma_once(path)
        return already_had_it, path.read_text(encoding="utf-8")


def test_missing_pragma_is_added() -> None:
    already_had_it, result = run_on("int x;\n")
    assert already_had_it is False
    assert result == "#pragma once\n\nint x;\n"


def test_pragma_with_blank_line_is_left_alone() -> None:
    content = "#pragma once\n\nint x;\n"
    assert run_on(content) == (True, content)


def test_pragma_without_blank_line_is_left_alone() -> None:
    # regression: the check looked for the directive plus a blank line, so a
    # header that ran straight into its first include got a second directive
    content = "#pragma once\n#include <cstdint>\n"
    assert run_on(content) == (True, content)


def test_pragma_below_a_copyright_banner_is_left_alone() -> None:
    content = "// Copyright\n#pragma once\n#include <cstdint>\n"
    assert run_on(content) == (True, content)


def test_pragma_on_the_last_line_is_left_alone() -> None:
    content = "// Copyright\n#pragma once\n"
    assert run_on(content) == (True, content)


def main() -> int:
    tests = sorted(
        (name, fn)
        for name, fn in globals().items()
        if name.startswith("test_") and callable(fn)
    )
    failed = 0
    for name, fn in tests:
        try:
            fn()
            print(f"PASS {name}")
        except AssertionError as exc:
            failed += 1
            print(f"FAIL {name}: {exc!r}")
    print(f"\n{len(tests) - failed}/{len(tests)} passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
