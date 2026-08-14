"""Small, synthetic tests for the local oracle evaluator."""

from __future__ import annotations

import json
import hashlib

import pytest

import evaluate_run as evaluator


def interaction(features, *, score=70, tier="review", feature_ids=None):
    item = {
        "features": features,
        "roles": ["mediator", "mediator"],
        "kind": "mediator×mediator",
        "vias": [[], []],
        "score": score,
        "tier": tier,
        "why": ["synthetic"],
        "evidence": [],
    }
    if feature_ids is not None:
        item["feature_ids"] = feature_ids
    return item


def group(resource, *items, resource_kind="fork"):
    return {
        "resource": resource,
        "resource_kind": resource_kind,
        "interactions": list(items),
    }


def investigation_candidate(
    resource,
    features,
    *,
    score=70,
    tier="review",
    feature_ids=None,
    resource_kind="fork",
    kind="mediator×mediator",
):
    result = {
        "resource": resource,
        "resource_kind": resource_kind,
        "signal": "high",
        "resource_match": None,
        "new_levers": [],
        "boundary_states": [],
        "features": features,
        "roles": [] if kind == "invariant" else ["mediator", "mediator"],
        "kind": kind,
        "vias": [] if kind == "invariant" else [[], []],
        "score": score,
        "tier": tier,
        "why": ["synthetic"],
        "evidence": [],
        "authorized_features": features if kind == "invariant" else [],
    }
    if feature_ids is not None:
        result["feature_ids"] = feature_ids
    return result


def invariant_group(resource, *, score=70, tier="review"):
    return group(
        resource,
        interaction(["not", "a-pair-oracle"], score=score, tier=tier),
        resource_kind="invariant",
    )


def selected(*groups):
    return {
        "schema": 1,
        "base": "base-sha",
        "head": "head-sha",
        "groups": list(groups),
    }


def oracle(*cases):
    return {"schema": 1, "name": "Synthetic oracle", "cases": list(cases)}


def case(id_, resource, features, *, feature_ids=None, **extra):
    result = {
        "id": id_,
        "resource_kind": "fork",
        "resource": resource,
        "features": features,
        **extra,
    }
    if feature_ids is not None:
        result["feature_ids"] = feature_ids
    return result


def judgement(resource, features, **outcome):
    return {
        "resource": resource,
        "features": features,
        "verdict": "unclear",
        "confidence": "low",
        **outcome,
    }


def test_recurring_locations_share_one_cluster_rank_without_losing_identity():
    report = selected(
        group("firstFork", interaction(["Batch", "Sponsor"])),
        group("noise", interaction(["Batch", "Tickets"])),
        group("targetFork", interaction(["Sponsor", "Batch"])),
    )
    result = evaluator.evaluate(
        report,
        oracle(case("target", "targetFork", ["Batch", "Sponsor"])),
        budget=2,
    )

    assert result["cases"][0]["cluster_rank"] == 1
    assert result["cases"][0]["resource_row_rank"] == 3
    assert result["cases"][0]["within_cluster_budget"] is True
    assert result["selection"] == {
        "budget_unit": "feature_pair_cluster",
        "oracle_cases": 1,
        "ranked_clusters": 2,
        "ranked_locations": 3,
        "cluster_found": 1,
        "cluster_hits_at_budget": 1,
        "cluster_recall_at_budget": 1.0,
        "cluster_mean_reciprocal_rank": 1.0,
        "legacy_resource_rows": {
            "ranked_rows": 3,
            "found": 1,
            "hits_at_budget": 0,
            "recall_at_budget": 0.0,
            "mean_reciprocal_rank": 1 / 3,
        },
    }


def test_rank_is_the_global_tier_then_score_order_used_by_the_judge_budget():
    report = selected(
        group("early-low", interaction(["Batch", "Tickets"], score=61)),
        group("context", interaction(["Batch", "Cleanup"], score=999, tier="context")),
        invariant_group("AccountRootsNotDeleted", score=98),
        group("late-high", interaction(["Batch", "Sponsor"], score=97)),
        group(
            "consider", interaction(["Batch", "Lending"], score=120, tier="consider")
        ),
    )
    result = evaluator.evaluate(
        report,
        oracle(case("target", "late-high", ["Batch", "Sponsor"])),
        budget=1,
    )

    assert result["cases"][0]["cluster_rank"] == 2
    assert result["cases"][0]["within_cluster_budget"] is False
    assert result["selection"]["ranked_clusters"] == 4
    assert result["selection"]["legacy_resource_rows"]["ranked_rows"] == 4
    assert result["tiers"] == ["review", "consider"]


def test_recall_counts_each_oracle_case_and_missing_pairs_as_zero_rank():
    report = selected(
        group("fee", interaction(["Batch", "Sponsor"])),
        group("sign", interaction(["Batch", "Tickets"])),
    )
    expected = oracle(
        case("found", "fee", ["Sponsor", "Batch"]),
        case("missing", "other", ["Batch", "Sponsor"]),
    )

    result = evaluator.evaluate(report, expected, budget=1)

    assert result["selection"]["cluster_found"] == 1
    assert result["selection"]["cluster_recall_at_budget"] == 0.5
    assert result["selection"]["cluster_mean_reciprocal_rank"] == 0.5
    assert result["cases"][1]["cluster_rank"] is None


def test_judge_matches_current_and_future_outcome_fields_without_an_enum():
    report = selected(group("fee", interaction(["Batch", "Sponsor"])))
    expected = oracle(
        case(
            "bug",
            "fee",
            ["Batch", "Sponsor"],
            expected_judgement={
                "verdict": ["gap", "possible_gap"],
                "behavior": "broken",
                "coverage": ["missing", "partial"],
            },
        )
    )
    judged = {
        **report,
        "judgements": [
            judgement(
                "fee",
                ["Sponsor", "Batch"],
                verdict="possible_gap",
                behavior="broken",
                coverage="missing",
            )
        ],
    }

    result = evaluator.evaluate(report, expected, judged, budget=1)

    assert result["cases"][0]["judge_status"] == "matched"
    assert result["judge"] == {
        "expected_cases": 1,
        "judged": 1,
        "matched": 1,
        "mismatched": 0,
        "not_judged": 0,
        "coverage": 1.0,
        "match_rate": 1.0,
    }


def test_one_cluster_judgement_satisfies_each_of_its_exact_locations():
    report = selected(
        group("fee", interaction(["Batch", "Sponsor"], score=90)),
        group("sign", interaction(["Sponsor", "Batch"], score=80)),
    )
    expected = oracle(
        case(
            "sign-bug",
            "sign",
            ["Batch", "Sponsor"],
            expected_judgement={"behavior": "broken", "coverage": "covered"},
        )
    )
    judged = {
        **report,
        "judgements": [
            {
                **judgement(
                    "fee",
                    ["Batch", "Sponsor"],
                    behavior="broken",
                    coverage="covered",
                    verdict="gap",
                ),
                "locations": [
                    {"resource_kind": "fork", "resource": "fee"},
                    {"resource_kind": "fork", "resource": "sign"},
                ],
            }
        ],
    }

    result = evaluator.evaluate(report, expected, judged, budget=1)

    assert result["cases"][0]["cluster_rank"] == 1
    assert result["cases"][0]["judge_status"] == "matched"


def test_judge_reports_mismatches_separately_from_unjudged_oracle_cases():
    report = selected(
        group("fee", interaction(["Batch", "Sponsor"])),
        group("sign", interaction(["Batch", "Tickets"])),
    )
    expected = oracle(
        case(
            "wrong",
            "fee",
            ["Batch", "Sponsor"],
            expected_judgement={"verdict": "gap"},
        ),
        case(
            "not-run",
            "sign",
            ["Batch", "Tickets"],
            expected_judgement={"verdict": "handled"},
        ),
    )
    judged = {
        **report,
        "judgements": [judgement("fee", ["Batch", "Sponsor"], verdict="unclear")],
    }

    result = evaluator.evaluate(report, expected, judged, budget=2)

    assert [item["judge_status"] for item in result["cases"]] == [
        "mismatched",
        "not_judged",
    ]
    assert result["cases"][0]["judge_mismatches"] == [
        {"field": "verdict", "expected": ["gap"], "observed": "unclear"}
    ]
    assert result["judge"]["coverage"] == 0.5
    assert result["judge"]["match_rate"] == 0.0


def test_infrastructure_failure_does_not_count_as_a_judged_outcome():
    report = selected(group("fee", interaction(["Batch", "Sponsor"])))
    expected = oracle(
        case(
            "failed",
            "fee",
            ["Batch", "Sponsor"],
            expected_judgement={"behavior": "broken"},
        )
    )
    judged = {
        **report,
        "judgements": [
            judgement(
                "fee",
                ["Batch", "Sponsor"],
                behavior="unclear",
                error="bedrock unavailable",
            )
        ],
    }

    result = evaluator.evaluate(report, expected, judged, budget=1)

    assert result["cases"][0]["judge_status"] == "not_judged"
    assert result["judge"]["judged"] == 0
    assert result["judge"]["coverage"] == 0.0


def test_cli_writes_presentation_markdown_and_machine_json(tmp_path, capsys):
    selected_path = tmp_path / "selected.json"
    oracle_path = tmp_path / "oracle.json"
    markdown_path = tmp_path / "report.md"
    json_path = tmp_path / "report.json"
    selected_path.write_text(
        json.dumps(selected(group("fee", interaction(["Batch", "Sponsor"])))),
        encoding="utf-8",
    )
    oracle_path.write_text(
        json.dumps(oracle(case("demo", "fee", ["Batch", "Sponsor"]))),
        encoding="utf-8",
    )

    exit_code = evaluator.main(
        [
            "--selected",
            str(selected_path),
            "--oracle",
            str(oracle_path),
            "--budget",
            "1",
            "--markdown-out",
            str(markdown_path),
            "--json-out",
            str(json_path),
            "--fail-on-miss",
        ]
    )

    assert exit_code == 0
    assert "Recall@1 100.0%" in capsys.readouterr().out
    assert (
        "| demo | Batch x Sponsor @ fork:fee | 1 | 1 | yes |"
        in markdown_path.read_text(encoding="utf-8")
    )
    assert (
        json.loads(json_path.read_text(encoding="utf-8"))["schema"]
        == evaluator.REPORT_SCHEMA
    )


def test_fail_on_miss_is_an_opt_in_regression_exit(tmp_path):
    selected_path = tmp_path / "selected.json"
    oracle_path = tmp_path / "oracle.json"
    selected_path.write_text(json.dumps(selected()), encoding="utf-8")
    oracle_path.write_text(
        json.dumps(oracle(case("missing", "fee", ["Batch", "Sponsor"]))),
        encoding="utf-8",
    )

    assert (
        evaluator.main(
            [
                "--selected",
                str(selected_path),
                "--oracle",
                str(oracle_path),
                "--fail-on-miss",
            ]
        )
        == 1
    )


def test_cli_rejects_a_judged_artifact_from_another_selection(tmp_path, capsys):
    selected_path = tmp_path / "selected.json"
    oracle_path = tmp_path / "oracle.json"
    judged_path = tmp_path / "judged.json"
    report = selected(group("fee", interaction(["Batch", "Sponsor"])))
    selected_path.write_text(json.dumps(report), encoding="utf-8")
    oracle_path.write_text(
        json.dumps(oracle(case("demo", "fee", ["Batch", "Sponsor"]))),
        encoding="utf-8",
    )
    judged_path.write_text(
        json.dumps(
            {
                **report,
                "judge": {"selected_sha256": hashlib.sha256(b"different").hexdigest()},
                "judgements": [],
            }
        ),
        encoding="utf-8",
    )

    assert (
        evaluator.main(
            [
                "--selected",
                str(selected_path),
                "--judged",
                str(judged_path),
                "--oracle",
                str(oracle_path),
            ]
        )
        == 2
    )
    assert "selected_sha256 mismatch" in capsys.readouterr().err


def test_duplicate_resource_qualified_oracle_entries_are_rejected():
    duplicate = case("two", "fee", ["Sponsor", "Batch"])
    with pytest.raises(evaluator.EvaluationError, match="duplicate oracle interaction"):
        evaluator.evaluate(
            selected(),
            oracle(case("one", "fee", ["Batch", "Sponsor"]), duplicate),
        )


def test_canonical_feature_ids_keep_same_display_name_clusters_distinct():
    amendment = "feature:amendment:Shared"
    transactor = "feature:transactor:Shared"
    other = "feature:amendment:Other"
    report = selected(
        group(
            "checkSign",
            interaction(
                ["Shared", "Other"],
                score=95,
                feature_ids=[transactor, other],
            ),
            interaction(
                ["Shared", "Other"],
                score=90,
                feature_ids=[amendment, other],
            ),
        )
    )
    expected = oracle(
        case(
            "transaction",
            "checkSign",
            ["Shared", "Other"],
            feature_ids=[transactor, other],
        ),
        case(
            "amendment",
            "checkSign",
            ["Shared", "Other"],
            feature_ids=[amendment, other],
        ),
    )

    result = evaluator.evaluate(report, expected, budget=2)

    assert [entry["cluster_rank"] for entry in result["cases"]] == [1, 2]
    assert result["selection"]["cluster_recall_at_budget"] == 1.0


def test_cluster_outcome_and_location_attribution_are_scored_separately():
    report = selected(
        group("getFeePayer", interaction(["Batch", "Sponsor"], score=95)),
        group("checkPermission", interaction(["Batch", "Sponsor"], score=90)),
        group("checkSign", interaction(["Batch", "Sponsor"], score=85)),
    )
    expected = oracle(
        case(
            "consent",
            "checkSign",
            ["Batch", "Sponsor"],
            expected_judgement={"behavior": "broken", "coverage": "covered"},
            expected_localization={
                "primary": {"resource_kind": "fork", "resource": "checkSign"},
                "relevant": [
                    {"resource_kind": "fork", "resource": "checkSign"},
                    {"resource_kind": "fork", "resource": "checkPermission"},
                ],
            },
        )
    )
    judged = {
        **report,
        "judgements": [
            {
                **judgement(
                    "getFeePayer",
                    ["Batch", "Sponsor"],
                    kind="interaction",
                    behavior="broken",
                    coverage="covered",
                    verdict="gap",
                ),
                "locations": [
                    {"resource_kind": "fork", "resource": "getFeePayer"},
                    {"resource_kind": "fork", "resource": "checkPermission"},
                    {"resource_kind": "fork", "resource": "checkSign"},
                ],
                "primary_location": {
                    "resource_kind": "fork",
                    "resource": "checkSign",
                },
                "location_assessments": [
                    {
                        "resource_kind": "fork",
                        "resource": "getFeePayer",
                        "role": "not_relevant",
                    },
                    {
                        "resource_kind": "fork",
                        "resource": "checkPermission",
                        "role": "supporting",
                    },
                    {
                        "resource_kind": "fork",
                        "resource": "checkSign",
                        "role": "decisive",
                    },
                ],
            }
        ],
    }

    result = evaluator.evaluate(report, expected, judged, budget=1)

    case_result = result["cases"][0]
    assert case_result["judge_status"] == "matched"
    assert case_result["localization"]["status"] == "matched"
    assert result["judge"]["localization"] == {
        "expected_cases": 1,
        "judged": 1,
        "matched": 1,
        "primary_accuracy": 1.0,
        "mean_relevant_recall": 1.0,
        "mean_relevant_precision": 1.0,
    }


def test_invariant_oracle_matches_the_rule_resource_not_a_synthetic_pair_row():
    report = selected(invariant_group("OverrideFreeze", score=98))
    report["investigation_candidates"] = [
        investigation_candidate(
            "OverrideFreeze",
            ["AMMClawback"],
            score=98,
            resource_kind="invariant",
            kind="invariant",
            feature_ids=["feature:transactor:AMMClawback"],
        )
    ]

    result = evaluator.evaluate(
        report,
        oracle(
            case(
                "invariant",
                "OverrideFreeze",
                ["AMMClawback", "DeepFreeze"],
                resource_kind="invariant",
                match_mode="invariant_route",
            )
        ),
        budget=1,
    )

    assert result["cases"][0]["cluster_rank"] == 1
    assert result["cases"][0]["resource_row_rank"] == 1
    assert result["cases"][0]["match_mode"] == "invariant_route"
    assert result["cases"][0]["observed_selection"] == {
        "resource_kind": "invariant",
        "resource": "OverrideFreeze",
        "authorized_features": ["AMMClawback"],
    }
    assert "invariant route invariant:OverrideFreeze" in evaluator.format_console(
        result
    )
    assert "oracle context: AMMClawback x DeepFreeze" in evaluator.format_markdown(
        result
    )


def test_cluster_ranking_enumerates_the_uncapped_investigation_plane():
    report = selected(group("visible", interaction(["Batch", "Noise"], score=95)))
    report["investigation_candidates"] = [
        investigation_candidate("visible", ["Batch", "Noise"], score=95),
        investigation_candidate("hidden-target", ["Batch", "Sponsor"], score=90),
    ]

    result = evaluator.evaluate(
        report,
        oracle(case("target", "hidden-target", ["Batch", "Sponsor"])),
        budget=1,
    )

    assert result["selection"]["ranked_clusters"] == 2
    assert result["cases"][0]["cluster_rank"] == 2
    assert result["cases"][0]["resource_row_rank"] is None
    assert result["cases"][0]["within_cluster_budget"] is False
