"""Contract tests for cross-resource AI investigation clusters."""

from __future__ import annotations

from copy import deepcopy

import pytest

from review_clusters import ClusterError, build_review_clusters


def interaction(
    left: str,
    right: str,
    *,
    feature_ids: tuple[str, str] | None = None,
    tier: str = "review",
    score: int = 70,
    line: int = 10,
) -> dict:
    item = {
        "features": [left, right],
        "roles": ["mediator", "mediator"],
        "kind": "mediator×mediator",
        "vias": [[f"via-{left}"], [f"via-{right}"]],
        "tier": tier,
        "score": score,
        "why": [f"{left} and {right} meet here"],
        "evidence": [{"file": "src/example.cpp", "lines": [[line, line + 1]]}],
    }
    if feature_ids is not None:
        item["feature_ids"] = list(feature_ids)
    return item


def group(resource: str, *items: dict, kind: str = "fork") -> dict:
    return {
        "resource": resource,
        "resource_kind": kind,
        "signal": "high",
        "resource_match": "span",
        "new_levers": ["sfDelegate"],
        "boundary_states": ["Account", "Delegate"],
        "score": max((item["score"] for item in items), default=0),
        "omitted": 0,
        "consumer_cohort": None,
        "authorized_features": [],
        "interactions": list(items),
    }


def report(*groups: dict) -> dict:
    return {"groups": list(groups)}


def investigation_candidate(resource: str, item: dict, kind: str = "fork") -> dict:
    candidate = {
        "resource": resource,
        "resource_kind": kind,
        "signal": "high",
        "resource_match": "span",
        "new_levers": ["sfDelegate"],
        "boundary_states": ["Account", "Delegate"],
        "features": deepcopy(item["features"]),
        "roles": deepcopy(item["roles"]),
        "kind": item["kind"],
        "vias": deepcopy(item["vias"]),
        "score": item["score"],
        "tier": item["tier"],
        "why": deepcopy(item["why"]),
        "evidence": deepcopy(item["evidence"]),
        "authorized_features": [],
    }
    if "feature_ids" in item:
        candidate["feature_ids"] = deepcopy(item["feature_ids"])
    return candidate


BATCH = "feature:transactor:Batch"
DELEGATE = "feature:amendment:PermissionDelegationV1_1"


def test_a_recurring_pair_is_one_cluster_with_every_qualified_location():
    selected = report(
        group(
            "getFeePayer",
            interaction(
                "PermissionDelegationV1_1",
                "Batch",
                feature_ids=(DELEGATE, BATCH),
                score=108,
                line=20,
            ),
        ),
        group(
            "checkSign",
            interaction(
                "Batch",
                "PermissionDelegationV1_1",
                feature_ids=(BATCH, DELEGATE),
                score=103,
                line=40,
            ),
        ),
        group(
            "checkPermission",
            interaction(
                "PermissionDelegationV1_1",
                "Batch",
                feature_ids=(DELEGATE, BATCH),
                score=105,
                line=60,
            ),
        ),
    )

    clusters = build_review_clusters(selected, max_items=1)

    assert len(clusters) == 1
    cluster = clusters[0]
    assert cluster["feature_identities"] == sorted([BATCH, DELEGATE])
    assert cluster["features"] == ["PermissionDelegationV1_1", "Batch"]
    assert cluster["score"] == 108
    assert [location["resource"] for location in cluster["locations"]] == [
        "getFeePayer",
        "checkPermission",
        "checkSign",
    ]
    assert [location["budget_rank"] for location in cluster["locations"]] == [
        1,
        2,
        3,
    ]
    assert cluster["locations"][2]["features"] == [
        "Batch",
        "PermissionDelegationV1_1",
    ]
    assert cluster["locations"][2]["feature_identities"] == [BATCH, DELEGATE]
    assert cluster["locations"][2]["evidence"] == [
        {"file": "src/example.cpp", "lines": [[40, 41]]}
    ]


def test_uncapped_candidate_view_wins_over_the_display_projection():
    first = interaction(
        "PermissionDelegationV1_1",
        "Batch",
        feature_ids=(DELEGATE, BATCH),
        score=108,
        line=20,
    )
    hidden = interaction(
        "Batch",
        "PermissionDelegationV1_1",
        feature_ids=(BATCH, DELEGATE),
        score=68,
        line=70,
    )
    selected = {
        "groups": [group("getFeePayer", first)],
        "investigation_candidates": [
            investigation_candidate("getFeePayer", first),
            investigation_candidate("preflight1", hidden),
        ],
    }

    clusters = build_review_clusters(selected, max_items=1)

    assert len(clusters) == 1
    assert [location["resource"] for location in clusters[0]["locations"]] == [
        "getFeePayer",
        "preflight1",
    ]
    assert clusters[0]["locations"][1]["evidence"] == [
        {"file": "src/example.cpp", "lines": [[70, 71]]}
    ]


def test_identical_display_names_do_not_merge_distinct_graph_feature_ids():
    amendment = "feature:amendment:Shared"
    transactor = "feature:transactor:Shared"
    other = "feature:transactor:Other"
    selected = report(
        group(
            "checkSign",
            interaction("Shared", "Other", feature_ids=(amendment, other), score=90),
        ),
        group(
            "checkPermission",
            interaction("Shared", "Other", feature_ids=(transactor, other), score=89),
        ),
    )

    clusters = build_review_clusters(selected, max_items=8)

    assert len(clusters) == 2
    assert {tuple(cluster["feature_identities"]) for cluster in clusters} == {
        tuple(sorted((amendment, other))),
        tuple(sorted((transactor, other))),
    }
    assert [sorted(cluster["features"]) for cluster in clusters] == [
        ["Other", "Shared"],
        ["Other", "Shared"],
    ]
    for cluster in clusters:
        names_by_id = dict(
            zip(cluster["feature_identities"], cluster["features"], strict=True)
        )
        assert names_by_id[other] == "Other"
        assert (
            names_by_id[amendment if amendment in names_by_id else transactor]
            == "Shared"
        )


def test_max_items_caps_clusters_not_their_locations():
    selected = report(
        group(
            "a1",
            interaction("A", "B", score=100),
            interaction("C", "D", score=90),
        ),
        group("a2", interaction("B", "A", score=99)),
        group("a3", interaction("A", "B", score=98)),
        group("e1", interaction("E", "F", score=80)),
    )

    clusters = build_review_clusters(selected, max_items=2)

    assert len(clusters) == 2
    assert clusters[0]["features"] == ["A", "B"]
    assert len(clusters[0]["locations"]) == 3
    assert clusters[1]["features"] == ["C", "D"]
    assert build_review_clusters(selected, max_items=0) == []


def test_cluster_order_uses_best_eligible_tier_then_exact_score():
    selected = report(
        group(
            "alpha-context",
            interaction("Alpha", "Pair", tier="context", score=999),
        ),
        group(
            "alpha-review",
            interaction("Alpha", "Pair", tier="review", score=61),
        ),
        group(
            "beta-review",
            interaction("Beta", "Pair", tier="review", score=97),
        ),
        group(
            "gamma-consider",
            interaction("Gamma", "Pair", tier="consider", score=500),
        ),
    )

    clusters = build_review_clusters(selected, max_items=8)

    assert [cluster["features"] for cluster in clusters] == [
        ["Beta", "Pair"],
        ["Alpha", "Pair"],
        ["Gamma", "Pair"],
    ]
    alpha = clusters[1]
    assert (alpha["tier"], alpha["score"]) == ("review", 61)
    # Context cannot win the budget slot, but remains evidence in the packet.
    assert {location["resource"] for location in alpha["locations"]} == {
        "alpha-context",
        "alpha-review",
    }
    context = next(
        location for location in alpha["locations"] if location["tier"] == "context"
    )
    assert context["budget_rank"] is None
    assert isinstance(context["selection_rank"], int)


def test_exact_ties_have_an_input_order_independent_canonical_tiebreak():
    first = group("z-resource", interaction("Zed", "Pair", score=80))
    second = group("a-resource", interaction("Alpha", "Pair", score=80))

    forward = build_review_clusters(report(first, second), max_items=8)
    reverse = build_review_clusters(report(second, first), max_items=8)

    assert [cluster["key"] for cluster in forward] == [
        cluster["key"] for cluster in reverse
    ]
    assert [cluster["rank"] for cluster in forward] == [1, 2]


def test_output_is_independent_and_duplicate_locations_fail_loudly():
    selected = report(group("checkSign", interaction("A", "B", score=80)))
    before = deepcopy(selected)

    clusters = build_review_clusters(selected, max_items=8)
    clusters[0]["locations"][0]["evidence"][0]["file"] = "changed.cpp"
    clusters[0]["locations"][0]["group"]["resource"] = "changed"

    assert selected == before

    duplicate = report(
        group(
            "checkSign",
            interaction("A", "B", score=80),
            interaction("B", "A", score=70),
        )
    )
    with pytest.raises(ClusterError, match="duplicate resource-qualified"):
        build_review_clusters(duplicate)


def test_one_graph_identity_cannot_acquire_two_display_names():
    selected = report(
        group(
            "checkSign",
            interaction(
                "Batch", "Other", feature_ids=(BATCH, "feature:transactor:Other")
            ),
        ),
        group(
            "checkPermission",
            interaction(
                "NotBatch",
                "Other",
                feature_ids=(BATCH, "feature:transactor:Other"),
            ),
        ),
    )

    with pytest.raises(ClusterError, match="inconsistent display names"):
        build_review_clusters(selected)


def test_ambiguous_same_name_endpoints_require_canonical_ids():
    ambiguous = report(group("checkSign", interaction("Batch", "Batch")))
    with pytest.raises(ClusterError, match="supply graph feature IDs"):
        build_review_clusters(ambiguous)

    disambiguated = report(
        group(
            "checkSign",
            interaction(
                "Batch",
                "Batch",
                feature_ids=(
                    "feature:amendment:Batch",
                    "feature:transactor:Batch",
                ),
            ),
        )
    )
    assert len(build_review_clusters(disambiguated)) == 1


def test_an_invariant_group_remains_one_budget_unit_with_all_of_its_evidence():
    invariant = group(
        "AccountRootsNotDeleted",
        interaction("EscrowCreate", "Payment", score=88, line=30),
        interaction("Payment", "OfferCreate", score=80, line=50),
        kind="invariant",
    )
    invariant["authorized_features"] = ["EscrowCreate"]

    clusters = build_review_clusters(report(invariant), max_items=1)

    assert len(clusters) == 1
    assert clusters[0]["kind"] == "invariant"
    assert clusters[0]["features"] == ["EscrowCreate"]
    assert len(clusters[0]["locations"]) == 1
    assert clusters[0]["locations"][0]["evidence"] == [
        {"file": "src/example.cpp", "lines": [[30, 31]]},
        {"file": "src/example.cpp", "lines": [[50, 51]]},
    ]


@pytest.mark.parametrize("max_items", [-1, True, 1.5])
def test_invalid_cluster_caps_are_rejected(max_items):
    with pytest.raises(ClusterError, match="max_items"):
        build_review_clusters(report(), max_items=max_items)
