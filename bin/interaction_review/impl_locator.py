#!/usr/bin/env python3
"""Resolve each transactor to the source files that implement it.

A transactor's macro row says what it is; its implementation files say what it
does. A PR almost always edits the latter, so without this mapping a diff to
`AMMWithdraw.cpp` would touch no graph node at all.

The mapping is derived, not declared, in three rules, the third applying only if the first two find nothing:

1. **Stem** — a file named after the transactor (`Payment` -> `Payment.cpp`,
   `Payment.h`). Covers 71 of 82 transactors.
2. **Symbol** — a file defining `Name::method` at column 0, or declaring
   `class Name`. Covers the eight XChain transactors, which all live in
   `XChainBridge.cpp`. Rules 1 and 2 are unioned: a transactor can legitimately
   have both a file of its own and definitions elsewhere.
3. **Alias** — a `using Name = Other;` declaration, resolved by re-running rules
   1 and 2 against `Other`. Covers the two XChain transactors whose C++ class
   name differs from the macro name (`XChainModifyBridge` is `BridgeModify`) and
   the three pseudo-transactions, which alias to `Change`.

All 82 currently resolve with no hand-maintained table. A transactor that
resolves to nothing fails the build rather than silently losing its
implementation; `config/transactor_impl_overrides.yml` is the escape hatch for a
future layout these rules genuinely cannot express.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from pathlib import Path

from graph import LOC_IMPL, Location
from sourceutil import strip_comments, whole_file_location

# Trees holding transactor sources. Headers and sources live in separate trees
# (include/ and src/), and a diff can land in either.
TRANSACTOR_TREES = (
    "src/libxrpl/tx/transactors",
    "include/xrpl/tx/transactors",
)
_SOURCE_SUFFIXES = (".cpp", ".h")

# `Name::method(` at column 0 — a member *definition*, since clang-format puts
# the return type on its own line. Anchoring matters: an unanchored match also
# hits every qualified *call*, so `AMMWithdraw::withdraw(...)` invoked from
# AMMClawback.cpp would attribute that whole file to the AMMWithdraw transactor.
# A `^[ \t]*` variant is not enough either — it still matches an argument
# continuation line such as `        AMMWithdraw::equalWithdrawTokens(`.
_SCOPE_RE = re.compile(r"^(\w+)::\w+\s*\(", re.MULTILINE)
# A real definition, not `friend class X;` / `class X;` / an elaborated type
# specifier — same hazard the _SCOPE_RE comment describes, so anchor it the same
# way and require the opening brace or a base-class list.
_RECORD_RE = re.compile(
    r"^(?:class|struct)\s+(\w+)\s*(?:final\s*)?(?:[:{]|$)", re.MULTILINE
)
# `using Alias = Target;`, where Target may be namespace-qualified.
_ALIAS_RE = re.compile(r"\busing\s+(\w+)\s*=\s*([\w:]+)\s*;")

# Bound on alias chasing. One hop covers every current case; the bound only
# exists so a hand-written cycle (`using A = B; using B = A;`) cannot hang.
_MAX_ALIAS_DEPTH = 4


@dataclass
class SourceIndex:
    """Symbol tables over the transactor trees, built in a single pass.

    Indexing once and looking up 82 names beats re-scanning every file per name.
    """

    files: list[Path] = field(default_factory=list)
    stems: dict[str, list[Path]] = field(default_factory=dict)
    symbols: dict[str, list[Path]] = field(default_factory=dict)
    aliases: dict[str, str] = field(default_factory=dict)


def build_index(repo_root: Path) -> SourceIndex:
    index = SourceIndex()
    for tree in TRANSACTOR_TREES:
        root = Path(repo_root) / tree
        if not root.is_dir():
            raise FileNotFoundError(
                f"transactor tree {tree!r} not found under {repo_root} "
                f"(relocated? update TRANSACTOR_TREES)"
            )
        for path in sorted(root.rglob("*")):
            if path.suffix in _SOURCE_SUFFIXES and path.is_file():
                index.files.append(path)

    for path in index.files:
        index.stems.setdefault(path.stem, []).append(path)
        # Comments are stripped so a class named only in prose cannot create a
        # phantom mapping.
        text = strip_comments(path.read_text())
        for name in set(_SCOPE_RE.findall(text)) | set(_RECORD_RE.findall(text)):
            index.symbols.setdefault(name, []).append(path)
        for alias, target in _ALIAS_RE.findall(text):
            # Keep the first definition; a redeclaration is the same type.
            index.aliases.setdefault(alias, target.rsplit("::", 1)[-1])
    return index


def _direct_files(name: str, index: SourceIndex) -> list[Path]:
    """Rules 1 and 2 for a single name, stem matches first."""
    found = list(index.stems.get(name, ()))
    for path in index.symbols.get(name, ()):
        if path not in found:
            found.append(path)
    return found


def resolve_files(name: str, index: SourceIndex) -> list[Path]:
    """Files implementing `name`, following `using` aliases when needed."""
    seen_aliases = {name}
    current = name
    for _ in range(_MAX_ALIAS_DEPTH):
        found = _direct_files(current, index)
        if found:
            return found
        target = index.aliases.get(current)
        if target is None or target in seen_aliases:
            return []
        seen_aliases.add(target)
        current = target
    return []


def locate_transactor_impls(
    repo_root: Path,
    names: list[str],
    overrides: dict[str, list[str]] | None = None,
) -> dict[str, list[Location]]:
    """transactor name -> whole-file impl locations. Raises if any is unresolved."""
    overrides = overrides or {}
    index = build_index(Path(repo_root))

    located: dict[str, list[Location]] = {}
    unresolved: list[str] = []
    for name in names:
        if name in overrides:
            paths = []
            for rel in overrides[name]:
                path = Path(repo_root) / rel
                if not path.is_file():
                    raise ValueError(
                        f"transactor_impl_overrides[{name!r}] lists {rel!r}, "
                        f"which does not exist"
                    )
                paths.append(path)
        else:
            paths = resolve_files(name, index)
        if not paths:
            unresolved.append(name)
            continue
        located[name] = sorted(
            {whole_file_location(p, repo_root, LOC_IMPL) for p in paths}
        )

    if unresolved:
        raise ValueError(
            f"no implementation file found for transactor(s): {sorted(unresolved)}. "
            f"A diff to their logic would map to no graph node. Add them to "
            f"config/transactor_impl_overrides.yml if the layout genuinely "
            f"cannot be derived."
        )
    return located


def unused_overrides(overrides: dict[str, list[str]], names: list[str]) -> list[str]:
    """Override keys naming no current transactor — a stale entry to delete."""
    return sorted(set(overrides) - set(names))
