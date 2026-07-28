#!/usr/bin/env python3
"""Extract invariant resource nodes from the Privilege enum, and connect each
transactor to the privilege bits it declares.

Each privilege bit is a shared invariant boundary: two transactors that both
declare the same bit exercise the same invariant-check code path.
"""

from __future__ import annotations

import re
from pathlib import Path

from graph import (
    EDGE_CONSUMER,
    FEATURE_TRANSACTOR,
    LOC_IMPL,
    LOC_MACRO,
    RESOURCE_INVARIANT,
    SIGNAL_MEDIUM,
    Edge,
    GraphBuilder,
    Location,
    ResourceNode,
    resource_id,
)
from sourceutil import repo_relative, strip_comments, whole_file_location

# Trees holding the checks that enforce the privilege bits.
INVARIANT_TREES = (
    "src/libxrpl/tx/invariants",
    "include/xrpl/tx/invariants",
)
_INVARIANT_SUFFIXES = (".cpp", ".h")

# `hasPrivilege(<tx>, <Bit>)` — how a check consults a privilege bit. This is
# the link from a bit to the code enforcing it; without it a PR editing an
# invariant check touches no node at all. The second argument may be a mask of
# OR'd bits (`hasPrivilege(tx, CreateAcct | CreatePseudoAcct)`), so every bit in
# it is captured; requiring a single name missed that form entirely.
_HAS_PRIVILEGE_RE = re.compile(r"\bhasPrivilege\s*\([^,()]+,\s*([\w\s|]+?)\s*\)")
# A mask is bare names joined by `|`. Requiring this shape rejects the function
# *declaration* `hasPrivilege(STTx const&, Privilege priv)`, whose second
# argument is a type and a parameter name rather than a bit.
_BIT_MASK_RE = re.compile(r"^\w+(?:\s*\|\s*\w+)*$")
_BIT_SPLIT_RE = re.compile(r"\s*\|\s*")

_ENUM_RE = re.compile(r"enum\s+Privilege\s*\{(.*?)\};", re.DOTALL)
# Enumerator with an explicit hex value; NoPriv (0x0000) is the empty sentinel.
_BIT_RE = re.compile(r"(\w+)\s*=\s*0x0*([0-9A-Fa-f]+)")
_NO_PRIV = "NoPriv"


def parse_privileges(header_path: Path) -> list[str]:
    return list(parse_privilege_rows(header_path))


def parse_privilege_rows(header_path: Path) -> dict[str, int]:
    """Privilege bit name -> the 1-based line declaring it.

    Comments are stripped (preserving line numbers) so a bit named only in a
    comment cannot be mistaken for a declaration.
    """
    text = strip_comments(Path(header_path).read_text())
    match = _ENUM_RE.search(text)
    if not match:
        raise ValueError(f"enum Privilege not found in {header_path}")
    body_offset = match.start(1)
    rows: dict[str, int] = {}
    for bit in _BIT_RE.finditer(match.group(1)):
        if bit.group(1) == _NO_PRIV:
            continue
        line = text.count("\n", 0, body_offset + bit.start()) + 1
        rows.setdefault(bit.group(1), line)
    if not rows:
        raise ValueError(f"No privilege bits parsed from {header_path}")
    return rows


def locate_privilege_checks(repo_root: Path) -> dict[str, list[Location]]:
    """Privilege bit -> the files whose checks consult it.

    The enum row says a bit exists; these files are where it is enforced, and
    they are what a PR actually edits. Without them an amendment x invariant
    change — the tool's own target class of bug — maps to nothing.
    """
    found: dict[str, set[Location]] = {}
    for tree in INVARIANT_TREES:
        root = Path(repo_root) / tree
        if not root.is_dir():
            raise FileNotFoundError(
                f"invariant tree {tree!r} not found under {repo_root} "
                f"(relocated? update INVARIANT_TREES)"
            )
        for path in sorted(root.rglob("*")):
            if path.suffix not in _INVARIANT_SUFFIXES or not path.is_file():
                continue
            text = strip_comments(path.read_text())
            for mask in set(_HAS_PRIVILEGE_RE.findall(text)):
                mask = mask.strip()
                if not _BIT_MASK_RE.match(mask):
                    continue
                for bit in _BIT_SPLIT_RE.split(mask):
                    found.setdefault(bit, set()).add(
                        whole_file_location(path, repo_root, LOC_IMPL)
                    )
    return {bit: sorted(locs) for bit, locs in found.items()}


def extract_privileges(
    builder: GraphBuilder, repo_root: Path, privilege_header: Path
) -> None:
    rows = parse_privilege_rows(privilege_header)
    bits = set(rows)
    header_rel = repo_relative(privilege_header, repo_root)
    checks = locate_privilege_checks(repo_root)

    unknown = sorted(set(checks) - bits)
    if unknown:
        raise ValueError(
            f"hasPrivilege() consults privilege bit(s) absent from "
            f"{header_rel}: {unknown} (renamed, or the enum scan is wrong)"
        )

    for bit in sorted(bits):
        builder.add_resource(
            ResourceNode(
                id=resource_id(RESOURCE_INVARIANT, bit),
                kind=RESOURCE_INVARIANT,
                name=bit,
                signal=SIGNAL_MEDIUM,
                locations=[
                    Location(header_rel, rows[bit], rows[bit], LOC_MACRO),
                    *checks.get(bit, ()),
                ],
            )
        )

    for node in builder.features.values():
        if node.kind != FEATURE_TRANSACTOR:
            continue
        for bit in node.privileges:
            if bit not in bits:
                raise ValueError(
                    f"Transactor {node.name} declares unknown privilege {bit!r}"
                )
            builder.add_edge(
                Edge(
                    kind=EDGE_CONSUMER,
                    src=node.id,
                    dst=resource_id(RESOURCE_INVARIANT, bit),
                    via=bit,
                )
            )
