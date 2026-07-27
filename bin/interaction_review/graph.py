#!/usr/bin/env python3
"""Graph data model and builder for the feature-interaction graph.

Nodes are features (transactors, amendments) and resources (base-pipeline forks,
invariants, shared per-tx SFields). Edges connect a feature to a resource it
consumes or mediates. See DESIGN.md for the model.
"""

from __future__ import annotations

import json
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

    def add_feature(self, node: FeatureNode) -> FeatureNode:
        existing = self.features.get(node.id)
        if existing is None:
            self.features[node.id] = node
            return node
        return existing

    def add_resource(self, node: ResourceNode) -> ResourceNode:
        existing = self.resources.get(node.id)
        if existing is None:
            self.resources[node.id] = node
            return node
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

    def neighbors(self, resource: ResourceNode) -> list[Edge]:
        return [e for e in self.edges if e.dst == resource.id]

    def to_dict(self) -> dict:
        return {
            "features": [asdict(n) for n in self.features.values()],
            "resources": [asdict(n) for n in self.resources.values()],
            "edges": [asdict(e) for e in self.edges],
        }

    def write(self, path: Path) -> None:
        path.write_text(json.dumps(self.to_dict(), indent=2, sort_keys=False))
