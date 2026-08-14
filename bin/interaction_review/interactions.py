#!/usr/bin/env python3
"""Enumerate feature interactions from the graph.

An interaction is a pair of features that are common neighbors of a resource.
Roles: a feature reaching a resource via a mediator/wrapper edge is a mediator
of it; via a consumer edge, a consumer. consumer x consumer pairs on fork
resources are excluded by construction (every transactor consumes every fork,
so such pairs are vacuous); they are kept on invariant and shared-SField
resources, where sharing the node is itself the meaningful relation.
"""

from __future__ import annotations

import itertools
import json
from pathlib import Path

from graph import (
    EDGE_CONSUMER,
    RESOURCE_FORK,
    GraphBuilder,
    ResourceNode,
)

MEDIATOR = "mediator"
CONSUMER = "consumer"

KIND_MM = "mediator×mediator"
KIND_MC = "mediator×consumer"
KIND_CC = "consumer×consumer"

# Keyed by the alphabetically sorted role pair.
_PAIR_KIND = {
    (MEDIATOR, MEDIATOR): KIND_MM,
    (CONSUMER, MEDIATOR): KIND_MC,
    (CONSUMER, CONSUMER): KIND_CC,
}


def _roles_for(builder: GraphBuilder, resource: ResourceNode) -> dict[str, dict]:
    """feature id -> {role, vias}. Mediator role dominates when a feature has
    both a mediator/wrapper and a consumer edge to the resource (e.g. Batch)."""
    roles: dict[str, dict] = {}
    for edge in builder.neighbors(resource):
        role = CONSUMER if edge.kind == EDGE_CONSUMER else MEDIATOR
        info = roles.setdefault(edge.src, {"role": CONSUMER, "vias": set()})
        info["vias"].add(edge.via)
        if role == MEDIATOR:
            info["role"] = MEDIATOR
    return roles


def enumerate_interactions(builder: GraphBuilder) -> list[dict]:
    interactions: list[dict] = []
    for resource in builder.resources.values():
        roles = _roles_for(builder, resource)
        for a_id, b_id in itertools.combinations(sorted(roles), 2):
            role_a = roles[a_id]["role"]
            role_b = roles[b_id]["role"]
            kind = _PAIR_KIND[tuple(sorted((role_a, role_b)))]
            # Drop vacuous consumer x consumer pairs on fork resources only.
            if kind == KIND_CC and resource.kind == RESOURCE_FORK:
                continue
            fa, fb = builder.features[a_id], builder.features[b_id]
            interactions.append(
                {
                    "resource": resource.name,
                    "resource_kind": resource.kind,
                    "signal": resource.signal,
                    "kind": kind,
                    "features": [fa.name, fb.name],
                    "roles": [role_a, role_b],
                    "vias": [
                        sorted(roles[a_id]["vias"]),
                        sorted(roles[b_id]["vias"]),
                    ],
                    "boundary_states": resource.state_space,
                }
            )
    return interactions


def write_interactions(interactions: list[dict], path: Path) -> None:
    by_kind: dict[str, int] = {}
    by_signal: dict[str, int] = {}
    for it in interactions:
        by_kind[it["kind"]] = by_kind.get(it["kind"], 0) + 1
        by_signal[it["signal"]] = by_signal.get(it["signal"], 0) + 1
    payload = {
        "summary": {
            "total": len(interactions),
            "by_kind": by_kind,
            "by_signal": by_signal,
        },
        "interactions": interactions,
    }
    path.write_text(json.dumps(payload, indent=2))
