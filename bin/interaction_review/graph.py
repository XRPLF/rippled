#!/usr/bin/env python3
"""Graph data model and builder for the feature-interaction graph.

Nodes are features (transactors, amendments) and resources (base-pipeline forks,
invariants, shared per-tx SFields). Edges connect a feature to a resource it
consumes or mediates. Every node carries the source locations it was derived
from, so a PR diff can be mapped back onto the graph. See DESIGN.md.
"""

from __future__ import annotations

import json
from collections.abc import Iterable
from dataclasses import asdict, dataclass, field
from pathlib import Path

# Node-kind constants.
FEATURE_TRANSACTOR = "transactor"
FEATURE_AMENDMENT = "amendment"
RESOURCE_FORK = "fork"
RESOURCE_INVARIANT = "invariant"
RESOURCE_SFIELD = "shared_sfield"

# Edge-kind constants.
EDGE_CONSUMER = "consumer"
EDGE_MEDIATOR = "mediator"
EDGE_WRAPPER = "wrapper"

# Signal levels attached to resources for downstream filtering.
SIGNAL_HIGH = "high"
SIGNAL_MEDIUM = "medium"
SIGNAL_LOW = "low"

# Sentinel governing-feature value for cross-cutting fields that are core
# protocol behavior or pre-amendment / retired, i.e. have no active amendment.
CORE = "core"

# Bumped when the artifact's shape changes incompatibly, so a consumer reading a
# graph built by an older tool fails loudly instead of misinterpreting it.
SCHEMA_VERSION = 1

# Location roles. A node may carry several locations of different roles: a
# transactor is declared by a macro row and implemented in one or more files,
# and a fork that merges overloads has one definition span per overload.
LOC_MACRO = "macro"  # the declaring row in a .macro file or an enum header
LOC_DEFINITION = "definition"  # a C++ definition span (function, overload)
LOC_IMPL = "impl"  # a whole implementation file attributed to a feature
LOC_STATE_ENUM = "state_enum"  # the enum declaring a fork's boundary states
LOC_REFERENCE = "reference"  # a precise use site that enforces a resource

LOC_ROLES = (LOC_MACRO, LOC_DEFINITION, LOC_IMPL, LOC_STATE_ENUM, LOC_REFERENCE)


@dataclass(frozen=True, order=True)
class Location:
    """A repo-relative, 1-based inclusive line span a node was derived from.

    Frozen and ordered so locations deduplicate through a set and serialize in a
    stable order — the graph must be byte-identical across runs for the
    head-vs-base comparison to be meaningful.
    """

    file: str
    start_line: int
    end_line: int
    role: str

    def __post_init__(self) -> None:
        if self.role not in LOC_ROLES:
            raise ValueError(f"unknown location role {self.role!r}")
        if self.start_line < 1:
            raise ValueError(
                f"{self.file}: start_line must be 1-based, got {self.start_line}"
            )
        if self.end_line < self.start_line:
            raise ValueError(
                f"{self.file}: end_line {self.end_line} precedes "
                f"start_line {self.start_line}"
            )


def feature_id(kind: str, name: str) -> str:
    return f"feature:{kind}:{name}"


def resource_id(kind: str, name: str) -> str:
    return f"resource:{kind}:{name}"


@dataclass
class FeatureNode:
    id: str
    kind: str  # FEATURE_TRANSACTOR | FEATURE_AMENDMENT
    name: str
    # Transactor-only attributes (None for amendments).
    delegable: bool | None = None
    amendment: str | None = None  # gating amendment node name, if any
    privileges: list[str] = field(default_factory=list)
    fields: list[str] = field(default_factory=list)
    wrapper: bool = False
    locations: list[Location] = field(default_factory=list)


@dataclass
class ResourceNode:
    id: str
    kind: str  # RESOURCE_FORK | RESOURCE_INVARIANT | RESOURCE_SFIELD
    name: str
    signal: str
    lever_fields: list[str] = field(default_factory=list)
    lever_flags: list[str] = field(default_factory=list)
    amendment_gates: list[str] = field(default_factory=list)
    state_space: list[str] = field(default_factory=list)
    locations: list[Location] = field(default_factory=list)


def add_locations(node: FeatureNode | ResourceNode, locs: Iterable[Location]) -> None:
    """Merge locations into a node, deduplicated and in stable sorted order.

    Nodes are built incrementally (a fork accumulates one span per overload, a
    transactor gains its impl files after its macro row), so merging rather than
    assigning is the only safe way to add them.
    """
    node.locations = sorted(set(node.locations) | set(locs))


@dataclass
class Edge:
    kind: str  # EDGE_CONSUMER | EDGE_MEDIATOR | EDGE_WRAPPER
    src: str  # feature node id
    dst: str  # resource node id
    via: str  # what produced the edge (field / flag / gate / "wrapper" / "base")


class GraphBuilder:
    """Accumulates nodes and edges, deduplicating by id, and serializes."""

    def __init__(self) -> None:
        self.features: dict[str, FeatureNode] = {}
        self.resources: dict[str, ResourceNode] = {}
        self._edges: dict[tuple, Edge] = {}
        # The lever names the graph was derived from, carried in the artifact so
        # consumers can tell a real lever from an identifier that merely looks
        # like one, without re-parsing the macro files.
        self.vocabulary: dict[str, list[str]] = {
            "common_fields": [],
            "sfields": [],
            "flags": [],
            "amendment_globals": [],
        }

    def add_feature(self, node: FeatureNode) -> FeatureNode:
        return self._add(self.features, node)

    def add_resource(self, node: ResourceNode) -> ResourceNode:
        return self._add(self.resources, node)

    @staticmethod
    def _add(store: dict, node):
        """Insert, or return the identical existing node.

        Re-adding the same node is normal (extractors run in passes). Re-adding a
        *different* node under the same id means two sources disagree about what
        that node is, and silently keeping the first would bake in whichever
        extractor happened to run earlier.
        """
        existing = store.get(node.id)
        if existing is None:
            store[node.id] = node
            return node
        if existing != node:
            raise ValueError(
                f"conflicting definitions for node {node.id!r}: "
                f"{existing} vs {node}"
            )
        return existing

    def add_edge(self, edge: Edge) -> None:
        # Dedup on the full tuple so the same feature can reach a resource via
        # distinct causes (e.g. two lever fields) without collapsing them.
        self._edges[(edge.kind, edge.src, edge.dst, edge.via)] = edge

    @property
    def edges(self) -> list[Edge]:
        return list(self._edges.values())

    def feature_by_name(self, kind: str, name: str) -> FeatureNode | None:
        return self.features.get(feature_id(kind, name))

    def all_nodes(self) -> list[FeatureNode | ResourceNode]:
        return [*self.features.values(), *self.resources.values()]

    def neighbors(self, resource: ResourceNode) -> list[Edge]:
        return [e for e in self.edges if e.dst == resource.id]

    def to_dict(self) -> dict:
        return {
            "schema": SCHEMA_VERSION,
            "vocabulary": {k: sorted(v) for k, v in sorted(self.vocabulary.items())},
            "features": [asdict(n) for n in self.features.values()],
            "resources": [asdict(n) for n in self.resources.values()],
            "edges": [asdict(e) for e in self.edges],
        }

    def write(self, path: Path) -> None:
        path.write_text(json.dumps(self.to_dict(), indent=2, sort_keys=False))
