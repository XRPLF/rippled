#!/usr/bin/env python3
"""Extract feature nodes and macro-derived edges from the .macro files.

Produces transactor and amendment feature nodes, shared per-tx SField resource
nodes (non-common fields on >=2 transactors), and the consumer edges from
transactors to those shared fields. Reuses the codegen TRANSACTION parser.
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
    RESOURCE_SFIELD,
    SIGNAL_LOW,
    Edge,
    FeatureNode,
    GraphBuilder,
    ResourceNode,
    feature_id,
    resource_id,
)

# Transactor delegability column values.
_DELEGABLE = "Delegation::Delegable"
_NOT_DELEGABLE = "Delegation::NotDelegable"

# The gating-amendment sentinel used for transactions enabled from genesis.
_NO_AMENDMENT = "uint256{}"

# Active amendment declarations; anchoring (FEATURE|FIX) right after XRPL_
# excludes XRPL_RETIRE_* and, because it is not line-anchored past leading
# whitespace, still rejects // commented-out examples.
_AMENDMENT_RE = re.compile(r"^\s*XRPL_(FEATURE|FIX)\s*\(\s*(\w+)", re.MULTILINE)

_RAW_TX_FIELD = "sfRawTransactions"  # wrapper (Batch) marker


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
    text = Path(features_macro).read_text()
    names: set[str] = set()
    globals_map: dict[str, str] = {}
    for kind, name in _AMENDMENT_RE.findall(text):
        names.add(name)
        prefix = "feature" if kind == "FEATURE" else "fix"
        globals_map[f"{prefix}{name}"] = name
    if not names:
        raise ValueError(f"No active amendments parsed from {features_macro}")
    return names, globals_map


def _privileges(privileges_col: str) -> list[str]:
    col = privileges_col.strip()
    if col == "NoPriv":
        return []
    return [p.strip() for p in col.split("|") if p.strip()]


def extract_macros(
    builder: GraphBuilder,
    transactions_macro: Path,
    features_macro: Path,
    common_fields: set[str],
) -> dict[str, str]:
    """Populate feature nodes, shared-SField resources, and consumer edges.

    Returns the amendment global-token -> name map for the fork extractor's
    gate resolution.
    """
    amendment_names, globals_map = parse_amendments(features_macro)
    for name in sorted(amendment_names):
        builder.add_feature(
            FeatureNode(
                id=feature_id(FEATURE_AMENDMENT, name),
                kind=FEATURE_AMENDMENT,
                name=name,
            )
        )

    parse_macro_file = _load_tx_parser()
    transactions = parse_macro_file(str(transactions_macro))

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
            )
        )
        for fname in field_names:
            field_carriers.setdefault(fname, []).append(name)

    # Shared per-tx SFields: non-common fields on >=2 transactors (low signal).
    for fname, carriers in sorted(field_carriers.items()):
        if fname in common_fields or len(carriers) < 2:
            continue
        rid = resource_id(RESOURCE_SFIELD, fname)
        builder.add_resource(
            ResourceNode(
                id=rid,
                kind=RESOURCE_SFIELD,
                name=fname,
                signal=SIGNAL_LOW,
                lever_fields=[fname],
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
