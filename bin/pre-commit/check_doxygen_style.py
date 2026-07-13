#!/usr/bin/env python3
"""
Check C++ Doxygen comment style.

Enforces the house convention for documentation comments:

  * Use ``/** ... */`` blocks, not ``///``, ``//!`` or ``/*! ... */``; a plain
    ``/* ... */`` that contains Doxygen commands is a doc comment missing its
    second star. Trailing member-after comments use ``///<`` (not ``//!<``,
    ``/*!< ... */`` or ``/**< ... */`` -- the block forms get reflowed and
    mis-attached by clang-format on packed enum values, the line form does not).
  * ``/**`` sits alone on its line; the closing ``*/`` sits alone on its line.
  * Every content line is prefixed with `` * `` (no bare-indented continuation).
  * The first content line is flush (not over-indented).
  * Doxygen commands use the ``@cmd`` form, not ``\\cmd``.
  * Use ``@return`` / ``@throws`` rather than prose ``Returns:`` / ``Throws:``.
  * A plain ``//`` comment carrying a block-level ``@command`` (``@param``,
    ``@return``, ``@see``, ...) is documentation and must be a ``/** ... */``
    block (Doxygen ignores ``//``).
  * Use canonical command spellings: ``@return`` (not ``@returns``),
    ``@throws`` (not ``@throw``), ``@see`` (not ``@sa``).
  * Order block tags ``@tparam`` -> ``@param`` -> ``@return``. (Whether
    ``@param`` order matches the signature is not checked here -- too fragile to
    parse; Doxygen's WARN_IF_DOC_ERROR covers name mismatches.)
  * One-liners are expanded to three lines, EXCEPT bare markers ``@{`` / ``@}``
    / ``@cond [label]`` / ``@endcond`` / ``@file [name]`` which stay on one line.

Left intentionally alone (recognized, valid Doxygen that is not this style's
concern):

  * ``///<`` trailing "member-after" comments (the house form).
  * Divider lines made only of slashes (``//////////``).
  * Plain ``/* ... */`` (non-Doxygen) comments.

Usage:
    check_doxygen_style.py [FILE ...]      # explicit files
    check_doxygen_style.py                 # default: src/ and include/ trees

Exit status is non-zero if any violation is found.
"""

import argparse
import re
import sys
from collections.abc import Iterable, Iterator
from dataclasses import dataclass
from enum import Enum
from pathlib import Path


class Category(Enum):
    """A kind of style violation: a printed ``label`` and its ``description``.

    The description is the default message; a few categories whose wording
    depends on the offending text (see ``Finding.detail``) override it.
    """

    def __init__(self, label: str, description: str) -> None:
        self.label = label
        self.description = description

    BACKSLASH_COMMAND = ("backslash-command", "use the @cmd form, not \\cmd")
    WRONG_COMMAND = ("wrong-command", "use the canonical command spelling")
    TRIPLE_SLASH = ("triple-slash", "use a /** ... */ block instead of ///")
    QT_MEMBER = ("qt-member", "use ///< instead of //!<")
    QT_LINE = ("qt-line", "use a /** ... */ block instead of //!")
    BLOCK_MEMBER = ("block-member", "use ///< instead of /**<")
    QT_BLOCK_MEMBER = ("qt-block-member", "use ///< instead of /*!<")
    DOC_IN_LINE_COMMENT = (
        "doc-in-line-comment",
        "use a /** ... */ block for documentation, not //",
    )
    QT_COMMENT = ("qt-comment", "use /** instead of /*!")
    SINGLE_LINE_BLOCK = (
        "single-line-block",
        "expand one-line /** ... */ to a multi-line block "
        "(markers @{ @} @cond @endcond @file may stay)",
    )
    TEXT_ON_OPENER = ("text-on-opener", "move text off the /** opener line")
    BARE_CONTINUATION = ("bare-continuation", 'prefix continuation lines with " * "')
    OVER_INDENTED = ("over-indented", "first content line is over-indented")
    OVER_INDENTED_TAG = (
        "over-indented-tag",
        'Doxygen tag over-indented; use a single space after "*"',
    )
    COMBINED_MARKER = (
        "combined-marker",
        "scope marker @{ / @} should be its own single-line /** @{ */ block",
    )
    PROSE_LABEL = ("prose-label", "use a Doxygen tag instead of a prose label")
    CONTENT_ON_CLOSER = ("content-on-closer", "move content off the closing */ line")
    PLAIN_BLOCK_DOC = (
        "plain-block-doc",
        "documentation comment must open with /** not /*",
    )
    TAG_ORDER = (
        "tag-order",
        "block tags out of order; expected @tparam, then @param, then @return",
    )


@dataclass(frozen=True)
class Finding:
    """A single style violation at a 1-based line number.

    ``detail`` overrides the category's default description when the message
    depends on the offending text (e.g. which command was misspelled).
    """

    line: int
    category: Category
    detail: str | None = None

    @property
    def message(self) -> str:
        return self.detail if self.detail is not None else self.category.description


DEFAULT_ROOTS = ("src", "include")
EXTS = {".h", ".hpp", ".cpp", ".ipp", ".cxx", ".cc"}

# Every Doxygen command we recognize when written with a backslash (\cmd).
_ALL_COMMANDS = (
    "brief|param|tparam|return|returns|retval|note|warning|pre|post|see|sa|ref|"
    "throw|throws|exception|deprecated|details|code|endcode|verbatim|endverbatim|"
    "li|arg|c|internal|since|todo|attention|remark|remarks|ingroup|defgroup"
)
# Block-level tags whose over-indentation we flag inside a block body.
_BLOCK_TAGS = (
    "param|tparam|returns?|retval|brief|throws?|note|warning|"
    "pre|post|see|sa|details|deprecated"
)
# Tags that, appearing anywhere in a comment, mark it as documentation.
_ANY_DOC_TAGS = (
    "param|tparam|returns?|retval|brief|throws?|note|warning|pre|post|see|sa"
)
# Tags that make a plain // comment a mis-styled doc comment.
_LINE_DOC_TAGS = "brief|param|tparam|returns?|retval|throws?|note|see|pre|post"

# \cmd that should be @cmd.
RE_BACKSLASH_CMD = re.compile(r"\\(" + _ALL_COMMANDS + r")\b")
# Bare markers that may legitimately stay on a single line.
RE_MARKER = re.compile(r"^@(\{|\}|cond(\s.*)?|endcond|file(\s.*)?)$")
# Prose section labels that should be Doxygen tags.
RE_PROSE_LABEL = re.compile(r"^\*\s(Returns|Throws|Exceptions):\s*$")
# An over-indented block tag: "*" followed by 2+ spaces then the tag.
RE_OVERINDENTED_TAG = re.compile(r"^\*\s{2,}@(" + _BLOCK_TAGS + r")\b")
# Any documentation tag (used to spot a doc comment hiding in a plain /* */).
RE_ANY_DOC_TAG = re.compile(r"@(" + _ANY_DOC_TAGS + r")\b")
# A documentation tag inside a // comment.
RE_LINE_DOC_TAG = re.compile(r"@(" + _LINE_DOC_TAGS + r")\b")
# Order-relevant tags, for the @tparam -> @param -> @return ordering check.
RE_ORDER_TAG = re.compile(r"^\*\s*@(param|tparam|returns?|retval)\b")
# First content line indented by 2+ spaces after the "*".
RE_FIRST_OVERINDENT = re.compile(r"^\s*\*\s{2,}\S")
# A scope marker @{ / @} sharing a comment with other text.
RE_COMBINED_MARKER = re.compile(r"^\*\s*@[{}]\s*$")

# Non-canonical command spellings -> the house spelling (bare command names).
# Used both to flag a wrong @form and to suggest the right @form for a \wrong.
CANONICAL_COMMAND = {"returns": "return", "throw": "throws", "sa": "see"}
WRONG_SPELLINGS = [
    (re.compile(rf"@{wrong}\b"), f"@{right}")
    for wrong, right in CANONICAL_COMMAND.items()
]

# Order block tags should appear in; a body out of this order is a violation.
EXPECTED_TAG_ORDER = ("tparam", "param", "return")


def is_doxy_open(stripped: str) -> bool:
    """True for a line-start Doxygen block opener we should normalize."""
    if stripped.startswith("/*!"):  # Qt-style Doxygen
        return not stripped.startswith("/*!<")  # member-after, leave inline
    return (
        stripped.startswith("/**")
        and not stripped.startswith("/***")
        and not stripped.startswith("/**/")
        and not stripped.startswith("/**<")
    )


def _flag_commands(raw_line: str, stripped: str, index: int) -> list[Finding]:
    """Flag \\cmd and misspelled @cmd on a comment line (opener, body, or closer)."""
    if not stripped.startswith(("*", "//", "/*")):
        return []
    findings: list[Finding] = []
    backslash = RE_BACKSLASH_CMD.search(raw_line)
    if backslash:
        command = backslash.group(1)
        canonical = CANONICAL_COMMAND.get(command, command)
        findings.append(
            Finding(
                index + 1,
                Category.BACKSLASH_COMMAND,
                f"use @{canonical} instead of \\{command}",
            )
        )
    for pattern, replacement in WRONG_SPELLINGS:
        wrong = pattern.search(raw_line)
        if wrong:
            findings.append(
                Finding(
                    index + 1,
                    Category.WRONG_COMMAND,
                    f"use {replacement} instead of {wrong.group(0)}",
                )
            )
    return findings


def _flag_line_comment(raw_line: str, stripped: str, index: int) -> Finding | None:
    """Return the finding for a single-line comment form (///, //!, /**<, ...), else None."""
    if stripped.startswith("///") and not stripped.startswith(("////", "///<")):
        return Finding(index + 1, Category.TRIPLE_SLASH)
    if "//!<" in raw_line:
        return Finding(index + 1, Category.QT_MEMBER)
    if stripped.startswith("//!"):
        return Finding(index + 1, Category.QT_LINE)
    if "/**<" in raw_line:
        return Finding(index + 1, Category.BLOCK_MEMBER)
    if "/*!<" in raw_line:
        return Finding(index + 1, Category.QT_BLOCK_MEMBER)
    if stripped.startswith("//") and RE_LINE_DOC_TAG.search(stripped):
        return Finding(index + 1, Category.DOC_IN_LINE_COMMENT)
    return None


def _flag_single_line_block(stripped: str, line_no: int, is_qt: bool) -> list[Finding]:
    """Findings for a whole /** ... */ or /*! ... */ block on one line."""
    inner = re.sub(r"^/\*[*!]", "", stripped)
    inner = re.sub(r"\*/\s*$", "", inner).strip()
    findings: list[Finding] = []
    if is_qt:
        findings.append(Finding(line_no, Category.QT_COMMENT))
    if inner and not RE_MARKER.match(inner):
        findings.append(Finding(line_no, Category.SINGLE_LINE_BLOCK))
    return findings


def _canonical_order_tag(body: str) -> str | None:
    """The order-relevant tag (tparam/param/return) a body line opens with, if any."""
    match = RE_ORDER_TAG.match(body)
    if match is None:
        return None
    command = match.group(1)
    return "return" if command in ("return", "returns", "retval") else command


def _flag_body_line(
    body_line: str, line_no: int, is_first_content: bool
) -> list[Finding]:
    """Findings for one interior line of a multi-line block."""
    body = body_line.strip()
    findings: list[Finding] = []
    if body and not body.startswith("*"):
        findings.append(Finding(line_no, Category.BARE_CONTINUATION))
    if body.startswith("*"):
        if is_first_content and RE_FIRST_OVERINDENT.match(body_line):
            findings.append(Finding(line_no, Category.OVER_INDENTED))
        if RE_OVERINDENTED_TAG.match(body):
            findings.append(Finding(line_no, Category.OVER_INDENTED_TAG))
        if RE_COMBINED_MARKER.match(body):
            findings.append(Finding(line_no, Category.COMBINED_MARKER))
        label = RE_PROSE_LABEL.match(body)
        if label:
            suggested_tag = "@return" if label.group(1) == "Returns" else "@throws"
            findings.append(
                Finding(
                    line_no,
                    Category.PROSE_LABEL,
                    f'use {suggested_tag} instead of prose "{label.group(1)}:"',
                )
            )
    return findings


def _flag_closer(closer_line: str, line_no: int) -> list[Finding]:
    """Findings for content sharing the closing */ line."""
    before = closer_line[: closer_line.index("*/")].strip()
    if before and before != "*":
        return [Finding(line_no, Category.CONTENT_ON_CLOSER)]
    return []


def _flag_tag_order(first_tag_line: dict[str, int]) -> list[Finding]:
    """One finding if the present block tags are not in EXPECTED_TAG_ORDER."""
    tag_lines = [
        first_tag_line[tag] for tag in EXPECTED_TAG_ORDER if tag in first_tag_line
    ]
    if tag_lines != sorted(tag_lines):
        return [Finding(min(tag_lines), Category.TAG_ORDER)]
    return []


def _flag_doxy_block(lines: list[str], start: int) -> tuple[int, list[Finding]]:
    """Handle a /** or /*! block opening at ``start``; return (next index, findings)."""
    raw_line = lines[start]
    stripped = raw_line.lstrip()
    open_pos = raw_line.index("/*")
    is_qt = stripped.startswith("/*!")

    # A whole block on one line: /** ... */.
    if "*/" in raw_line[open_pos + 2 :]:
        return start + 1, _flag_single_line_block(stripped, start + 1, is_qt)

    # Multi-line block: opener, then scan the body to the closer.
    findings: list[Finding] = []
    if is_qt:
        findings.append(Finding(start + 1, Category.QT_COMMENT))
    if raw_line[open_pos + 3 :].strip():
        findings.append(Finding(start + 1, Category.TEXT_ON_OPENER))

    line_count = len(lines)
    cursor = start + 1
    is_first_content = True
    first_tag_line: dict[str, int] = {}  # canonical tag -> 1-based first line
    while cursor < line_count and "*/" not in lines[cursor]:
        body_line = lines[cursor]
        body = body_line.strip()
        findings.extend(_flag_commands(body_line, body, cursor))
        tag = _canonical_order_tag(body)
        if tag is not None:
            first_tag_line.setdefault(tag, cursor + 1)
        findings.extend(_flag_body_line(body_line, cursor + 1, is_first_content))
        if body.startswith("*"):
            is_first_content = False
        cursor += 1

    if cursor < line_count:
        closer_line = lines[cursor]
        findings.extend(_flag_commands(closer_line, closer_line.strip(), cursor))
        findings.extend(_flag_closer(closer_line, cursor + 1))
    findings.extend(_flag_tag_order(first_tag_line))

    return cursor + 1, findings


def _flag_plain_block(lines: list[str], start: int) -> tuple[int, list[Finding]]:
    """Handle a line-start plain /* ... */ block; return (next index, findings).

    Only flagged when it hides a documentation command (a missing second star).
    """
    line_count = len(lines)
    cursor = start
    while cursor < line_count and "*/" not in lines[cursor]:
        cursor += 1
    findings: list[Finding] = []
    # The opener (start) is command-checked by check_file; check the rest here.
    for i in range(start + 1, min(cursor + 1, line_count)):
        findings.extend(_flag_commands(lines[i], lines[i].strip(), i))
    block_text = "\n".join(
        lines[start : cursor + 1] if cursor < line_count else lines[start:]
    )
    if RE_ANY_DOC_TAG.search(block_text):
        findings.append(Finding(start + 1, Category.PLAIN_BLOCK_DOC))
    next_index = cursor + 1 if cursor < line_count else line_count
    return next_index, findings


def check_source(text: str) -> list[Finding]:
    """Return all style violations found in the given source text."""
    lines = text.split("\n")
    findings: list[Finding] = []
    line_count = len(lines)
    index = 0
    in_plain_block = False  # inside a mid-line, non-Doxygen /* ... */
    while index < line_count:
        raw_line = lines[index]
        stripped = raw_line.lstrip()

        # Skip the interior of a plain block opened on an earlier line.
        if in_plain_block:
            in_plain_block = "*/" not in raw_line
            index += 1
            continue

        findings.extend(_flag_commands(raw_line, stripped, index))

        line_finding = _flag_line_comment(raw_line, stripped, index)
        if line_finding is not None:
            findings.append(line_finding)
            index += 1
        elif is_doxy_open(stripped):
            index, block_findings = _flag_doxy_block(lines, index)
            findings.extend(block_findings)
        elif stripped.startswith("/*"):
            index, block_findings = _flag_plain_block(lines, index)
            findings.extend(block_findings)
        else:
            # A /* that opens mid-line without closing starts a plain block.
            if "/*" in raw_line and not stripped.startswith("//"):
                if "*/" not in raw_line[raw_line.index("/*") + 2 :]:
                    in_plain_block = True
            index += 1
    return findings


def check_file(path: Path) -> list[Finding]:
    """Return all style violations found in one file."""
    return check_source(path.read_text(encoding="utf-8"))


def iter_files(paths: Iterable[str]) -> Iterator[Path]:
    """Yield every C++ source file among the given files and directories."""
    for raw_path in paths:
        path = Path(raw_path)
        if path.is_dir():
            for candidate in path.rglob("*"):
                if candidate.is_file() and candidate.suffix in EXTS:
                    yield candidate
        elif path.suffix in EXTS:
            yield path


def main() -> int:
    parser = argparse.ArgumentParser(description="Check Doxygen comment style.")
    parser.add_argument(
        "files", nargs="*", help="files or directories (default: src/ include/)"
    )
    parser.add_argument(
        "-q", "--quiet", action="store_true", help="only print the summary count"
    )
    args = parser.parse_args()
    roots = args.files or [root for root in DEFAULT_ROOTS if Path(root).is_dir()]

    total = 0
    for path in sorted(set(iter_files(roots)), key=str):
        for finding in check_file(path):
            total += 1
            if not args.quiet:
                print(
                    f"{path}:{finding.line}: {finding.category.label}: {finding.message}"
                )
    print(f"\n{total} doxygen-style violation(s)", file=sys.stderr)
    return 1 if total else 0


if __name__ == "__main__":
    sys.exit(main())
