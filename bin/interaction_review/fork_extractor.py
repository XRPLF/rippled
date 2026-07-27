#!/usr/bin/env python3
"""Extract base-pipeline fork resources from Transactor.cpp via libclang.

Forks are auto-discovered, not hand-listed: any function defined in
Transactor.cpp (or a repo `/helpers/` header it pulls in) that references a
common (cross-cutting) SField, a transaction flag, or an amendment gate is a
fork. From each we collect, from its body:
  - lever fields: references to common SFields;
  - lever flags: references to transaction flag (tf*) constants;
  - amendment gates: references to amendment (feature*/fix*) constants, i.e.
    `rules().enabled(featureX)`-style branches.

Any SField/flag/amendment access reduces to a DeclRefExpr on the constant, so a
single reference sweep captures every access idiom. State-space enums for the
fee-payer and sequencing forks are read from the same AST. See DESIGN.md.
"""

from __future__ import annotations

import json
import shlex
from dataclasses import dataclass, field
from pathlib import Path

import clang.cindex as ci

# Fork function -> qualified enum whose values are that fork's boundary states.
# Keys must be discovered forks and the enum must resolve, else extract_forks
# fails (a stale mapping must not silently drop the state space).
FORK_STATE_ENUM = {
    "getFeePayer": "FeePayerType",
    "checkSeqProxy": "SeqProxy::Type",
    "consumeSeqProxy": "SeqProxy::Type",
}

# Anchor forks the design depends on by identity. Auto-discovery finds forks
# without a list, but a high-value fork silently vanishing (rename/relocation)
# must still fail loudly — the count floor alone can't catch a single dropout or
# a same-count swap. This is a tripwire, not the discovery source, so it stays
# small; it names exactly the forks DESIGN.md's verification asserts by name.
_REQUIRED_FORKS = frozenset(
    {"getFeePayer", "checkSeqProxy", "consumeSeqProxy", "checkSign", "preflight2"}
)

# Sanity floor: a healthy parse of the base pipeline finds well over this many
# forks; a lower count means a broken parse or wrong scope, so fail loudly. This
# is defense-in-depth behind _REQUIRED_FORKS, not a substitute for it.
_MIN_FORKS = 15

# Marker substring for repo helper headers whose functions join the scan.
_HELPERS_MARKER = "/helpers/"

# Repo-relative path of the translation unit; stripping it from the absolute
# compile-DB path yields the repo root (used to bound the scan). Includes the
# src/ segment so the derived root covers include/ headers too.
_TU_SUFFIX = "src/libxrpl/tx/Transactor.cpp"
_DEFAULT_DYLIB = "/opt/homebrew/opt/llvm/lib/libclang.dylib"


@dataclass
class ForkResult:
    name: str
    lever_fields: set[str] = field(default_factory=set)
    lever_flags: set[str] = field(default_factory=set)
    gate_globals: set[str] = field(default_factory=set)
    state_space: list[str] = field(default_factory=list)


def _resource_dir(dylib: Path) -> str | None:
    """Derive the clang builtin-header resource dir from the dylib location.

    <prefix>/lib/libclang.dylib -> <prefix>/lib/clang/<major>. Supplying this
    lets the parse resolve builtin headers (stddef.h, etc.) so the AST is
    complete; without it, degraded types silently drop the gate references we
    need. Returns None if the layout is unrecognized.
    """
    clang_root = dylib.parent / "clang"
    if not clang_root.is_dir():
        return None
    versions = sorted(
        (d for d in clang_root.iterdir() if d.is_dir() and d.name[:1].isdigit()),
        key=lambda d: int(d.name.split(".")[0]),
    )
    return str(versions[-1]) if versions else None


def _find_tu_entry(build_dir: Path) -> dict:
    db_path = build_dir / "compile_commands.json"
    if not db_path.exists():
        raise FileNotFoundError(f"compile_commands.json not found in {build_dir}")
    db = json.loads(db_path.read_text())
    for entry in db:
        if entry["file"].endswith(_TU_SUFFIX):
            return entry
    raise ValueError(f"{_TU_SUFFIX} has no entry in {db_path}")


def _parse_args(entry: dict, resource_dir: str | None) -> list[str]:
    if "command" in entry:
        tokens = shlex.split(entry["command"])
    else:
        tokens = list(entry["arguments"])
    args: list[str] = []
    skip = False
    for tok in tokens[1:]:  # drop the compiler
        if skip:
            skip = False
            continue
        if tok == "-c":
            continue
        if tok == "-o":
            skip = True
            continue
        if tok.endswith("Transactor.cpp"):
            continue
        args.append(tok)
    args += ["-working-directory", entry["directory"]]
    if resource_dir:
        args += ["-resource-dir", resource_dir]
    return args


def _collect_refs(fn: ci.Cursor) -> tuple[set[str], set[str], set[str]]:
    sfields: set[str] = set()
    flags: set[str] = set()
    gates: set[str] = set()
    for node in fn.walk_preorder():
        if node.kind != ci.CursorKind.DECL_REF_EXPR:
            continue
        name = node.spelling
        if name.startswith("sf"):
            sfields.add(name)
        elif name.startswith("tf"):
            flags.add(name)
        elif name.startswith("feature") or name.startswith("fix"):
            gates.add(name)
    return sfields, flags, gates


def _validate_required(discovered: set[str]) -> None:
    missing = sorted(_REQUIRED_FORKS - discovered)
    if missing:
        raise RuntimeError(
            f"required anchor fork(s) not discovered (renamed, relocated, or the "
            f"parse is broken): {missing}"
        )


def _qualified_enum_name(enum: ci.Cursor) -> str:
    parent = enum.semantic_parent
    if parent is not None and parent.kind in (
        ci.CursorKind.CLASS_DECL,
        ci.CursorKind.STRUCT_DECL,
    ):
        return f"{parent.spelling}::{enum.spelling}"
    return enum.spelling


def _in_scope(loc_name: str, tu_file: str, repo_root: str) -> bool:
    """A definition is in the base-pipeline scan scope if it lives in the TU
    itself or in a repo `/helpers/` header (bounded to repo_root so a same-named
    directory in a third-party include can't leak in)."""
    if loc_name == tu_file:
        return True
    return loc_name.startswith(repo_root) and _HELPERS_MARKER in loc_name


def scan_translation_unit(
    tu: ci.TranslationUnit,
    common_fields: set[str],
    tu_file: str,
    repo_root: str,
) -> list[ForkResult]:
    """Auto-discover fork resources in a parsed TU.

    A function in scope (see `_in_scope`) is a fork if it references at least one
    common field, transaction flag, or amendment gate. Overloads sharing a name
    merge into one resource. State-space enums are attached where a mapping in
    FORK_STATE_ENUM resolves.
    """
    results: dict[str, ForkResult] = {}
    enums: dict[str, list[str]] = {}

    for node in tu.cursor.walk_preorder():
        if node.kind == ci.CursorKind.ENUM_DECL and node.spelling:
            qname = _qualified_enum_name(node)
            values = [
                c.spelling
                for c in node.get_children()
                if c.kind == ci.CursorKind.ENUM_CONSTANT_DECL
            ]
            if values:
                enums.setdefault(qname, values)
        elif node.kind in (ci.CursorKind.CXX_METHOD, ci.CursorKind.FUNCTION_DECL):
            loc = node.location.file
            if not node.is_definition() or loc is None:
                continue
            if not _in_scope(loc.name, tu_file, repo_root):
                continue
            sfields, flags, gates = _collect_refs(node)
            levers = sfields & common_fields
            if not (levers or flags or gates):
                continue
            res = results.setdefault(node.spelling, ForkResult(node.spelling))
            res.lever_fields |= levers
            res.lever_flags |= flags
            res.gate_globals |= gates

    for name, res in results.items():
        enum_name = FORK_STATE_ENUM.get(name)
        if enum_name and enum_name in enums:
            res.state_space = enums[enum_name]

    return sorted(results.values(), key=lambda r: r.name)


def extract_forks(
    build_dir: Path,
    common_fields: set[str],
    libclang_path: str | None = None,
) -> list[ForkResult]:
    dylib = Path(libclang_path or _DEFAULT_DYLIB)
    if libclang_path and not dylib.exists():
        raise FileNotFoundError(f"--libclang path does not exist: {libclang_path}")
    # set_library_file may only be called once per process, before any other
    # libclang use; ignore if a library is already loaded.
    if dylib.exists() and not ci.Config.loaded:
        ci.Config.set_library_file(str(dylib))
    resource_dir = _resource_dir(dylib) if dylib.exists() else None

    entry = _find_tu_entry(build_dir)
    args = _parse_args(entry, resource_dir)
    tu = ci.Index.create().parse(entry["file"], args=args)

    errors = [d for d in tu.diagnostics if d.severity >= ci.Diagnostic.Error]
    if errors:
        detail = "\n".join(f"  {d.spelling} @ {d.location}" for d in errors[:10])
        raise RuntimeError(
            "Transactor.cpp parsed with errors; the AST would be degraded and "
            "gate/lever references dropped. Pass --libclang for a matching "
            f"toolchain.\n{detail}"
        )

    # Strip the repo-relative TU path to get the repo root; the trailing
    # separator makes the scan filter match on a path boundary.
    path = entry["file"]
    if not path.endswith(_TU_SUFFIX):
        raise ValueError(f"unexpected TU path {path!r}, cannot derive repo root")
    repo_root = path[: -len(_TU_SUFFIX)]

    forks = scan_translation_unit(tu, common_fields, path, repo_root)

    # A broken parse or wrong scope silently yields almost nothing; fail loudly.
    if len(forks) < _MIN_FORKS:
        raise RuntimeError(
            f"only {len(forks)} fork(s) discovered (expected >= {_MIN_FORKS}); "
            f"the parse or scan scope is likely broken"
        )
    discovered = {f.name: f for f in forks}
    # Anchor forks must be present even if auto-discovery would otherwise skip
    # them (a high-value fork must not vanish silently).
    _validate_required(set(discovered.keys()))
    # A stale FORK_STATE_ENUM mapping must fail, not silently drop a state space.
    for fork_name, enum_name in FORK_STATE_ENUM.items():
        fork = discovered.get(fork_name)
        if fork is None:
            raise ValueError(
                f"FORK_STATE_ENUM maps {fork_name!r}, which is not a discovered "
                f"fork (renamed or removed?)"
            )
        if not fork.state_space:
            raise ValueError(
                f"state enum {enum_name!r} for fork {fork_name!r} was not found "
                f"in the AST (renamed?)"
            )
    return forks
