#!/usr/bin/env python3
"""Extract feature nodes and macro-derived edges from the .macro files.

Produces transactor and amendment feature nodes, shared per-tx SField resource
nodes (non-common fields on >=2 transactors), and the consumer edges from
transactors to those shared fields. Reuses the codegen TRANSACTION parser.

Every node produced here carries the macro row it was declared by, as a line
span, so a diff touching that row maps back to the node.
"""

from __future__ import annotations

import importlib.util
import re
import sys
from pathlib import Path

from graph import (
    EDGE_CONSUMER,
    FEATURE_AMENDMENT,
    FEATURE_TRANSACTOR,
    LOC_MACRO,
    RESOURCE_SFIELD,
    SIGNAL_LOW,
    Edge,
    FeatureNode,
    GraphBuilder,
    Location,
    ResourceNode,
    add_locations,
    feature_id,
    resource_id,
)
from impl_locator import locate_transactor_impls, unused_overrides
from sourceutil import repo_relative, strip_comments

# Transactor delegability column values.
_DELEGABLE = "Delegation::Delegable"
_NOT_DELEGABLE = "Delegation::NotDelegable"

# The gating-amendment sentinel used for transactions enabled from genesis.
_NO_AMENDMENT = "uint256{}"

_RAW_TX_FIELD = "sfRawTransactions"  # wrapper (Batch) marker

# TRANSACTION rows span several lines and one wraps its name onto a second line
# (ttXCHAIN_ADD_ACCOUNT_CREATE_ATTESTATION), so the header is matched against the
# joined span rather than the opening line.
_TRANSACTION_HEADER_RE = re.compile(
    r"TRANSACTION\s*\(\s*(\w+)\s*,\s*\d+\s*,\s*(\w+)", re.DOTALL
)
# The SField declaration macros in sfields.macro, all of which take the field
# name as their first argument. The leading indent class is `[ \t]*`, never
# `\s*`: `\s` matches newlines, so a `\s*` prefix would start the match on an
# earlier blank line and report a line number short by however many blank lines
# precede the declaration.
_SFIELD_DECL_RE = re.compile(
    r"^[ \t]*(?:CONSTRUCT_)?(?:TYPED|UNTYPED)_SFIELD\s*\(\s*(\w+)\s*,", re.MULTILINE
)


def macro_invocation_spans(text: str, macro_name: str) -> list[tuple[int, int]]:
    """1-based inclusive (start, end) line spans of each `macro_name(...)` call.

    Comments are stripped first (so a commented-out row is not a declaration and
    a stray paren in prose cannot unbalance the scan) in a way that preserves
    line numbers. The span ends where the invocation's parentheses balance.
    """
    lines = strip_comments(text).splitlines()
    opener = re.compile(rf"^\s*{re.escape(macro_name)}\s*\(")
    spans: list[tuple[int, int]] = []
    i = 0
    while i < len(lines):
        if not opener.match(lines[i]):
            i += 1
            continue
        depth = 0
        j = i
        while j < len(lines):
            depth += lines[j].count("(") - lines[j].count(")")
            if depth <= 0:
                break
            j += 1
        if depth > 0:
            raise ValueError(
                f"unterminated {macro_name}( invocation starting at line {i + 1}"
            )
        spans.append((i + 1, j + 1))
        i = j + 1
    return spans


def _transaction_rows(text: str) -> dict[str, tuple[int, int]]:
    """Transactor name -> its TRANSACTION row span."""
    lines = text.splitlines()
    rows: dict[str, tuple[int, int]] = {}
    for start, end in macro_invocation_spans(text, "TRANSACTION"):
        header = _TRANSACTION_HEADER_RE.match(
            strip_comments("\n".join(lines[start - 1 : end]))
        )
        if header is None:
            raise ValueError(f"unparseable TRANSACTION header at line {start}")
        rows[header.group(2)] = (start, end)
    return rows


def _amendment_rows(text: str) -> list[tuple[str, str, int, int]]:
    """(macro kind, amendment name, start line, end line) for each active row."""
    lines = text.splitlines()
    rows: list[tuple[str, str, int, int]] = []
    for kind in ("FEATURE", "FIX"):
        for start, end in macro_invocation_spans(text, f"XRPL_{kind}"):
            joined = strip_comments("\n".join(lines[start - 1 : end]))
            name = re.search(r"\(\s*(\w+)", joined)
            if name is None:
                raise ValueError(f"unparseable XRPL_{kind} row at line {start}")
            rows.append((kind, name.group(1), start, end))
    return rows


def parse_sfield_rows(sfields_macro: Path) -> dict[str, tuple[int, int]]:
    """SField name -> its declaring row span in sfields.macro."""
    text = strip_comments(Path(sfields_macro).read_text())
    rows: dict[str, tuple[int, int]] = {}
    for match in _SFIELD_DECL_RE.finditer(text):
        line = text.count("\n", 0, match.start()) + 1
        rows.setdefault(match.group(1), (line, line))
    if not rows:
        raise ValueError(f"No SField declarations parsed from {sfields_macro}")
    return rows


def _load_tx_parser():
    """Import parse_macro_file from cmake/scripts/codegen.

    generate_tx_classes.py does `import macro_parser_common`, so its directory
    must be importable; append (not prepend) to avoid shadowing, and cache the
    module in sys.modules so repeated calls don't re-exec it.
    """
    cached = sys.modules.get("_codegen_tx")
    if cached is not None:
        return cached.parse_macro_file
    repo_root = Path(__file__).resolve().parents[2]
    codegen = repo_root / "cmake" / "scripts" / "codegen"
    if str(codegen) not in sys.path:
        sys.path.append(str(codegen))
    spec = importlib.util.spec_from_file_location(
        "_codegen_tx", codegen / "generate_tx_classes.py"
    )
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    sys.modules["_codegen_tx"] = module
    return module.parse_macro_file


def _amendment_global_to_name(token: str) -> str | None:
    """featureBatchV1_1 -> BatchV1_1, fixCleanup3_3_0 -> Cleanup3_3_0.

    Returns None for the no-amendment sentinel.
    """
    if token == _NO_AMENDMENT:
        return None
    if token.startswith("feature"):
        return token[len("feature") :]
    if token.startswith("fix"):
        return token[len("fix") :]
    return None


def parse_amendments(features_macro: Path) -> tuple[set[str], dict[str, str]]:
    """Return (active amendment names, global-token -> amendment-name map)."""
    names, globals_map, _ = _parse_amendments_located(Path(features_macro))
    return names, globals_map


def _parse_amendments_located(
    features_macro: Path,
) -> tuple[set[str], dict[str, str], dict[str, tuple[int, int]]]:
    """As `parse_amendments`, plus each amendment's declaring row span."""
    rows = _amendment_rows(Path(features_macro).read_text())
    names: set[str] = set()
    globals_map: dict[str, str] = {}
    spans: dict[str, tuple[int, int]] = {}
    for kind, name, start, end in rows:
        names.add(name)
        prefix = "feature" if kind == "FEATURE" else "fix"
        globals_map[f"{prefix}{name}"] = name
        spans[name] = (start, end)
    if not names:
        raise ValueError(f"No active amendments parsed from {features_macro}")
    return names, globals_map, spans


def _privileges(privileges_col: str) -> list[str]:
    col = privileges_col.strip()
    if col == "NoPriv":
        return []
    return [p.strip() for p in col.split("|") if p.strip()]


def extract_macros(
    builder: GraphBuilder,
    repo_root: Path,
    transactions_macro: Path,
    features_macro: Path,
    sfields_macro: Path,
    common_fields: set[str],
) -> dict[str, str]:
    """Populate feature nodes, shared-SField resources, and consumer edges.

    `repo_root` anchors the repo-relative paths recorded in node locations; the
    three macro paths must live under it.

    Returns the amendment global-token -> name map for the fork extractor's
    gate resolution.
    """
    features_rel = repo_relative(features_macro, repo_root)
    transactions_rel = repo_relative(transactions_macro, repo_root)
    sfields_rel = repo_relative(sfields_macro, repo_root)

    amendment_names, globals_map, amendment_spans = _parse_amendments_located(
        Path(features_macro)
    )
    for name in sorted(amendment_names):
        start, end = amendment_spans[name]
        builder.add_feature(
            FeatureNode(
                id=feature_id(FEATURE_AMENDMENT, name),
                kind=FEATURE_AMENDMENT,
                name=name,
                locations=[Location(features_rel, start, end, LOC_MACRO)],
            )
        )

    parse_macro_file = _load_tx_parser()
    transactions = parse_macro_file(str(transactions_macro))
    tx_rows = _transaction_rows(Path(transactions_macro).read_text())

    # The codegen parser is the source of truth for row contents and the span
    # scanner for row positions; if they disagree, one of them is wrong and a
    # transactor would silently lose its location.
    parsed_names = {tx["name"] for tx in transactions}
    if parsed_names != set(tx_rows):
        raise ValueError(
            "TRANSACTION row scan disagrees with the codegen parser: "
            f"only in scan {sorted(set(tx_rows) - parsed_names)}, "
            f"only in parser {sorted(parsed_names - set(tx_rows))}"
        )

    # field name -> transactors carrying it (for shared-SField resources).
    field_carriers: dict[str, list[str]] = {}

    for tx in transactions:
        name = tx["name"]
        field_names = [f["name"] for f in tx["fields"]]
        delegable_col = tx["delegable"].strip()
        if delegable_col not in (_DELEGABLE, _NOT_DELEGABLE):
            raise ValueError(
                f"Transactor {name} has unrecognized delegability {delegable_col!r}"
            )
        start, end = tx_rows[name]
        builder.add_feature(
            FeatureNode(
                id=feature_id(FEATURE_TRANSACTOR, name),
                kind=FEATURE_TRANSACTOR,
                name=name,
                delegable=(delegable_col == _DELEGABLE),
                amendment=_amendment_global_to_name(tx["amendments"].strip()),
                privileges=_privileges(tx["privileges"]),
                fields=field_names,
                wrapper=(_RAW_TX_FIELD in field_names),
                locations=[Location(transactions_rel, start, end, LOC_MACRO)],
            )
        )
        for fname in field_names:
            field_carriers.setdefault(fname, []).append(name)

    sfield_rows = parse_sfield_rows(Path(sfields_macro))

    # Shared per-tx SFields: non-common fields on >=2 transactors (low signal).
    for fname, carriers in sorted(field_carriers.items()):
        if fname in common_fields or len(carriers) < 2:
            continue
        if fname not in sfield_rows:
            raise ValueError(
                f"shared SField {fname!r} has no declaration in {sfields_rel} "
                f"(renamed, or declared by a macro this scan does not know)"
            )
        start, end = sfield_rows[fname]
        rid = resource_id(RESOURCE_SFIELD, fname)
        builder.add_resource(
            ResourceNode(
                id=rid,
                kind=RESOURCE_SFIELD,
                name=fname,
                signal=SIGNAL_LOW,
                lever_fields=[fname],
                locations=[Location(sfields_rel, start, end, LOC_MACRO)],
            )
        )
        for carrier in carriers:
            builder.add_edge(
                Edge(
                    kind=EDGE_CONSUMER,
                    src=feature_id(FEATURE_TRANSACTOR, carrier),
                    dst=rid,
                    via=fname,
                )
            )

    return globals_map


def attach_impl_locations(
    builder: GraphBuilder,
    repo_root: Path,
    overrides: dict[str, list[str]] | None = None,
) -> None:
    """Attach each transactor's implementation files to its node.

    Separate from `extract_macros` because it reads the source tree rather than
    the macro files, and because it is what makes an edit to a transactor's
    `doApply` — the common case in a PR — resolve to that transactor.
    """
    transactors = [n for n in builder.features.values() if n.kind == FEATURE_TRANSACTOR]
    names = [n.name for n in transactors]
    stale = unused_overrides(overrides or {}, names)
    if stale:
        raise ValueError(
            f"transactor_impl_overrides names non-existent transactor(s): {stale}"
        )
    located = locate_transactor_impls(Path(repo_root), names, overrides)
    for node in transactors:
        add_locations(node, located[node.name])
