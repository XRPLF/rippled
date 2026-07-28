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
from graph import LOC_DEFINITION, LOC_STATE_ENUM, Location
from sourceutil import repo_relative

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
    # One definition span per overload merged into this fork, so a diff to
    # either `checkSign` maps to the single checkSign resource.
    spans: set[Location] = field(default_factory=set)


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


def _is_local(node: ci.Cursor) -> bool:
    """True if the reference resolves to a function-scope declaration.

    rippled routinely names locals after the amendment they cache, e.g.
    `bool const fixEnabled = view.rules().enabled(fixCleanup3_2_0);` and
    `featureSAVEnabled`. Treating those as amendment gates would invent edges,
    and — because an unrecognized gate is a hard error — could fail the build
    outright the first time such a local appears in scan scope.
    """
    referenced = node.referenced
    if referenced is None:
        return False
    parent = referenced.semantic_parent
    return parent is not None and parent.kind in (
        ci.CursorKind.FUNCTION_DECL,
        ci.CursorKind.CXX_METHOD,
        ci.CursorKind.FUNCTION_TEMPLATE,
        ci.CursorKind.CONSTRUCTOR,
        ci.CursorKind.DESTRUCTOR,
    )


def _collect_refs(fn: ci.Cursor) -> tuple[set[str], set[str], set[str]]:
    sfields: set[str] = set()
    flags: set[str] = set()
    gates: set[str] = set()
    for node in fn.walk_preorder():
        if node.kind != ci.CursorKind.DECL_REF_EXPR:
            continue
        name = node.spelling
        if _is_local(node):
            continue
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


def _owner_name(fn: ci.Cursor, file_name: str) -> str:
    """An identity for the thing a definition belongs to, for collision checks.

    A class parent is identity enough — overloads and out-of-line definitions of
    one class legitimately merge, wherever they live. A namespace parent is not:
    two unrelated free functions of the same name in `namespace xrpl` are the
    likeliest collision in this scan scope, which spans Transactor.cpp plus ~19
    helper headers, so the file is folded in for those.
    """
    parent = fn.semantic_parent
    if parent is not None and parent.kind in (
        ci.CursorKind.CLASS_DECL,
        ci.CursorKind.STRUCT_DECL,
        ci.CursorKind.CLASS_TEMPLATE,
    ):
        return f"class {parent.spelling}"
    scope = parent.spelling if parent is not None and parent.spelling else "<file>"
    return f"namespace {scope} in {file_name}"


def _qualified_enum_name(enum: ci.Cursor) -> str:
    parent = enum.semantic_parent
    if parent is not None and parent.kind in (
        ci.CursorKind.CLASS_DECL,
        ci.CursorKind.STRUCT_DECL,
    ):
        return f"{parent.spelling}::{enum.spelling}"
    return enum.spelling


def _definition_span(
    fn: ci.Cursor, file_name: str, repo_root: str, role: str = LOC_DEFINITION
) -> Location:
    """The definition's line span, signature through closing brace.

    The extent's end is trusted only when it lands in the same file as the
    start; a definition assembled across files by macro expansion would
    otherwise yield a span whose end line indexes the wrong file.
    """
    extent = fn.extent
    start_line = extent.start.line
    end_file = extent.end.file
    end_line = (
        extent.end.line
        if end_file is not None and end_file.name == file_name
        else start_line
    )
    return Location(
        file=repo_relative(file_name, repo_root),
        start_line=start_line,
        end_line=max(start_line, end_line),
        role=role,
    )


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
    enum_spans: dict[str, Location] = {}
    # fork name -> the semantic parent its definitions belong to, so two
    # unrelated same-named functions cannot silently merge into one resource.
    owners: dict[str, str] = {}

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
                loc = node.location.file
                # Bounded by repo_root, NOT by _in_scope. A fork's state enum is
                # declared in a header (`FeePayerType` in include/xrpl/tx/
                # Transactor.h, `SeqProxy::Type` in SeqProxy.h) that the scan
                # scope deliberately excludes, so requiring _in_scope here meant
                # no enum span was ever recorded and adding a boundary state
                # mapped to nothing.
                if loc is not None and loc.name.startswith(repo_root):
                    enum_spans.setdefault(
                        qname,
                        _definition_span(
                            node, loc.name, repo_root, role=LOC_STATE_ENUM
                        ),
                    )
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
            owner = _owner_name(node, loc.name)
            previous_owner = owners.setdefault(node.spelling, owner)
            if previous_owner != owner:
                raise ValueError(
                    f"two unrelated definitions named {node.spelling!r} are in "
                    f"scan scope ({previous_owner} and {owner}); merging them "
                    f"would fabricate levers and edges between unrelated code. "
                    f"Key forks by qualified name, or narrow the scan scope."
                )
            res = results.setdefault(node.spelling, ForkResult(node.spelling))
            res.lever_fields |= levers
            res.lever_flags |= flags
            res.gate_globals |= gates
            res.spans.add(_definition_span(node, loc.name, repo_root))

    for name, res in results.items():
        enum_name = FORK_STATE_ENUM.get(name)
        if enum_name and enum_name in enums:
            res.state_space = enums[enum_name]
            # The enum declaring a fork's boundary states is part of that fork:
            # adding a FeePayerType value changes getFeePayer's state space, and
            # that edit must map to the fork even though it lives in a header.
            if enum_name in enum_spans:
                res.spans.add(enum_spans[enum_name])

    return sorted(results.values(), key=lambda r: r.name)


def extract_forks(
    build_dir: Path,
    common_fields: set[str],
    libclang_path: str | None = None,
    repo_root: Path | None = None,
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
    tu_root = path[: -len(_TU_SUFFIX)]

    # The compile database points at whichever checkout was configured. If the
    # caller is building the graph for a different tree — exactly what the
    # base-vs-head workflow does — fork spans would come from one checkout and
    # macro/impl spans from another, and nothing downstream could tell.
    if repo_root is not None:
        expected = Path(repo_root).resolve()
        if Path(tu_root).resolve() != expected:
            raise ValueError(
                f"compile database describes {tu_root!r} but --repo-root is "
                f"{expected}{__import__('os').sep}. Fork spans and macro spans "
                f"would come from different checkouts. Rebuild the compile "
                f"database for this tree, or point --repo-root at it."
            )

    forks = scan_translation_unit(tu, common_fields, path, tu_root)

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
