"""Immutable, least-privilege source access for AI review conversations.

The static selector says which ``base`` and ``head`` a review describes.  A
judge must read that exact source, even when another branch is currently
checked out.  ``SourceSnapshot`` binds those revisions to the three operations
the model needs: bounded file reads, grep, and per-file diffs.

Committed heads are naturally immutable and every operation is served directly
from Git objects.  ``head == "worktree"`` is different: its meaning can drift
after selection.  Selection must therefore persist :meth:`binding_metadata`
and judging must pass it back through :meth:`from_selected`.  The binding hashes
the complete tracked diff against ``base`` plus the checked-out commit and is
checked before and after every model-visible operation.

The access policy is intentionally narrower than repository containment.  Only
tracked files under source/test/include roots are visible.  Git metadata,
configuration, interaction-review implementation and outputs, credentials, and
oracle/evaluation artifacts are denied even if the model guesses their paths.
"""

from __future__ import annotations

import hashlib
import json
import re
import subprocess
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Any, Callable, Mapping, TypeVar

BINDING_VERSION = 1
HEAD_WORKTREE = "worktree"

DEFAULT_MAX_READ_LINES = 400
DEFAULT_MAX_GREP_MATCHES = 60
MAX_SOURCE_BYTES = 16 * 1024 * 1024
GIT_TIMEOUT_SECONDS = 60

_REVIEWABLE_ROOTS = frozenset({"src", "include", "test", "tests"})
_DENIED_COMPONENTS = frozenset(
    {
        ".git",
        ".aws",
        ".ssh",
        "cfg",
        "out",
        "oracle",
        "oracles",
        "evaluation",
        "evaluations",
        "eval",
        "evals",
        "credentials",
        "secrets",
    }
)
_DENIED_EXACT_NAMES = frozenset(
    {
        ".env",
        ".envrc",
        "credentials",
        "credentials.json",
        "id_rsa",
        "id_ed25519",
        "known_hosts",
        "selected.json",
        "judged.json",
        "evaluation.json",
        "evaluation.md",
        "oracle.json",
    }
)
_DENIED_SECRET_SUFFIXES = frozenset(
    {".key", ".pem", ".p12", ".pfx", ".jks", ".keystore"}
)
_ARTIFACT_SUFFIXES = frozenset({".json", ".jsonl", ".yaml", ".yml", ".md"})
_GLOB_CHARS = frozenset("*?[")
_GREP_LINE = re.compile(r"^([^:]+):([1-9]\d*):(.*)$")

# Stabilize and de-power every Git subprocess.  In particular, no repository
# diff driver or textconv command may execute just because a PR added an
# attribute/configuration entry.
_GIT_CONFIG = (
    "-c",
    "color.ui=false",
    "-c",
    "core.quotepath=false",
    "-c",
    "diff.external=",
    "-c",
    "diff.renames=false",
)


class SnapshotError(RuntimeError):
    """Base class for source-snapshot failures."""


class SnapshotAccessError(SnapshotError):
    """A requested path is outside the model's source-only capability."""


class SnapshotBindingError(SnapshotError):
    """Selected revision metadata is absent or inconsistent."""


class SnapshotDriftError(SnapshotBindingError):
    """A bound worktree changed after selection/capture."""


@dataclass(frozen=True)
class SourceBinding:
    """Serializable identity of the source exposed to one judge run."""

    version: int
    mode: str
    base: str
    head: str
    checkout_head: str | None
    tracked_diff_sha256: str
    fingerprint: str

    def to_dict(self) -> dict[str, Any]:
        return {
            "version": self.version,
            "mode": self.mode,
            "base": self.base,
            "head": self.head,
            "checkout_head": self.checkout_head,
            "tracked_diff_sha256": self.tracked_diff_sha256,
            "fingerprint": self.fingerprint,
        }

    @classmethod
    def from_mapping(cls, value: Mapping[str, Any]) -> "SourceBinding":
        if not isinstance(value, Mapping):
            raise SnapshotBindingError("source binding must be an object")
        required = {
            "version",
            "mode",
            "base",
            "head",
            "checkout_head",
            "tracked_diff_sha256",
            "fingerprint",
        }
        missing = sorted(required - set(value))
        extra = sorted(set(value) - required)
        if missing or extra:
            raise SnapshotBindingError(
                f"invalid source binding fields; missing={missing}, extra={extra}"
            )
        try:
            binding = cls(**{key: value[key] for key in required})
        except TypeError as error:
            raise SnapshotBindingError(f"invalid source binding: {error}") from error
        binding._validate_shape()
        return binding

    def _validate_shape(self) -> None:
        if isinstance(self.version, bool) or self.version != BINDING_VERSION:
            raise SnapshotBindingError(
                f"unsupported source binding version: {self.version}"
            )
        if self.mode not in {"commit", HEAD_WORKTREE}:
            raise SnapshotBindingError(f"unsupported source binding mode: {self.mode}")
        for name, value in (
            ("base", self.base),
            ("head", self.head),
            ("tracked_diff_sha256", self.tracked_diff_sha256),
            ("fingerprint", self.fingerprint),
        ):
            if not isinstance(value, str) or not value:
                raise SnapshotBindingError(f"source binding {name} must be text")
        if self.checkout_head is not None and not isinstance(self.checkout_head, str):
            raise SnapshotBindingError(
                "source binding checkout_head must be text or null"
            )
        for name, value in (
            ("tracked_diff_sha256", self.tracked_diff_sha256),
            ("fingerprint", self.fingerprint),
        ):
            if not re.fullmatch(r"[0-9a-f]{64}", value):
                raise SnapshotBindingError(f"source binding {name} is not sha256")
        if self.mode == "commit" and self.checkout_head is not None:
            raise SnapshotBindingError("commit binding must not have checkout_head")
        if self.mode == HEAD_WORKTREE and (
            self.head != HEAD_WORKTREE or self.checkout_head is None
        ):
            raise SnapshotBindingError(
                "worktree binding requires head=worktree and checkout_head"
            )
        if not re.fullmatch(r"[0-9a-f]{40}", self.base):
            raise SnapshotBindingError("source binding base is not a full commit id")
        if self.mode == "commit" and not re.fullmatch(r"[0-9a-f]{40}", self.head):
            raise SnapshotBindingError("source binding head is not a full commit id")
        if self.checkout_head is not None and not re.fullmatch(
            r"[0-9a-f]{40}", self.checkout_head
        ):
            raise SnapshotBindingError(
                "source binding checkout_head is not a full commit id"
            )


_T = TypeVar("_T")


class SourceSnapshot:
    """Bound, source-only view used by model tools and citation validation."""

    def __init__(
        self,
        repo_root: Path,
        base: str,
        head: str,
        *,
        expected_binding: Mapping[str, Any] | SourceBinding | None = None,
    ):
        self.repo_root = Path(repo_root).resolve()
        self._require_repo_root()
        self.base = self._resolve_commit(base, "base")
        self.mode = HEAD_WORKTREE if head == HEAD_WORKTREE else "commit"
        self.head = (
            HEAD_WORKTREE
            if self.mode == HEAD_WORKTREE
            else self._resolve_commit(head, "head")
        )
        self._binding = self._current_binding()

        if expected_binding is not None:
            expected = (
                expected_binding
                if isinstance(expected_binding, SourceBinding)
                else SourceBinding.from_mapping(expected_binding)
            )
            if expected != self._binding:
                raise SnapshotDriftError(
                    self._binding_mismatch(expected, self._binding)
                )

    @classmethod
    def capture(cls, repo_root: Path, *, base: str, head: str) -> "SourceSnapshot":
        """Capture a binding for selection or a committed-head judge run."""

        return cls(repo_root, base, head)

    @classmethod
    def from_selected(
        cls,
        repo_root: Path,
        selected: Mapping[str, Any],
        *,
        expected_binding: Mapping[str, Any] | SourceBinding | None = None,
        require_worktree_binding: bool = True,
    ) -> "SourceSnapshot":
        """Bind the revisions named by ``selected.json``.

        Future selected artifacts can carry ``source_binding`` directly.  Until
        its schema is integrated, callers may pass the same sidecar object via
        ``expected_binding``.  Secure worktree judging refuses an unbound legacy
        artifact by default; committed heads need no sidecar because the commit
        id is already content-addressed.
        """

        if not isinstance(selected, Mapping):
            raise SnapshotBindingError("selected report must be an object")
        base = selected.get("base")
        head = selected.get("head")
        if not isinstance(base, str) or not base:
            raise SnapshotBindingError("selected.base must be non-empty text")
        if not isinstance(head, str) or not head:
            raise SnapshotBindingError("selected.head must be non-empty text")
        embedded = selected.get("source_binding")
        binding = expected_binding if expected_binding is not None else embedded
        if head == HEAD_WORKTREE and binding is None and require_worktree_binding:
            raise SnapshotBindingError(
                "worktree selected artifact has no source binding; capture one "
                "during selection and persist SourceSnapshot.binding_metadata()"
            )
        return cls(repo_root, base, head, expected_binding=binding)

    def binding_metadata(self) -> dict[str, Any]:
        """Detached JSON metadata to persist beside/inside ``selected.json``."""

        return dict(self._binding.to_dict())

    @property
    def fingerprint(self) -> str:
        return self._binding.fingerprint

    def validate_pre(self) -> None:
        """Validate the source binding immediately before a judge run/action."""

        self._validate_binding("before source access")

    def validate_post(self) -> None:
        """Validate the source binding immediately after a judge run/action."""

        self._validate_binding("after source access")

    def read_file(
        self,
        path: str,
        *,
        start_line: int = 1,
        end_line: int | None = None,
        max_lines: int = DEFAULT_MAX_READ_LINES,
    ) -> str:
        """Return a numbered slice from the bound head, never the wrong checkout."""

        def operation() -> str:
            normalized = self._normalize_path(path)
            content = self._read_source_bytes(normalized)
            if len(content) > MAX_SOURCE_BYTES:
                raise SnapshotAccessError(
                    f"source file exceeds {MAX_SOURCE_BYTES} bytes: {normalized}"
                )
            lines = content.decode("utf-8", errors="replace").splitlines()
            if isinstance(start_line, bool) or not isinstance(start_line, int):
                raise SnapshotAccessError("start_line must be an integer")
            if end_line is not None and (
                isinstance(end_line, bool) or not isinstance(end_line, int)
            ):
                raise SnapshotAccessError("end_line must be an integer")
            if (
                isinstance(max_lines, bool)
                or not isinstance(max_lines, int)
                or max_lines < 1
            ):
                raise SnapshotAccessError("max_lines must be a positive integer")
            start = max(1, start_line)
            requested_end = end_line if end_line is not None else start + max_lines - 1
            end = min(max(start - 1, requested_end), start + max_lines - 1, len(lines))
            if start > len(lines):
                return f"{normalized} has {len(lines)} lines; {start} is past the end."
            body = "\n".join(
                f"{number}\t{lines[number - 1]}" for number in range(start, end + 1)
            )
            return f"{normalized} lines {start}-{end} of {len(lines)}:\n{body}"

        return self._guarded(operation)

    def grep(
        self,
        pattern: str,
        *,
        path: str | None = None,
        max_matches: int = DEFAULT_MAX_GREP_MATCHES,
    ) -> str:
        """Search tracked, allowed files in the bound head with POSIX ERE."""

        def operation() -> str:
            if not isinstance(pattern, str) or not pattern:
                raise SnapshotAccessError("grep pattern must be non-empty text")
            if (
                isinstance(max_matches, bool)
                or not isinstance(max_matches, int)
                or max_matches < 1
            ):
                raise SnapshotAccessError("max_matches must be a positive integer")
            pathspecs = (
                [self._normalize_path(path, allow_glob=True)]
                if path is not None
                else sorted(_REVIEWABLE_ROOTS)
            )
            args = ["grep", "-n", "-I", "-E", "--no-color", "-e", pattern]
            if self.mode == "commit":
                args.append(self.head)
            args += ["--", *pathspecs]
            completed = self._git(args, ok_codes=(0, 1))
            prefix = f"{self.head}:" if self.mode == "commit" else ""
            allowed: list[str] = []
            for raw_line in completed.stdout.decode(
                "utf-8", errors="replace"
            ).splitlines():
                line = raw_line
                if prefix:
                    if not line.startswith(prefix):
                        continue
                    line = line[len(prefix) :]
                match = _GREP_LINE.match(line)
                if not match:
                    continue
                try:
                    normalized = self._normalize_path(match.group(1))
                except SnapshotAccessError:
                    continue
                allowed.append(f"{normalized}:{match.group(2)}:{match.group(3)}")
            if not allowed:
                return "no matches"
            shown = allowed[:max_matches]
            if len(allowed) > len(shown):
                shown.append(
                    f"... {len(allowed) - len(shown)} more matches; "
                    "narrow the pattern or path."
                )
            return "\n".join(shown)

        return self._guarded(operation)

    def git_diff(self, path: str, *, context: int = 6) -> str:
        """Return ``base`` to bound-head diff for one allowed tracked path."""

        def operation() -> str:
            normalized = self._normalize_path(path)
            if isinstance(context, bool) or not isinstance(context, int) or context < 0:
                raise SnapshotAccessError("diff context must be a non-negative integer")
            if not self._known_in_either_tree(normalized):
                raise SnapshotAccessError(
                    f"path is not tracked by this review: {normalized}"
                )
            args = [
                "diff",
                "--no-ext-diff",
                "--no-textconv",
                "--no-renames",
                "--no-color",
                f"--unified={context}",
                self.base,
            ]
            if self.mode == "commit":
                args.append(self.head)
            args += ["--", normalized]
            out = self._git(args).stdout.decode("utf-8", errors="replace").strip()
            target = self.head[:12] if self.mode == "commit" else "the bound worktree"
            return (
                out
                or f"{normalized} is unchanged between {self.base[:12]} and {target}."
            )

        return self._guarded(operation)

    def citation_line_exists(self, file: str, line: int) -> bool:
        """Whether an allowed citation resolves against the exact bound head."""

        if isinstance(line, bool) or not isinstance(line, int) or line < 1:
            return False

        def operation() -> bool:
            try:
                normalized = self._normalize_path(file)
                content = self._read_source_bytes(normalized)
            except SnapshotAccessError:
                return False
            return line <= len(content.decode("utf-8", errors="replace").splitlines())

        return self._guarded(operation)

    def citation_exists(self, citation: Mapping[str, Any]) -> bool:
        """Convenience adapter for the judge's ``{file, line, what}`` shape."""

        if not isinstance(citation, Mapping):
            return False
        file = citation.get("file")
        if not isinstance(file, str):
            return False
        return self.citation_line_exists(file, citation.get("line"))

    def _guarded(self, operation: Callable[[], _T]) -> _T:
        self.validate_pre()
        try:
            result = operation()
        except Exception:
            # If source drift caused an otherwise ordinary tool error, surface
            # the binding failure instead of letting the run continue unsafely.
            self.validate_post()
            raise
        self.validate_post()
        return result

    def _validate_binding(self, when: str) -> None:
        if self.mode == "commit":
            self._ensure_object(self.base)
            self._ensure_object(self.head)
            return
        current = self._current_binding()
        if current != self._binding:
            raise SnapshotDriftError(
                f"worktree changed {when}: {self._binding_mismatch(self._binding, current)}"
            )

    def _current_binding(self) -> SourceBinding:
        checkout_head = (
            self._resolve_commit("HEAD", "checkout HEAD")
            if self.mode == HEAD_WORKTREE
            else None
        )
        diff = self._tracked_diff_bytes()
        diff_hash = hashlib.sha256(diff).hexdigest()
        body = {
            "version": BINDING_VERSION,
            "mode": self.mode,
            "base": self.base,
            "head": self.head,
            "checkout_head": checkout_head,
            "tracked_diff_sha256": diff_hash,
        }
        fingerprint = hashlib.sha256(
            json.dumps(
                body, sort_keys=True, separators=(",", ":"), ensure_ascii=True
            ).encode("utf-8")
        ).hexdigest()
        binding = SourceBinding(**body, fingerprint=fingerprint)
        binding._validate_shape()
        return binding

    def _tracked_diff_bytes(self) -> bytes:
        args = [
            "diff",
            "--binary",
            "--full-index",
            "--no-ext-diff",
            "--no-textconv",
            "--no-renames",
            "--no-color",
            "--src-prefix=a/",
            "--dst-prefix=b/",
            self.base,
        ]
        if self.mode == "commit":
            args.append(self.head)
        args.append("--")
        return self._git(args).stdout

    @staticmethod
    def _binding_mismatch(expected: SourceBinding, actual: SourceBinding) -> str:
        changed = [
            name
            for name in expected.to_dict()
            if expected.to_dict()[name] != actual.to_dict()[name]
        ]
        return (
            f"source binding mismatch in {', '.join(changed)}; "
            f"expected {expected.fingerprint[:12]}, got {actual.fingerprint[:12]}"
        )

    def _require_repo_root(self) -> None:
        completed = self._git(["rev-parse", "--show-toplevel"])
        top = Path(completed.stdout.decode("utf-8", errors="replace").strip()).resolve()
        if top != self.repo_root:
            raise SnapshotBindingError(
                f"repo_root must be the Git worktree root: {self.repo_root} != {top}"
            )

    def _resolve_commit(self, value: str, label: str) -> str:
        if not isinstance(value, str) or not value or value == HEAD_WORKTREE:
            raise SnapshotBindingError(f"{label} must name a commit")
        completed = self._git(["rev-parse", "--verify", f"{value}^{{commit}}"])
        resolved = completed.stdout.decode("ascii", errors="strict").strip()
        if not re.fullmatch(r"[0-9a-f]{40}", resolved):
            raise SnapshotBindingError(f"{label} did not resolve to a full commit")
        return resolved

    def _ensure_object(self, revision: str) -> None:
        self._git(["cat-file", "-e", f"{revision}^{{commit}}"])

    def _normalize_path(self, raw: str, *, allow_glob: bool = False) -> str:
        if (
            not isinstance(raw, str)
            or not raw
            or any(ord(character) < 32 for character in raw)
            or "\\" in raw
        ):
            raise SnapshotAccessError("path must be non-empty repo-relative POSIX text")
        if ":" in raw:
            raise SnapshotAccessError("colon is not allowed in review paths")
        path = PurePosixPath(raw)
        if path.is_absolute() or any(part in {"", ".", ".."} for part in path.parts):
            raise SnapshotAccessError(f"path escapes the reviewable source tree: {raw}")
        if not path.parts or path.parts[0].lower() not in _REVIEWABLE_ROOTS:
            raise SnapshotAccessError(
                f"path is outside source/test/include roots: {raw}"
            )
        lowered = [part.lower() for part in path.parts]
        if any(part in _DENIED_COMPONENTS for part in lowered):
            raise SnapshotAccessError(f"path enters a denied directory: {raw}")
        name = lowered[-1]
        suffix = PurePosixPath(name).suffix
        if name in _DENIED_EXACT_NAMES or suffix in _DENIED_SECRET_SUFFIXES:
            raise SnapshotAccessError(
                f"path names a denied artifact or credential: {raw}"
            )
        if suffix in _ARTIFACT_SUFFIXES and (
            "oracle" in name or "evaluation" in name or name.startswith("eval")
        ):
            raise SnapshotAccessError(
                f"path names an oracle/evaluation artifact: {raw}"
            )
        if not allow_glob and any(character in raw for character in _GLOB_CHARS):
            raise SnapshotAccessError(f"glob is not allowed for this operation: {raw}")
        return path.as_posix()

    def _read_source_bytes(self, path: str) -> bytes:
        if self.mode == "commit":
            return self._blob_at(self.head, path)
        return self._worktree_file(path)

    def _blob_at(self, revision: str, path: str) -> bytes:
        completed = self._git(["ls-tree", "-z", revision, "--", path])
        records = [record for record in completed.stdout.split(b"\0") if record]
        if len(records) != 1 or b"\t" not in records[0]:
            raise SnapshotAccessError(f"path is not tracked at {revision[:12]}: {path}")
        metadata, raw_name = records[0].split(b"\t", 1)
        fields = metadata.split()
        name = raw_name.decode("utf-8", errors="surrogateescape")
        if name != path or len(fields) != 3 or fields[1] != b"blob":
            raise SnapshotAccessError(f"path is not a regular tracked blob: {path}")
        mode, _, object_id = fields
        if mode == b"120000":
            raise SnapshotAccessError(f"tracked symlinks are not reviewable: {path}")
        return self._git(["cat-file", "blob", object_id.decode("ascii")]).stdout

    def _worktree_file(self, path: str) -> bytes:
        index = self._git(
            ["ls-files", "-s", "--error-unmatch", "--", path], ok_codes=(0, 1)
        )
        if index.returncode != 0:
            raise SnapshotAccessError(f"path is not tracked by this review: {path}")
        first = index.stdout.splitlines()[0] if index.stdout.splitlines() else b""
        mode = first.split(maxsplit=1)[0] if first else b""
        if mode not in {b"100644", b"100755"}:
            raise SnapshotAccessError(f"path is not a regular tracked file: {path}")
        candidate = self.repo_root / path
        try:
            resolved = candidate.resolve(strict=True)
        except OSError as error:
            raise SnapshotAccessError(
                f"tracked path is not readable: {path}"
            ) from error
        if not resolved.is_relative_to(self.repo_root) or candidate.is_symlink():
            raise SnapshotAccessError(
                f"path resolves outside the safe source tree: {path}"
            )
        if not resolved.is_file():
            raise SnapshotAccessError(f"tracked path is not a file: {path}")
        try:
            return resolved.read_bytes()
        except OSError as error:
            raise SnapshotAccessError(
                f"tracked path is not readable: {path}"
            ) from error

    def _known_in_either_tree(self, path: str) -> bool:
        if self._tree_has_path(self.base, path):
            return True
        if self.mode == "commit":
            return self._tree_has_path(self.head, path)
        completed = self._git(
            ["ls-files", "--error-unmatch", "--", path], ok_codes=(0, 1)
        )
        return completed.returncode == 0

    def _tree_has_path(self, revision: str, path: str) -> bool:
        completed = self._git(["ls-tree", "-z", revision, "--", path])
        return bool(completed.stdout)

    def _git(
        self, args: list[str], *, ok_codes: tuple[int, ...] = (0,)
    ) -> subprocess.CompletedProcess[bytes]:
        try:
            completed = subprocess.run(
                ["git", "-C", str(self.repo_root), *_GIT_CONFIG, *args],
                capture_output=True,
                check=False,
                timeout=GIT_TIMEOUT_SECONDS,
            )
        except subprocess.TimeoutExpired as error:
            raise SnapshotError(
                f"git {args[0] if args else ''} exceeded {GIT_TIMEOUT_SECONDS}s"
            ) from error
        if completed.returncode not in ok_codes:
            stderr = completed.stderr.decode("utf-8", errors="replace").strip()
            raise SnapshotError(
                f"git {args[0] if args else ''} failed ({completed.returncode}): {stderr}"
            )
        return completed


__all__ = [
    "BINDING_VERSION",
    "HEAD_WORKTREE",
    "SourceBinding",
    "SourceSnapshot",
    "SnapshotAccessError",
    "SnapshotBindingError",
    "SnapshotDriftError",
    "SnapshotError",
]
