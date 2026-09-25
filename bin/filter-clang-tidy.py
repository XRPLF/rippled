#!/usr/bin/env python3

"""
Reduce run-clang-tidy output to its unique errors.

It does two things:

  1. Filters the raw output down to diagnostics and their source-context lines
     (the indented "  103 | ..." / "      | ^" lines clang-tidy prints),
     matching the "path:line:col: error:" diagnostic shape.

  2. Deduplicates. The same diagnostic in a header is reported once per
     translation unit that includes it, so identical error blocks are collapsed
     to their first occurrence.

An "error block" is an "error:" line together with the indented context lines
and any "note:" lines that follow it (up to the next "error:" line). Blocks are
compared as a whole, so an error stays attached to its own context, and
first-occurrence order is preserved.

The deduplicated output goes to stdout; a summary of unique error counts per
check is printed to stderr.

Usage:
  bin/filter-clang-tidy.py [INPUT_FILE]            # read from file, or
  run-clang-tidy ... | bin/filter-clang-tidy.py    # read from stdin
"""

import re
import sys
from collections import Counter

# A clang-tidy diagnostic line looks like "path:line:col: error: msg [check]".
# Matching on that shape (rather than a loose "error" substring) avoids treating
# progress lines whose paths contain "error" as diagnostics, e.g.
#   [284/850][0.7s] /nix/.../clang-tidy ... src/.../error.cpp
DIAG_RE = re.compile(r":\d+:\d+: (?:error|warning|note):")
ERROR_RE = re.compile(r":\d+:\d+: error:")
CHECK_RE = re.compile(r" error: .*\[([^\],]+)")


def filter_and_dedup(lines: list[str]) -> list[str]:
    """Keep diagnostics with their context, then drop duplicate error blocks."""
    blocks: list[str] = []
    seen: set[str] = set()
    current: list[str] = []

    def flush() -> None:
        if not current:
            return
        block = "".join(current)
        if block not in seen:
            seen.add(block)
            blocks.append(block)

    for line in lines:
        # Keep only diagnostics and their indented source-context lines; drop
        # progress/status output and blank lines.
        if not (DIAG_RE.search(line) or line[:1] in (" ", "\t")):
            continue
        # An "error:" line starts a new block; its context and any following
        # "note:" lines (and their context) belong to it.
        if ERROR_RE.search(line):
            flush()
            current = []
        current.append(line)
    flush()

    return blocks


def summarize(blocks: list[str]) -> Counter[str]:
    """Count unique errors per check name (e.g. "bugprone-branch-clone")."""
    counts: Counter[str] = Counter()
    for block in blocks:
        # The error line is the first line of the block.
        match = CHECK_RE.search(block.splitlines()[0])
        if match:
            counts[match.group(1)] += 1
    return counts


def main() -> int:
    if len(sys.argv) > 1 and sys.argv[1] != "-":
        with open(sys.argv[1], encoding="utf-8") as f:
            lines = f.readlines()
    else:
        lines = sys.stdin.readlines()

    blocks = filter_and_dedup(lines)
    # Blank line between blocks so distinct errors are easy to tell apart.
    sys.stdout.write("\n".join(blocks))

    print("\nUnique errors per check:", file=sys.stderr)
    for check, count in summarize(blocks).most_common():
        print(f"{count:>4} {check}", file=sys.stderr)

    return 0


if __name__ == "__main__":
    sys.exit(main())
