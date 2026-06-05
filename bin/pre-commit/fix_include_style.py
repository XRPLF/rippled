#!/usr/bin/env python3

"""
Converts quoted includes (#include "...") to angle-bracket includes
(#include <...>), which is the required style in this project.

Usage: ./bin/pre-commit/fix_include_style.py <file1> <file2> ...
"""

import re
import sys
from pathlib import Path

PATTERN = re.compile(r'^(\s*#include\s*)"([^"]+)"', re.MULTILINE)


def fix_includes(path: Path) -> bool:
    original = path.read_text(encoding="utf-8")
    fixed = PATTERN.sub(r"\1<\2>", original)
    if fixed != original:
        path.write_text(fixed, encoding="utf-8")
        return False
    return True


def main() -> int:
    files = [Path(f) for f in sys.argv[1:]]
    success = True

    for path in files:
        success &= fix_includes(path)

    return 0 if success else 1


if __name__ == "__main__":
    sys.exit(main())
