#!/usr/bin/env python3

"""
Adds "#pragma once" to the top of header files that don't already have it.

Usage: ./bin/pre-commit/fix_pragma_once.py <file1> <file2> ...
"""

import sys
from pathlib import Path

PRAGMA_ONCE = "#pragma once\n\n"


def fix_pragma_once(path: Path) -> bool:
    original = path.read_text(encoding="utf-8")
    if PRAGMA_ONCE not in original:
        path.write_text(PRAGMA_ONCE + original, encoding="utf-8")
        return False
    return True


def main() -> int:
    files = [Path(f) for f in sys.argv[1:]]
    success = True

    for path in files:
        success &= fix_pragma_once(path)

    return 0 if success else 1


if __name__ == "__main__":
    sys.exit(main())
