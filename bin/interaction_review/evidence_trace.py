#!/usr/bin/env python3
"""Track which source lines an AI judgement actually observed.

Filesystem citation validation proves that a cited path and line exist.  It does
not prove that the model read them.  ``EvidenceTrace`` records the exact line
spans exposed by the judge's read-only tools and can therefore distinguish a
grounded citation from a plausible-looking citation to an unread line.

The trace deliberately receives the *final* tool-result string (after any
output cap) rather than reading files itself.  This keeps its claim narrow: a
line is observed only when its number was present in content sent to the model.
Raw source is not retained in the audit artifact.  Exact result and argument
hashes make the compact metadata pairable with a captured transcript when one
is available.
"""

from __future__ import annotations

import copy
import hashlib
import json
import re
import shlex
from collections.abc import Mapping
from dataclasses import dataclass
from pathlib import Path
from typing import Any

TRACE_VERSION = 1
HASH_ALGORITHM = "sha256"

REVISION_WORKING = "working"
REVISION_NEW = "new"
REVISION_OLD = "old"
_REVISIONS = frozenset({REVISION_WORKING, REVISION_NEW, REVISION_OLD})

_READ_HEADER_RE = re.compile(r"^(.*) lines (\d+)-(\d+) of (\d+):$")
_READ_LINE_RE = re.compile(r"^(\d+)\t")
_GREP_LINE_RE = re.compile(r"^(.+?):([1-9]\d*):(.*)$")
_HUNK_RE = re.compile(r"^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@(?:.*)$")
_GREP_TRUNCATION_RE = re.compile(
    r"^\.\.\. \d+ more matches; narrow the pattern or path\.$"
)
_GENERIC_TRUNCATION = "... truncated; request a narrower range."


def _sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def _text_hash(value: str) -> str:
    # ``surrogatepass`` makes the hash total over Python strings while retaining
    # a byte-for-byte distinction between surrogate-containing tool output.
    return _sha256_bytes(value.encode("utf-8", errors="surrogatepass"))


def _canonical_json(value: Any) -> bytes:
    return json.dumps(
        value,
        sort_keys=True,
        separators=(",", ":"),
        ensure_ascii=True,
        allow_nan=False,
    ).encode("utf-8")


def normalize_repo_path(repo_root: Path, raw_path: str) -> str:
    """Return a canonical repo-relative POSIX path, rejecting escapes.

    ``resolve(strict=False)`` also handles paths for deleted diff entries while
    still resolving any existing symlink prefix.  Consequently an observed path
    cannot be made to alias a file outside the repository.
    """

    if not isinstance(raw_path, str) or not raw_path:
        raise ValueError("path must be a non-empty string")

    root = repo_root.resolve()
    supplied = Path(raw_path)
    candidate = (
        supplied.resolve() if supplied.is_absolute() else (root / supplied).resolve()
    )
    if not candidate.is_relative_to(root) or candidate == root:
        raise ValueError(f"path escapes the repository: {raw_path}")
    return candidate.relative_to(root).as_posix()


@dataclass(frozen=True, order=True)
class ObservedSpan:
    """An inclusive, contiguous line range visible in one tool result."""

    file: str
    start_line: int
    end_line: int
    revision: str

    def __post_init__(self) -> None:
        if self.start_line < 1 or self.end_line < self.start_line:
            raise ValueError("an observed span must be a non-empty 1-based range")
        if self.revision not in _REVISIONS:
            raise ValueError(f"unknown revision: {self.revision}")

    def to_dict(self) -> dict[str, Any]:
        return {
            "file": self.file,
            "start_line": self.start_line,
            "end_line": self.end_line,
            "revision": self.revision,
        }


def _compress_lines(lines: set[tuple[str, int, str]]) -> list[ObservedSpan]:
    """Compress observations without filling gaps between returned lines."""

    ordered = sorted(lines, key=lambda item: (item[0], item[2], item[1]))
    spans: list[ObservedSpan] = []
    for file, line, revision in ordered:
        if (
            spans
            and spans[-1].file == file
            and spans[-1].revision == revision
            and spans[-1].end_line + 1 == line
        ):
            previous = spans[-1]
            spans[-1] = ObservedSpan(
                file=previous.file,
                start_line=previous.start_line,
                end_line=line,
                revision=previous.revision,
            )
        else:
            spans.append(ObservedSpan(file, line, line, revision))
    return spans


def _safe_normalize(repo_root: Path, raw_path: str | None) -> str | None:
    if not raw_path or raw_path == "/dev/null":
        return None
    try:
        return normalize_repo_path(repo_root, raw_path)
    except (OSError, ValueError):
        # A malformed result must not turn into evidence or break an advisory
        # judgement.  Its exact bytes are still represented by ``result_sha256``.
        return None


def _parse_read_file(repo_root: Path, result: str) -> set[tuple[str, int, str]]:
    lines = result.splitlines()
    if not lines:
        return set()
    header = _READ_HEADER_RE.match(lines[0])
    if not header:
        return set()

    path = _safe_normalize(repo_root, header.group(1))
    if path is None:
        return set()
    start, end = int(header.group(2)), int(header.group(3))
    if start < 1 or end < start:
        return set()

    observed: set[tuple[str, int, str]] = set()
    for line_text in lines[1:]:
        match = _READ_LINE_RE.match(line_text)
        if match:
            line = int(match.group(1))
            if start <= line <= end:
                observed.add((path, line, REVISION_WORKING))
    return observed


def _parse_grep(repo_root: Path, result: str) -> set[tuple[str, int, str]]:
    observed: set[tuple[str, int, str]] = set()
    for line_text in result.splitlines():
        match = _GREP_LINE_RE.match(line_text)
        if not match:
            continue
        path = _safe_normalize(repo_root, match.group(1))
        if path is not None:
            observed.add((path, int(match.group(2)), REVISION_WORKING))
    return observed


def _header_token(text: str) -> str | None:
    """Extract one possibly quoted path token from a unified-diff header."""

    try:
        tokens = shlex.split(text, posix=True)
    except ValueError:
        return None
    return tokens[0] if tokens else None


def _normalized_diff_token(
    repo_root: Path, token: str | None, prefix: str
) -> str | None:
    if token is None or token == "/dev/null":
        return None
    if token.startswith(prefix):
        token = token[len(prefix) :]
    return _safe_normalize(repo_root, token)


def _diff_path(repo_root: Path, text: str, prefix: str) -> str | None:
    return _normalized_diff_token(repo_root, _header_token(text), prefix)


def _parse_git_diff(
    repo_root: Path, args: Mapping[str, Any], result: str
) -> set[tuple[str, int, str]]:
    """Map every displayed unified-diff body line to its old/new line number."""

    fallback = _safe_normalize(repo_root, str(args.get("path", "")))
    old_path = fallback
    new_path = fallback
    old_line = new_line = 0
    old_remaining = new_remaining = 0
    in_hunk = False
    observed: set[tuple[str, int, str]] = set()

    for line_text in result.splitlines():
        hunk = _HUNK_RE.match(line_text)
        if hunk:
            old_line = int(hunk.group(1))
            new_line = int(hunk.group(3))
            old_remaining = int(hunk.group(2) or 1)
            new_remaining = int(hunk.group(4) or 1)
            in_hunk = True
            continue

        if not in_hunk:
            if line_text.startswith("diff --git "):
                try:
                    tokens = shlex.split(line_text[len("diff --git ") :], posix=True)
                except ValueError:
                    tokens = []
                if len(tokens) >= 2:
                    old_path = _normalized_diff_token(repo_root, tokens[0], "a/")
                    new_path = _normalized_diff_token(repo_root, tokens[1], "b/")
            elif line_text.startswith("--- "):
                old_path = _diff_path(repo_root, line_text[4:], "a/")
            elif line_text.startswith("+++ "):
                new_path = _diff_path(repo_root, line_text[4:], "b/")
            continue

        if line_text.startswith("\\"):
            continue

        prefix = line_text[:1]
        if prefix == " ":
            if old_remaining > 0 and old_path is not None:
                observed.add((old_path, old_line, REVISION_OLD))
            if new_remaining > 0 and new_path is not None:
                observed.add((new_path, new_line, REVISION_NEW))
            old_line += 1
            new_line += 1
            old_remaining = max(0, old_remaining - 1)
            new_remaining = max(0, new_remaining - 1)
        elif prefix == "-" and old_remaining > 0:
            if old_path is not None:
                observed.add((old_path, old_line, REVISION_OLD))
            old_line += 1
            old_remaining -= 1
        elif prefix == "+" and new_remaining > 0:
            if new_path is not None:
                observed.add((new_path, new_line, REVISION_NEW))
            new_line += 1
            new_remaining -= 1

        if old_remaining == 0 and new_remaining == 0:
            in_hunk = False

    return observed


def _looks_truncated(result: str) -> bool:
    lines = result.splitlines()
    if not lines:
        return False
    tail = lines[-1]
    return tail == _GENERIC_TRUNCATION or bool(_GREP_TRUNCATION_RE.match(tail))


class EvidenceTrace:
    """Accumulate tool observations for one model conversation."""

    def __init__(self, repo_root: Path):
        self._repo_root = repo_root.resolve()
        self._calls: list[dict[str, Any]] = []
        self._observed: set[tuple[str, int, str]] = set()

    def record(
        self,
        tool: str,
        args: Mapping[str, Any],
        result: str,
        *,
        is_error: bool = False,
        truncated: bool | None = None,
    ) -> dict[str, Any]:
        """Record exactly one result after it has been shaped for the model.

        The returned call record is a detached copy suitable for immediate
        logging.  Unsupported tool names are still audited, but yield no source
        observations.
        """

        if not isinstance(tool, str) or not tool:
            raise ValueError("tool must be a non-empty string")
        if not isinstance(args, Mapping):
            raise TypeError("tool args must be a mapping")
        if not isinstance(result, str):
            raise TypeError("tool result must be text")

        # Round-tripping establishes JSON serializability and prevents later
        # mutation of a model SDK's input mapping from changing the audit trail.
        args_copy = json.loads(_canonical_json(dict(args)).decode("utf-8"))
        normalized_path = None
        if isinstance(args_copy.get("path"), str):
            normalized_path = _safe_normalize(self._repo_root, args_copy["path"])

        observations: set[tuple[str, int, str]] = set()
        if not is_error:
            if tool == "read_file":
                observations = _parse_read_file(self._repo_root, result)
            elif tool == "grep":
                observations = _parse_grep(self._repo_root, result)
            elif tool == "git_diff":
                observations = _parse_git_diff(self._repo_root, args_copy, result)

        spans = _compress_lines(observations)
        call: dict[str, Any] = {
            "sequence": len(self._calls) + 1,
            "tool": tool,
            "args": args_copy,
            "args_sha256": _sha256_bytes(_canonical_json(args_copy)),
            "normalized_path": normalized_path,
            "result_sha256": _text_hash(result),
            "result_chars": len(result),
            "is_error": bool(is_error),
            "truncated": (
                _looks_truncated(result) if truncated is None else bool(truncated)
            ),
            "spans": [span.to_dict() for span in spans],
        }
        call["record_sha256"] = _sha256_bytes(_canonical_json(call))
        self._calls.append(call)
        self._observed.update(observations)
        return copy.deepcopy(call)

    def record_tool_result(
        self,
        tool: str,
        args: Mapping[str, Any],
        result: str,
        *,
        is_error: bool = False,
        truncated: bool | None = None,
    ) -> dict[str, Any]:
        """Spelled-out alias for callers where ``record`` is ambiguous."""

        return self.record(tool, args, result, is_error=is_error, truncated=truncated)

    def was_observed(
        self, file: str, line: int, *, revision: str | None = None
    ) -> bool:
        """Whether ``file:line`` occurred in a result sent to the model.

        With no revision, any source of observation counts.  Pass ``working``,
        ``new``, or ``old`` when a caller needs to distinguish ordinary reads
        and either side of a diff.
        """

        if not isinstance(line, int) or isinstance(line, bool) or line < 1:
            return False
        if revision is not None and revision not in _REVISIONS:
            return False
        try:
            normalized = normalize_repo_path(self._repo_root, file)
        except (OSError, ValueError):
            return False
        return any(
            observed_file == normalized
            and observed_line == line
            and (revision is None or observed_revision == revision)
            for observed_file, observed_line, observed_revision in self._observed
        )

    def citation_observed(
        self, citation: Mapping[str, Any], *, revision: str | None = None
    ) -> bool:
        """Apply :meth:`was_observed` to a verdict citation object."""

        if not isinstance(citation, Mapping):
            return False
        file = citation.get("file")
        line = citation.get("line")
        if not isinstance(file, str):
            return False
        return self.was_observed(file, line, revision=revision)

    def audit_metadata(self) -> dict[str, Any]:
        """Return detached, JSON-serializable, content-addressed metadata."""

        body: dict[str, Any] = {
            "version": TRACE_VERSION,
            "hash_algorithm": HASH_ALGORITHM,
            "calls": copy.deepcopy(self._calls),
            "observed_spans": [
                span.to_dict() for span in _compress_lines(self._observed)
            ],
        }
        body["trace_sha256"] = _sha256_bytes(_canonical_json(body))
        return body

    def to_dict(self) -> dict[str, Any]:
        """Alias used by artifact serializers."""

        return self.audit_metadata()
