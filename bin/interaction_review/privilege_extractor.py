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
    RESOURCE_INVARIANT,
    SIGNAL_MEDIUM,
    Edge,
    GraphBuilder,
    ResourceNode,
    resource_id,
)

_ENUM_RE = re.compile(r"enum\s+Privilege\s*\{(.*?)\};", re.DOTALL)
# Enumerator with an explicit hex value; NoPriv (0x0000) is the empty sentinel.
_BIT_RE = re.compile(r"(\w+)\s*=\s*0x0*([0-9A-Fa-f]+)")
_NO_PRIV = "NoPriv"


def parse_privileges(header_path: Path) -> list[str]:
    text = Path(header_path).read_text()
    match = _ENUM_RE.search(text)
    if not match:
        raise ValueError(f"enum Privilege not found in {header_path}")
    bits = [name for name, value in _BIT_RE.findall(match.group(1)) if name != _NO_PRIV]
    if not bits:
        raise ValueError(f"No privilege bits parsed from {header_path}")
    return bits


def extract_privileges(builder: GraphBuilder, privilege_header: Path) -> None:
    bits = set(parse_privileges(privilege_header))
    for bit in sorted(bits):
        builder.add_resource(
            ResourceNode(
                id=resource_id(RESOURCE_INVARIANT, bit),
                kind=RESOURCE_INVARIANT,
                name=bit,
                signal=SIGNAL_MEDIUM,
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
