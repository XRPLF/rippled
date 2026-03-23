#!/usr/bin/env python3
"""
List generated output files for CMake configure stage.

Uses only Python stdlib - no external dependencies required.
This allows CMake to determine output file lists at configure time
without needing to set up a Python virtual environment first.
"""

import re
import argparse
from pathlib import Path


def extract_names_transaction(filepath):
    """Extract class names from TRANSACTION macro calls.

    Matches: TRANSACTION(ttTAG, value, Name, ...)
    Requires the first argument to be a real tag (starts with 'tt') to
    avoid matching documentation comments like TRANSACTION(tag, value, name, ...).
    """
    with open(filepath, "r") as f:
        content = f.read()

    # Match TRANSACTION(ttTAG, value, Name, ...) and extract Name (3rd argument)
    # First arg must start with 'tt' to distinguish real calls from comments
    pattern = r"TRANSACTION\s*\(\s*tt\w+\s*,\s*[^,]+,\s*(\w+)\s*,"
    return re.findall(pattern, content)


def extract_names_ledger_entry(filepath):
    """Extract class names from LEDGER_ENTRY and LEDGER_ENTRY_DUPLICATE macro calls.

    Matches: LEDGER_ENTRY[_DUPLICATE](ltTAG, value, Name, ...)
    Requires the first argument to be a real tag (starts with 'lt') to
    avoid matching documentation comments.
    """
    with open(filepath, "r") as f:
        content = f.read()

    # Match LEDGER_ENTRY or LEDGER_ENTRY_DUPLICATE(ltTAG, value, Name, ...) and extract Name
    # First arg must start with 'lt' to distinguish real calls from comments
    pattern = r"LEDGER_ENTRY(?:_DUPLICATE)?\s*\(\s*lt\w+\s*,\s*[^,]+,\s*(\w+)\s*,"
    return re.findall(pattern, content)


def main():
    parser = argparse.ArgumentParser(
        description="List generated output files for CMake (no venv required)"
    )
    parser.add_argument("macro_path", help="Path to macro file")
    parser.add_argument(
        "--type",
        required=True,
        choices=["transaction", "ledger_entry"],
        help="Type of macro file to parse",
    )
    parser.add_argument(
        "--header-dir", required=True, help="Output directory for header files"
    )
    parser.add_argument("--test-dir", help="Output directory for test files (optional)")

    args = parser.parse_args()

    if args.type == "transaction":
        names = extract_names_transaction(args.macro_path)
    else:
        names = extract_names_ledger_entry(args.macro_path)

    header_dir = Path(args.header_dir)
    for name in names:
        print(header_dir / f"{name}.h")

    if args.test_dir:
        test_dir = Path(args.test_dir)
        for name in names:
            print(test_dir / f"{name}Tests.cpp")


if __name__ == "__main__":
    main()
