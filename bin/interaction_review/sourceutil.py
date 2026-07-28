#!/usr/bin/env python3
"""Small source-text helpers shared by the extractors.

Everything here preserves line numbering: node locations are line spans into
real files, so a helper that silently dropped or added a line would corrupt
every span derived from its output.
"""

from __future__ import annotations

from pathlib import Path


def strip_comments(text: str) -> str:
    """Blank out // and /* */ comments, preserving line count and offsets.

    Comment bodies become spaces and newlines are kept verbatim, so any line or
    column computed from the result also holds for the original text. String and
    character literals are honored, so a `//` inside a literal is not a comment.
    """
    out: list[str] = []
    i, n = 0, len(text)
    while i < n:
        ch = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if ch == "/" and nxt == "/":
            while i < n and text[i] != "\n":
                out.append(" ")
                i += 1
        elif ch == "/" and nxt == "*":
            out.append("  ")
            i += 2
            while i < n and not (text[i] == "*" and text[i + 1 : i + 2] == "/"):
                out.append("\n" if text[i] == "\n" else " ")
                i += 1
            out.append("  ")
            i += 2
        elif ch in "\"'":
            quote = ch
            out.append(ch)
            i += 1
            while i < n and text[i] != quote:
                # A backslash escapes the next character, including the quote.
                if text[i] == "\\" and i + 1 < n:
                    out.append(text[i])
                    out.append(text[i + 1])
                    i += 2
                    continue
                out.append(text[i])
                i += 1
            if i < n:
                out.append(text[i])
                i += 1
        else:
            out.append(ch)
            i += 1
    return "".join(out)


def repo_relative(path: Path | str, repo_root: Path | str) -> str:
    """Repo-relative POSIX path, so locations are portable across checkouts."""
    rel = Path(path).resolve().relative_to(Path(repo_root).resolve())
    return rel.as_posix()


def line_count(path: Path) -> int:
    """Number of lines in a file, counting a trailing partial line.

    Counts `\n` only. `str.splitlines()` also breaks on \x0b, \x0c, \x1c-\x1e and
    U+0085, which git does not treat as line endings — a file containing one
    would get a whole-file span longer than the file git sees.

    Never returns 0: an empty file still spans line 1, and a Location with
    end_line 0 would be rejected as malformed.
    """
    text = path.read_text()
    return max(1, text.count("\n") + (0 if text.endswith("\n") else 1))


def whole_file_location(path: Path, repo_root: Path | str, role: str):
    """A Location covering an entire file — used where a node is attributed to a
    file but not to a span within it (a transactor's implementation file).

    Imported lazily so this module stays the dependency-free text layer its
    docstring promises; `graph` imports nothing from here.
    """
    from graph import Location

    return Location(
        file=repo_relative(path, repo_root),
        start_line=1,
        end_line=line_count(path),
        role=role,
    )
