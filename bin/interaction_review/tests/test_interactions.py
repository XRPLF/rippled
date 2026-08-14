"""Interaction enumeration and role classification on a hand-built graph."""

from graph import (
    EDGE_CONSUMER,
    EDGE_MEDIATOR,
    EDGE_WRAPPER,
    FEATURE_AMENDMENT,
    FEATURE_TRANSACTOR,
    RESOURCE_FORK,
    RESOURCE_INVARIANT,
    SIGNAL_HIGH,
    SIGNAL_MEDIUM,
    Edge,
    FeatureNode,
    GraphBuilder,
    ResourceNode,
    feature_id,
    resource_id,
)
from interactions import KIND_CC, KIND_MC, KIND_MM, enumerate_interactions


def _toy() -> GraphBuilder:
    b = GraphBuilder()
    for name in ("Am1", "Am2"):
        b.add_feature(
            FeatureNode(feature_id(FEATURE_AMENDMENT, name), FEATURE_AMENDMENT, name)
        )
    for name in ("T1", "T2", "Wrap"):
        b.add_feature(
            FeatureNode(
                feature_id(FEATURE_TRANSACTOR, name),
                FEATURE_TRANSACTOR,
                name,
                wrapper=(name == "Wrap"),
            )
        )
    fork = b.add_resource(
        ResourceNode(
            resource_id(RESOURCE_FORK, "F"),
            RESOURCE_FORK,
            "F",
            SIGNAL_HIGH,
            state_space=["S0", "S1"],
        )
    )
    inv = b.add_resource(
        ResourceNode(
            resource_id(RESOURCE_INVARIANT, "I"), RESOURCE_INVARIANT, "I", SIGNAL_MEDIUM
        )
    )

    def fid(n):
        return feature_id(
            FEATURE_AMENDMENT if n.startswith("Am") else FEATURE_TRANSACTOR, n
        )

    b.add_edge(Edge(EDGE_MEDIATOR, fid("Am1"), fork.id, "sfX"))
    b.add_edge(Edge(EDGE_MEDIATOR, fid("Am2"), fork.id, "sfY"))
    for t in ("T1", "T2", "Wrap"):
        b.add_edge(Edge(EDGE_CONSUMER, fid(t), fork.id, "base"))
    b.add_edge(Edge(EDGE_WRAPPER, fid("Wrap"), fork.id, "wrapper"))
    b.add_edge(Edge(EDGE_CONSUMER, fid("T1"), inv.id, "Priv"))
    b.add_edge(Edge(EDGE_CONSUMER, fid("T2"), inv.id, "Priv"))
    return b


def _find(interactions, resource, features):
    fs = set(features)
    return [
        i
        for i in interactions
        if i["resource"] == resource and set(i["features"]) == fs
    ]


def test_fork_pairs_and_cc_exclusion():
    inter = enumerate_interactions(_toy())
    # Am1 x Am2 is mediator x mediator on the fork.
    mm = _find(inter, "F", {"Am1", "Am2"})
    assert len(mm) == 1 and mm[0]["kind"] == KIND_MM
    assert mm[0]["feature_ids"] == [
        feature_id(FEATURE_AMENDMENT, "Am1"),
        feature_id(FEATURE_AMENDMENT, "Am2"),
    ]
    assert mm[0]["boundary_states"] == ["S0", "S1"]
    # A mediator x consumer pair exists.
    assert _find(inter, "F", {"Am1", "T1"})[0]["kind"] == KIND_MC
    # consumer x consumer is excluded on fork resources.
    assert _find(inter, "F", {"T1", "T2"}) == []


def test_wrapper_counts_as_mediator():
    inter = enumerate_interactions(_toy())
    # Wrap has both a consumer and a wrapper edge; the mediator role dominates,
    # so Wrap x Am1 is mediator x mediator.
    pair = _find(inter, "F", {"Wrap", "Am1"})
    assert len(pair) == 1 and pair[0]["kind"] == KIND_MM


def test_cc_kept_on_invariant():
    inter = enumerate_interactions(_toy())
    cc = _find(inter, "I", {"T1", "T2"})
    assert len(cc) == 1 and cc[0]["kind"] == KIND_CC
