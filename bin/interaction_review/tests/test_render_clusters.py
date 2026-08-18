"""Cluster-aware comment rendering without changing legacy judged artifacts."""

from __future__ import annotations

import copy

import judge_interactions
import render_comment

FEATURES = ["Batch", "PermissionDelegationV1_1"]


def _item(*, tier: str, score: int, reverse_features: bool = False) -> dict:
    features = list(reversed(FEATURES)) if reverse_features else list(FEATURES)
    return {
        "features": features,
        "roles": ["mediator", "mediator"],
        "kind": "mediator×mediator",
        "vias": [["wrapper"], ["sfDelegate"]],
        "score": score,
        "tier": tier,
        "why": ["The diff changes one side of this combination"],
        "evidence": [
            {
                "file": "src/libxrpl/tx/transactors/system/Batch.cpp",
                "lines": [[421, 423]],
            }
        ],
    }


def _group(
    resource: str,
    *,
    tier: str,
    score: int,
    reverse_features: bool = False,
) -> dict:
    return {
        "resource": resource,
        "resource_kind": "fork",
        "signal": "high",
        "resource_match": None,
        "new_levers": [],
        "boundary_states": [],
        "score": score,
        "omitted": 0,
        "consumer_cohort": None,
        "authorized_features": [],
        "interactions": [
            _item(tier=tier, score=score, reverse_features=reverse_features)
        ],
    }


def _report(groups: list[dict]) -> dict:
    by_tier: dict[str, int] = {}
    for group in groups:
        tier = group["interactions"][0]["tier"]
        by_tier[tier] = by_tier.get(tier, 0) + 1
    return {
        "schema": 1,
        "base": "0" * 40,
        "head": "worktree",
        "summary": {
            "changed_files": 1,
            "touched_nodes": 1,
            "candidates": len(groups),
            "selected": len(groups),
            "cohort_pairs": 0,
            "dropped_low_signal": 0,
            "truncated": 0,
            "omitted_resources": 0,
            "by_tier": by_tier,
            "new_levers": [],
        },
        "groups": groups,
        "caveats": ["Static analysis cannot prove behavior."],
    }


def _cluster(locations: list[dict], **updates) -> dict:
    record = {
        "features": list(FEATURES),
        "locations": locations,
        "behavior": "broken",
        "coverage": "covered",
        "verdict": "gap",
        "confidence": "high",
        "summary": "CLUSTER SUMMARY: delegate consent can be bypassed.",
        "detail": "CLUSTER DETAIL: the inner transaction skips Bob's signature.",
        "citations": [
            {
                "file": "src/libxrpl/tx/transactors/system/Batch.cpp",
                "line": 421,
                "what": "required signer selection",
            }
        ],
        "states_reached": ["delegated inner"],
        "states_unreached": [],
        "error": None,
    }
    record.update(updates)
    return record


def _location(resource: str, *, tier: str, score: int) -> dict:
    return {
        "resource_kind": "fork",
        "resource": resource,
        "tier": tier,
        "score": score,
    }


def test_cluster_renders_one_finding_at_its_primary_location_only():
    groups = [
        _group("getFeePayer", tier="consider", score=88),
        _group("checkPermission", tier="review", score=70, reverse_features=True),
        _group("checkSign", tier="review", score=95),
    ]
    report = _report(groups)
    # Deliberately not rank-ordered: rendering owns the primary-location rule.
    report["judgements"] = [
        _cluster(
            [
                _location("getFeePayer", tier="consider", score=88),
                _location("checkPermission", tier="review", score=70),
                _location("checkSign", tier="review", score=95),
            ]
        )
    ]

    body = render_comment.render(report)

    assert body.count("CLUSTER SUMMARY") == 1
    assert body.count("CLUSTER DETAIL") == 1
    assert body.count("<summary>Full analysis</summary>") == 1
    assert body.index("Read:") < body.index("<summary>Full analysis</summary>")
    assert body.index("<summary>Full analysis</summary>") < body.index("CLUSTER DETAIL")
    assert body.count("Possible behavior gap") == 1
    assert "Primary location: `checkSign`." in body
    assert "Finding reported here" not in body
    assert "Checked; supporting" not in body
    assert "See the combined finding" not in body
    assert "Investigated together across" not in body
    assert "| features |" not in body
    assert "getFeePayer" not in body
    assert "checkPermission" not in body
    assert not any(
        marker in body for marker in ("🔴", "🟡", "⚪", "🟢", "🧪", "🔗", "⚠")
    )
    assert body.startswith(f"{render_comment.MARKER}\n## Model review")


def test_cluster_primary_prefers_review_tier_over_a_higher_consider_score():
    groups = [
        _group("checkPermission", tier="review", score=60),
        _group("getFeePayer", tier="consider", score=100),
    ]
    report = _report(groups)
    report["judgements"] = [
        _cluster(
            [
                _location("getFeePayer", tier="consider", score=100),
                _location("checkPermission", tier="review", score=60),
            ]
        )
    ]

    body = render_comment.render(report)

    assert "CLUSTER SUMMARY" in body
    assert "Primary location: `checkPermission`." in body
    assert "Primary location: `getFeePayer`." not in body


def test_model_primary_location_overrides_the_static_score_for_presentation():
    groups = [
        _group("checkPermission", tier="review", score=70),
        _group("checkSign", tier="review", score=95),
    ]
    report = _report(groups)
    report["judgements"] = [
        _cluster(
            [
                _location("checkPermission", tier="review", score=70),
                _location("checkSign", tier="review", score=95),
            ],
            primary_location={
                "resource_kind": "fork",
                "resource": "checkPermission",
            },
        )
    ]

    body = render_comment.render(report)

    assert "CLUSTER SUMMARY" in body
    assert "Primary location: `checkPermission`." in body
    assert "Primary location: `checkSign`." not in body


def test_primary_outside_the_static_selection_still_renders_once():
    report = _report([_group("getFeePayer", tier="review", score=95)])
    report["judgements"] = [
        _cluster(
            [
                _location("getFeePayer", tier="review", score=95),
                _location("checkSign", tier="review", score=90),
            ],
            primary_location={"resource_kind": "fork", "resource": "checkSign"},
            location_assessments=[
                {
                    "resource_kind": "fork",
                    "resource": "getFeePayer",
                    "role": "supporting",
                    "what": "Fee identity establishes the delegated path.",
                },
                {
                    "resource_kind": "fork",
                    "resource": "checkSign",
                    "role": "decisive",
                    "what": "Signature derivation establishes the bypass.",
                },
            ],
        )
    ]

    body = render_comment.render(report)

    assert body.count("CLUSTER SUMMARY") == 1
    assert "Primary location: `checkSign`." in body
    assert "additional reviewed location" not in body
    assert "shortened static table" not in body
    assert "See the combined finding" not in body


def test_location_role_annotations_are_not_rendered():
    groups = [
        _group("checkPermission", tier="review", score=95),
        _group("checkSign", tier="review", score=90),
        _group("getFeePayer", tier="review", score=85),
        _group("checkSponsor", tier="review", score=80),
    ]
    report = _report(groups)
    locations = [
        _location("checkPermission", tier="review", score=95),
        _location("checkSign", tier="review", score=90),
        _location("getFeePayer", tier="review", score=85),
        _location("checkSponsor", tier="review", score=80),
    ]
    report["judgements"] = [
        _cluster(
            locations,
            primary_location={"resource_kind": "fork", "resource": "checkSign"},
            location_assessments=[
                {
                    "resource_kind": "fork",
                    "resource": "checkPermission",
                    "role": "supporting",
                    "what": "Confirms delegated authority is consumed.",
                },
                {
                    "resource_kind": "fork",
                    "resource": "checkSign",
                    "role": "decisive",
                    "what": "Drops the required delegate signer.",
                },
                {
                    "resource_kind": "fork",
                    "resource": "getFeePayer",
                    "role": "not_relevant",
                    "what": "Fee routing does not authorize the transaction.",
                },
                {
                    "resource_kind": "fork",
                    "resource": "checkSponsor",
                    "role": "unresolved",
                    "what": "No sponsor path was established.",
                },
            ],
        )
    ]

    body = render_comment.render(report)

    assert body.count("CLUSTER SUMMARY") == 1
    assert "Primary location: `checkSign`." in body
    assert "Checked; supporting" not in body
    assert "Checked; not relevant" not in body
    assert "Checked; unresolved" not in body
    assert "Drops the required delegate signer" not in body
    assert "Fee routing does not authorize the transaction" not in body


def test_legacy_resource_qualified_record_renders_without_a_static_row():
    group = _group("checkSign", tier="review", score=95)
    report = _report([group])
    item = group["interactions"][0]
    cluster = _cluster([])
    legacy = {
        **cluster,
        "key": "checkSign|interaction|Batch:mediator,PermissionDelegationV1_1:mediator",
        "resource": "checkSign",
        "resource_kind": "fork",
        "kind": "interaction",
        "tier": "review",
        "score": 95,
        "iterations": 2,
        "dropped_citations": [],
    }
    legacy.pop("locations")
    report["judgements"] = [legacy]

    body = render_comment.render(report)

    assert body.count("CLUSTER SUMMARY") == 1
    assert "Possible behavior gap" in body
    assert "Primary location: `checkSign`." in body
    assert "Checked; supporting" not in body
    assert "investigation" not in body
    assert "highest-ranked" not in body


def test_cluster_infrastructure_error_is_not_shown_as_an_opinion():
    groups = [_group("checkSign", tier="review", score=95)]
    report = _report(groups)
    failed = _cluster(
        [_location("checkSign", tier="review", score=95)],
        summary="MODEL ERROR SUMMARY",
        detail="secret provider failure",
        behavior="unclear",
        coverage="unclear",
        verdict="unclear",
        error="provider failed",
    )
    report["judgements"] = [failed]

    body = render_comment.render(report)

    assert "MODEL ERROR SUMMARY" not in body
    assert "secret provider failure" not in body
    assert "Checked; supporting" not in body
    assert "attempt failed before producing a verdict" in body


def test_invariant_cluster_has_a_self_contained_authorization_rule_label():
    invariant_item = _item(tier="review", score=75)
    invariant_group = {
        **_group("ChangeNftCounts", tier="review", score=75),
        "resource_kind": "invariant",
        "authorized_features": ["NFTokenBurn", "NFTokenMint"],
        "interactions": [invariant_item],
    }
    report = _report([invariant_group])
    report["judgements"] = [
        _cluster(
            [
                {
                    **_location("ChangeNftCounts", tier="review", score=75),
                    "resource_kind": "invariant",
                }
            ],
            kind="invariant",
            features=["NFTokenBurn", "NFTokenMint"],
        )
    ]

    body = render_comment.render(report)

    assert body.count("CLUSTER SUMMARY") == 1
    assert "`ChangeNftCounts` authorization rule" in body
    assert "Primary location: `ChangeNftCounts`." in body
    assert "this permission" not in body
    assert "feature-pair investigation" not in body


def test_handled_cluster_uses_the_model_verdict_label():
    groups = [_group("checkSponsor", tier="review", score=95)]
    report = _report(groups)
    report["judgements"] = [
        _cluster(
            [_location("checkSponsor", tier="review", score=95)],
            behavior="correct",
            coverage="covered",
            verdict="handled",
        )
    ]

    body = render_comment.render(report)

    assert "**No issue found**" in body
    assert "Possible behavior gap" not in body


def test_static_same_name_rows_do_not_duplicate_the_model_comment():
    shared_amendment = "feature:amendment:Batch"
    shared_transactor = "feature:transactor:Batch"
    other = "feature:amendment:PermissionDelegationV1_1"
    one_group = _group("checkSign", tier="review", score=95)
    one_group["interactions"][0]["feature_ids"] = [shared_transactor, other]
    second = copy.deepcopy(one_group["interactions"][0])
    second["feature_ids"] = [shared_amendment, other]
    second["score"] = 90
    one_group["interactions"].append(second)
    report = _report([one_group])
    record = _cluster(
        [
            {
                **_location("checkSign", tier="review", score=95),
                "feature_identities": [shared_transactor, other],
            }
        ],
        feature_identities=sorted([shared_transactor, other]),
    )
    report["judgements"] = [record]

    body = render_comment.render(report)

    assert body.count("CLUSTER SUMMARY") == 1
    assert body.count("Possible behavior gap") == 1


def test_cluster_from_a_legacy_selected_plan_renders_once():
    report = _report(
        [
            _group("getFeePayer", tier="review", score=95),
            _group("checkSign", tier="review", score=90),
        ]
    )
    entry = judge_interactions.plan(report, ("review", "consider"), 1)[0]
    assessments = [
        {
            "resource_kind": location["resource_kind"],
            "resource": location["resource"],
            "role": "decisive" if location["resource"] == "checkSign" else "supporting",
            "what": "This legacy location was included in the combined trace.",
        }
        for location in entry["locations"]
    ]
    report["judgements"] = [
        _cluster(
            entry["locations"],
            key=entry["key"],
            features=entry["features"],
            feature_identities=entry["feature_identities"],
            primary_location={"resource_kind": "fork", "resource": "checkSign"},
            location_assessments=assessments,
        )
    ]

    body = render_comment.render(report)

    assert body.count("CLUSTER SUMMARY") == 1
    assert body.count("Possible behavior gap") == 1
    assert "Primary location: `checkSign`." in body
    assert "Checked; supporting" not in body


def test_static_only_report_renders_a_short_no_model_status():
    body = render_comment.render(
        _report([_group("checkSign", tier="review", score=95)])
    )

    assert "No model comments are available for this change." in body
    assert "checkSign" not in body
    assert "Batch" not in body
    assert "| features |" not in body
    assert "model can be wrong" not in body


def test_mixed_success_and_failure_keeps_the_finding_and_hides_the_error():
    report = _report([_group("checkSign", tier="review", score=95)])
    report["judgements"] = [
        _cluster(
            [_location("checkSign", tier="review", score=95)],
            rank=1,
        ),
        _cluster(
            [_location("checkSponsor", tier="review", score=80)],
            rank=2,
            features=["Batch", "Sponsor"],
            summary="PROVIDER ERROR SUMMARY",
            detail="secret provider failure",
            error="provider failed",
        ),
    ]

    body = render_comment.render(report)

    assert body.count("CLUSTER SUMMARY") == 1
    assert "1 automated-review attempt failed before producing a verdict" in body
    assert "PROVIDER ERROR SUMMARY" not in body
    assert "secret provider failure" not in body


def test_successful_clusters_render_in_judge_rank_order():
    report = _report([_group("checkSign", tier="review", score=95)])
    report["judgements"] = [
        _cluster(
            [_location("later", tier="review", score=90)],
            rank=2,
            features=["Batch", "Sponsor"],
            summary="SECOND FINDING",
            detail="SECOND FINDING",
        ),
        _cluster(
            [_location("earlier", tier="review", score=80)],
            rank=1,
            summary="FIRST FINDING",
            detail="FIRST FINDING",
        ),
    ]

    body = render_comment.render(report)

    assert body.index("FIRST FINDING") < body.index("SECOND FINDING")


def test_legacy_primary_location_falls_back_to_the_resource_in_its_key():
    report = _report([_group("checkSign", tier="review", score=95)])
    legacy = _cluster([], key="checkSign|interaction|legacy")
    legacy.pop("locations")
    report["judgements"] = [legacy]

    assert "Primary location: `checkSign`." in render_comment.render(report)
