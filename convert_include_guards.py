#!/usr/bin/env python3
"""
Script to replace include guards with #pragma once
Handles guards starting with XRPL_, BEAST_, or TEST_
"""

import sys
from pathlib import Path


def convert_file(file_path, prefixes=None):
    """
    Convert a file from include guards to #pragma once

    Args:
        file_path: Path to the file to convert
        prefixes: List of prefixes to match (e.g., ['XRPL_', 'BEAST_', 'TEST_'])
                  If None, defaults to ['XRPL_', 'BEAST_', 'TEST_']

    Returns True if the file was modified, False otherwise
    """
    if prefixes is None:
        prefixes = ["XRPL_", "BEAST_", "TEST_"]

    try:
        with open(file_path, "r", encoding="utf-8") as f:
            lines = f.readlines()
    except Exception as e:
        print(f"Error reading {file_path}: {e}", file=sys.stderr)
        return False

    if len(lines) < 3:
        print(f"Skipping {file_path}: too few lines", file=sys.stderr)
        return False

    # Find the #ifndef with any of the specified prefixes
    ifndef_idx = -1
    define_idx = -1
    found_prefix = None

    for i, line in enumerate(lines):
        stripped = line.strip()
        for prefix in prefixes:
            if stripped.startswith(f"#ifndef {prefix}"):
                ifndef_idx = i
                found_prefix = prefix
                # The #define should be the next line
                if i + 1 < len(lines) and lines[i + 1].strip().startswith(
                    f"#define {prefix}"
                ):
                    define_idx = i + 1
                break
        if ifndef_idx != -1:
            break

    if ifndef_idx == -1 or define_idx == -1:
        print(
            f"No include guard with prefixes {prefixes} found in {file_path}",
            file=sys.stderr,
        )
        return False

    # Find the last #endif line
    endif_idx = -1
    for i in range(len(lines) - 1, -1, -1):
        stripped = lines[i].strip()
        if stripped.startswith("#endif"):
            endif_idx = i
            break

    if endif_idx == -1:
        print(f"No closing #endif found in {file_path}", file=sys.stderr)
        return False

    # Build the new content
    new_lines = []

    # Add everything before the #ifndef
    new_lines.extend(lines[:ifndef_idx])

    # Add #pragma once with exactly one empty line after it
    new_lines.append("#pragma once\n")
    new_lines.append("\n")

    # Add everything between #define and #endif, but skip leading empty lines
    content_lines = lines[define_idx + 1 : endif_idx]
    # Skip leading empty lines
    start_idx = 0
    while start_idx < len(content_lines) and content_lines[start_idx].strip() == "":
        start_idx += 1
    new_lines.extend(content_lines[start_idx:])

    # Add everything after #endif (usually just empty lines, but include it)
    new_lines.extend(lines[endif_idx + 1 :])

    # Remove trailing empty lines at the end, then ensure exactly one newline at end
    while new_lines and new_lines[-1].strip() == "":
        new_lines.pop()

    if new_lines and not new_lines[-1].endswith("\n"):
        new_lines[-1] += "\n"
    else:
        new_lines.append("\n")

    # Write the file
    try:
        with open(file_path, "w", encoding="utf-8") as f:
            f.writelines(new_lines)
        print(f"Converted: {file_path}")
        return True
    except Exception as e:
        print(f"Error writing {file_path}: {e}", file=sys.stderr)
        return False


def main():
    if len(sys.argv) < 2:
        print("Usage: python convert_include_guards.py <file1> [file2 ...]")
        sys.exit(1)

    files = sys.argv[1:]
    success_count = 0
    fail_count = 0

    for file_path in files:
        if convert_file(file_path):
            success_count += 1
        else:
            fail_count += 1

    print(
        f"\nSummary: {success_count} files converted, {fail_count} files failed/unchanged"
    )

    return 0 if fail_count == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
