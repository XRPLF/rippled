#!/usr/bin/env python3
"""Parse a unified git diff into per-file changed line ranges.

Ranges are on the **new** side, because that is what node locations are built
from: the graph is extracted from the head revision, so a base-side line number
would index a file that no longer exists in that form.
"""

from __future__ import annotations

import re
import subprocess
from dataclasses import dataclass, field
from pathlib import Path

# `@@ -old_start,old_count +new_start,new_count @@`; either count may be absent,
# which means 1.
_HUNK_RE = re.compile(r"^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@")
_NEW_FILE_RE = re.compile(r"^\+\+\+ (?:b/)?(.*)$")
_OLD_FILE_RE = re.compile(r"^--- (?:a/)?(.*)$")
# `diff --git a/<old> b/<new>` opens every file entry, including entries with no
# hunks at all (a pure rename or a mode change), which would otherwise vanish.
_GIT_HEADER_RE = re.compile(r"^diff --git a/(.*?) b/(.*)$")
_DEV_NULL = "/dev/null"


@dataclass
class FileDiff:
    """Changes to one file, on the new side."""

    path: str
    # Inclusive (start, end) line ranges that the diff added or modified.
    ranges: list[tuple[int, int]] = field(default_factory=list)
    # New-side line number -> added line text, for token inspection.
    added_lines: dict[int, str] = field(default_factory=dict)
    deleted: bool = False

    def touches(self, start: int, end: int) -> list[tuple[int, int]]:
        """The subset of this file's ranges overlapping [start, end]."""
        return [r for r in self.ranges if r[0] <= end and start <= r[1]]


def parse_unified_diff(text: str) -> dict[str, FileDiff]:
    """Changed line ranges per new-side path.

    A hunk with a new-side count of 0 is a pure deletion: nothing on the new side
    to point at, so the seam is recorded as the two lines the removal sits
    between. Losing it would mean a PR that only deletes code inside a fork body
    touched no node at all.

    Hunk bodies are consumed by new-side line count, and file headers are only
    recognized between hunks. Both matter: an added source line reading
    `++ something` is indistinguishable from a `+++ ` file header on its face, and
    treating it as one would silently re-attribute the rest of the file's hunks
    to a path that does not exist.
    """
    files: dict[str, FileDiff] = {}
    current: FileDiff | None = None
    old_path: str | None = None
    new_line = 0
    # New-side lines still expected in the current hunk. While this is non-zero
    # we are inside a hunk body and no line may be read as a header.
    new_remaining = 0

    def _register(path: str) -> FileDiff:
        return files.setdefault(path, FileDiff(path))

    for line in text.splitlines():
        if new_remaining > 0:
            if line.startswith("+"):
                if current is not None:
                    current.added_lines[new_line] = line[1:]
                new_line += 1
                new_remaining -= 1
            elif line.startswith(" "):
                # Context lines occupy new-side numbers too. With --unified=0
                # there are none, but the parser must stay correct for a diff
                # fetched from GitHub, which carries three lines of context.
                new_line += 1
                new_remaining -= 1
            # '-' (removed) and '\' (no-newline marker) consume no new-side line.
            continue

        git_header = _GIT_HEADER_RE.match(line)
        if git_header:
            # Register the new-side path straight away. An entry with no hunks
            # at all — a 100%-similarity rename, a mode change, a binary file —
            # never reaches a `+++` line, and would otherwise vanish from the
            # report entirely rather than showing up as changed-but-unmapped.
            old_path = git_header.group(1)
            current = _register(git_header.group(2))
            continue

        if line.startswith("--- "):
            old_path = _OLD_FILE_RE.match(line).group(1).rstrip("\t")
            continue

        if line.startswith("+++ "):
            path = _NEW_FILE_RE.match(line).group(1).rstrip("\t")
            if path == _DEV_NULL:
                # Deleted: no new-side path, so record it under the old one. It
                # carries no ranges and can only match at file granularity.
                if old_path and old_path != _DEV_NULL:
                    current = _register(old_path)
                    current.deleted = True
                else:
                    current = None
            else:
                current = _register(path)
            continue

        hunk = _HUNK_RE.match(line)
        if hunk:
            if current is None:
                continue
            new_start = int(hunk.group(3))
            new_count = 1 if hunk.group(4) is None else int(hunk.group(4))
            if current.deleted:
                # A new-side range would index a file that no longer exists.
                continue
            if new_count == 0:
                seam = max(1, new_start)
                current.ranges.append((seam, seam + 1))
            else:
                current.ranges.append((new_start, new_start + new_count - 1))
                new_line = new_start
                new_remaining = new_count
            continue

    for file_diff in files.values():
        file_diff.ranges = _merge_ranges(file_diff.ranges)
    return files


def subtract_ranges(
    ranges: list[tuple[int, int]], covers: list[tuple[int, int]]
) -> list[tuple[int, int]]:
    """The parts of `ranges` not covered by any range in `covers`."""
    remaining = _merge_ranges(list(ranges))
    for cover_start, cover_end in _merge_ranges(list(covers)):
        next_remaining: list[tuple[int, int]] = []
        for start, end in remaining:
            if cover_end < start or end < cover_start:
                next_remaining.append((start, end))
                continue
            if start < cover_start:
                next_remaining.append((start, cover_start - 1))
            if cover_end < end:
                next_remaining.append((cover_end + 1, end))
        remaining = next_remaining
    return remaining


def _merge_ranges(ranges: list[tuple[int, int]]) -> list[tuple[int, int]]:
    """Sort and coalesce overlapping or adjacent ranges."""
    if not ranges:
        return []
    ordered = sorted(ranges)
    merged = [ordered[0]]
    for start, end in ordered[1:]:
        last_start, last_end = merged[-1]
        if start <= last_end + 1:
            merged[-1] = (last_start, max(last_end, end))
        else:
            merged.append((start, end))
    return merged


# Config that would change the shape of the diff we parse. A developer with
# `diff.mnemonicPrefix` set would otherwise get `+++ w/src/f.cpp`, every path
# would fail to match the graph, and the tool would report "0 touched nodes"
# with no error at all.
_GIT_CONFIG_OVERRIDES = (
    "-c",
    "diff.mnemonicPrefix=false",
    "-c",
    "diff.noprefix=false",
    "-c",
    "core.quotePath=false",
)
# NB: no `-c diff.external=`. Setting it empty makes git try to exec the empty
# string ("external diff died"); `--no-ext-diff` on the diff itself is what
# actually disables an external differ.


def _git(repo_root: Path, *args: str) -> str:
    result = subprocess.run(
        ["git", "-C", str(repo_root), *_GIT_CONFIG_OVERRIDES, *args],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"git {' '.join(args)} failed ({result.returncode}): "
            f"{result.stderr.strip()}"
        )
    return result.stdout


def merge_base(repo_root: Path, base: str, head: str = "HEAD") -> str:
    """The commit `head` diverged from `base` at.

    Diffing against the merge base rather than the branch tip is what keeps
    unrelated commits landing on `base` out of the PR's diff.
    """
    return _git(repo_root, "merge-base", base, head).strip()


def diff_ranges(
    repo_root: Path, base: str, head: str | None = None
) -> dict[str, FileDiff]:
    """Changed line ranges between the merge base of `base` and `head`.

    `head` of None diffs the working tree, which is what a local run wants.
    """
    against = merge_base(repo_root, base, head or "HEAD")
    args = [
        "diff",
        "--unified=0",
        "--no-color",
        "--no-ext-diff",
        "--find-renames",
        against,
    ]
    if head is not None:
        args.append(head)
    diffs = parse_unified_diff(_git(repo_root, *args, "--"))
    if head is None:
        _add_untracked(repo_root, diffs)
    return diffs


def _add_untracked(repo_root: Path, diffs: dict[str, FileDiff]) -> None:
    """Fold untracked files into a working-tree diff.

    `git diff` only reports tracked files, so a brand-new transactor source that
    has not been `git add`ed yet would be invisible — the exact file a reviewer
    most wants mapped. Treated as wholly added.
    """
    listing = _git(repo_root, "ls-files", "--others", "--exclude-standard", "-z").split(
        "\0"
    )
    for rel in listing:
        if not rel or rel in diffs:
            continue
        path = Path(repo_root) / rel
        try:
            # Split on \n only: str.splitlines() also breaks on \x0b, \x0c and
            # U+0085, which git does not count as line endings.
            lines = path.read_text().split("\n")
            if lines and lines[-1] == "":
                lines.pop()
        except (OSError, UnicodeDecodeError):
            # Unreadable or binary: represent the path, claim no line ranges.
            diffs[rel] = FileDiff(rel)
            continue
        entry = FileDiff(rel)
        if lines:
            entry.ranges = [(1, len(lines))]
            entry.added_lines = {i: text for i, text in enumerate(lines, start=1)}
        diffs[rel] = entry


def resolve_rev(repo_root: Path, rev: str) -> str:
    return _git(repo_root, "rev-parse", rev).strip()
