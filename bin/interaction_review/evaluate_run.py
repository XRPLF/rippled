#!/usr/bin/env python3
"""Evaluate one interaction-review run against a small, explicit oracle.

The evaluator measures the two stages separately:

* selection: where the feature-pair investigation or invariant authorization
  route containing each expected location ranked, and whether it appeared
  inside the configured review budget;
* judging: when a judged report is supplied, whether its outcome matches the
  fields named by the oracle.

It deliberately does not know a fixed verdict enum.  An oracle can check the
current ``verdict`` field or future fields such as ``behavior`` and
``coverage`` without changing this script.

Example:

    python evaluate_run.py \
      --selected out/selected.json \
      --judged out/judged.json \
      --oracle path/to/oracle.json \
      --budget 6 \
      --markdown-out out/evaluation.md \
      --json-out out/evaluation.json
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path
from typing import Any

from budget_order import budget_sort_key
from review_clusters import (
    ClusterError,
    build_review_clusters,
    normalized_feature_identities,
)

REPORT_SCHEMA = "interaction-review-evaluation/v2"
MATCH_PAIR_LOCATION = "pair_location"
MATCH_INVARIANT_ROUTE = "invariant_route"


class EvaluationError(ValueError):
    """The input artifacts cannot be evaluated without guessing."""


def _object(value: Any, where: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise EvaluationError(f"{where} must be a JSON object")
    return value


def _array(value: Any, where: str) -> list[Any]:
    if not isinstance(value, list):
        raise EvaluationError(f"{where} must be a JSON array")
    return value


def _text(value: Any, where: str) -> str:
    if not isinstance(value, str) or not value.strip():
        raise EvaluationError(f"{where} must be a non-empty string")
    return value


def _pair(value: Any, where: str) -> tuple[str, str]:
    features = _array(value, where)
    if len(features) != 2:
        raise EvaluationError(f"{where} must contain exactly two feature names")
    names = [_text(name, f"{where}[{index}]") for index, name in enumerate(features)]
    if names[0] == names[1]:
        raise EvaluationError(f"{where} must name two different features")
    return tuple(sorted(names))  # type: ignore[return-value]


def _feature_identity_pair(
    features: tuple[str, str], value: Any, where: str
) -> tuple[str, str]:
    feature_ids = None if value is None else _array(value, where)
    try:
        return normalized_feature_identities(
            features, feature_ids, where=where.removesuffix(".feature_ids")
        )
    except ClusterError as error:
        raise EvaluationError(str(error)) from error


def _load(path: Path, label: str) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except OSError as error:
        raise EvaluationError(f"could not read {label} {path}: {error}") from error
    except json.JSONDecodeError as error:
        raise EvaluationError(f"{label} {path} is not valid JSON: {error}") from error
    return _object(value, label)


def _identity(
    resource_kind: str,
    resource: str,
    feature_identities: tuple[str, str] | None,
) -> tuple[str, str, tuple[str, str] | None]:
    """The evaluation unit: pair *and* the shared location where it meets."""
    # An invariant is one authorization rule for a set of transaction types,
    # not a pair row. Its resource is the stable evaluation unit.
    return (
        resource_kind,
        resource,
        None if resource_kind == "invariant" else feature_identities,
    )


def _ranked_clusters(
    selected: dict[str, Any], tiers: tuple[str, ...]
) -> list[dict[str, Any]]:
    """Return the exact feature-pair cluster order used by the bounded judge."""
    investigation_candidates = selected.get("investigation_candidates")
    if investigation_candidates is not None:
        cluster_cap = len(
            _array(investigation_candidates, "selected.investigation_candidates")
        )
    else:
        # Legacy selected artifacts expose only rendered groups. Invariants
        # consume one cluster even though older singleton groups may contain no
        # pair row.
        cluster_cap = sum(
            max(
                1 if group.get("resource_kind") == "invariant" else 0,
                len(group.get("interactions", ())),
            )
            for group in selected.get("groups", ())
        )
    try:
        clusters = build_review_clusters(
            selected,
            tiers=tiers,
            max_items=cluster_cap,
        )
    except ClusterError as error:
        raise EvaluationError(f"could not cluster selected report: {error}") from error

    rows: list[dict[str, Any]] = []
    for cluster in clusters:
        features = (
            None
            if cluster["kind"] == "invariant"
            else _pair(cluster["features"], "cluster.features")
        )
        rows.append(
            {
                "features": features,
                "observed_features": tuple(cluster["features"]),
                "feature_identities": (
                    None
                    if cluster["kind"] == "invariant"
                    else tuple(cluster["feature_identities"])
                ),
                "score": cluster["score"],
                "tier": cluster["tier"],
                "rank": cluster["rank"],
                "locations": [
                    {
                        "resource_kind": location["resource_kind"],
                        "resource": location["resource"],
                    }
                    for location in cluster["locations"]
                ],
            }
        )
    return rows


def _ranked_resource_rows(
    selected: dict[str, Any], tiers: tuple[str, ...]
) -> list[dict[str, Any]]:
    """Preserve the v1 resource-row order for longitudinal comparisons."""
    rows: list[dict[str, Any]] = []
    for group_index, raw_group in enumerate(
        _array(selected.get("groups"), "selected.groups")
    ):
        group = _object(raw_group, f"selected.groups[{group_index}]")
        resource = _text(
            group.get("resource"), f"selected.groups[{group_index}].resource"
        )
        resource_kind = _text(
            group.get("resource_kind"),
            f"selected.groups[{group_index}].resource_kind",
        )
        interactions = _array(
            group.get("interactions"),
            f"selected.groups[{group_index}].interactions",
        )
        if resource_kind == "invariant":
            interactions = interactions[:1]
        for item_index, raw_item in enumerate(interactions):
            item = _object(
                raw_item,
                f"selected.groups[{group_index}].interactions[{item_index}]",
            )
            tier = _text(
                item.get("tier"),
                f"selected.groups[{group_index}].interactions[{item_index}].tier",
            )
            if tier not in tiers:
                continue
            score = item.get("score")
            if not isinstance(score, int) or isinstance(score, bool):
                raise EvaluationError(
                    f"selected.groups[{group_index}].interactions[{item_index}].score "
                    "must be an integer"
                )
            if resource_kind == "invariant":
                features = None
                identities = None
            else:
                where = f"selected.groups[{group_index}].interactions[{item_index}]"
                features = _pair(item.get("features"), f"{where}.features")
                identities = _feature_identity_pair(
                    features, item.get("feature_ids"), f"{where}.feature_ids"
                )
            rows.append(
                {
                    "resource_kind": resource_kind,
                    "resource": resource,
                    "features": features,
                    "feature_identities": identities,
                    "score": score,
                    "tier": tier,
                }
            )
    # Stable sort intentionally preserves selected group/item order for ties,
    # exactly matching the v1 evaluator.
    rows.sort(key=budget_sort_key)
    for rank, row in enumerate(rows, start=1):
        row["rank"] = rank
    return rows


def _allowed_values(value: Any, where: str) -> list[str]:
    values = value if isinstance(value, list) else [value]
    if not values:
        raise EvaluationError(f"{where} must allow at least one value")
    allowed = [_text(entry, f"{where}[{index}]") for index, entry in enumerate(values)]
    if len(set(allowed)) != len(allowed):
        raise EvaluationError(f"{where} contains duplicate values")
    return allowed


def _oracle_cases(oracle: dict[str, Any]) -> list[dict[str, Any]]:
    raw_cases = _array(oracle.get("cases"), "oracle.cases")
    if not raw_cases:
        raise EvaluationError("oracle.cases must contain at least one case")

    cases: list[dict[str, Any]] = []
    seen_ids: set[str] = set()
    seen_identities: set[tuple[str, str, tuple[str, str] | None]] = set()
    for index, raw_case in enumerate(raw_cases):
        case = _object(raw_case, f"oracle.cases[{index}]")
        case_id = _text(
            case.get("id", f"case-{index + 1}"), f"oracle.cases[{index}].id"
        )
        resource_kind = _text(
            case.get("resource_kind"), f"oracle.cases[{index}].resource_kind"
        )
        resource = _text(case.get("resource"), f"oracle.cases[{index}].resource")
        match_mode = _text(
            case.get("match_mode", MATCH_PAIR_LOCATION),
            f"oracle.cases[{index}].match_mode",
        )
        if match_mode not in {MATCH_PAIR_LOCATION, MATCH_INVARIANT_ROUTE}:
            raise EvaluationError(
                f"oracle.cases[{index}].match_mode must be "
                f"{MATCH_PAIR_LOCATION!r} or {MATCH_INVARIANT_ROUTE!r}"
            )
        if match_mode == MATCH_INVARIANT_ROUTE and resource_kind != "invariant":
            raise EvaluationError(
                f"oracle.cases[{index}].match_mode={MATCH_INVARIANT_ROUTE!r} "
                "requires resource_kind='invariant'"
            )
        features = _pair(case.get("features"), f"oracle.cases[{index}].features")
        feature_identities = _feature_identity_pair(
            features,
            case.get("feature_ids"),
            f"oracle.cases[{index}].feature_ids",
        )
        identity = _identity(resource_kind, resource, feature_identities)
        if case_id in seen_ids:
            raise EvaluationError(f"duplicate oracle case id: {case_id}")
        if identity in seen_identities:
            raise EvaluationError(
                "duplicate oracle interaction: "
                f"{resource_kind}:{resource} ({', '.join(features)})"
            )
        seen_ids.add(case_id)
        seen_identities.add(identity)

        raw_expected = case.get("expected_judgement", {})
        expected_object = _object(
            raw_expected, f"oracle.cases[{index}].expected_judgement"
        )
        expected = {
            _text(
                field, f"oracle.cases[{index}].expected_judgement field"
            ): _allowed_values(
                value,
                f"oracle.cases[{index}].expected_judgement.{field}",
            )
            for field, value in expected_object.items()
        }

        raw_localization = case.get("expected_localization")
        expected_localization = None
        if raw_localization is not None:
            localization = _object(
                raw_localization, f"oracle.cases[{index}].expected_localization"
            )

            def parse_location(raw: Any, where: str) -> dict[str, str]:
                location = _object(raw, where)
                return {
                    "resource_kind": _text(
                        location.get("resource_kind"), f"{where}.resource_kind"
                    ),
                    "resource": _text(location.get("resource"), f"{where}.resource"),
                }

            primary = parse_location(
                localization.get("primary"),
                f"oracle.cases[{index}].expected_localization.primary",
            )
            relevant = [
                parse_location(
                    raw_location,
                    f"oracle.cases[{index}].expected_localization.relevant[{location_index}]",
                )
                for location_index, raw_location in enumerate(
                    _array(
                        localization.get("relevant"),
                        f"oracle.cases[{index}].expected_localization.relevant",
                    )
                )
            ]
            relevant_keys = {
                (location["resource_kind"], location["resource"])
                for location in relevant
            }
            primary_key = (primary["resource_kind"], primary["resource"])
            if primary_key not in relevant_keys:
                raise EvaluationError(
                    f"oracle.cases[{index}].expected_localization.primary must "
                    "also appear in relevant"
                )
            if len(relevant_keys) != len(relevant):
                raise EvaluationError(
                    f"oracle.cases[{index}].expected_localization.relevant "
                    "contains duplicates"
                )
            expected_localization = {"primary": primary, "relevant": relevant}
        cases.append(
            {
                "id": case_id,
                "resource_kind": resource_kind,
                "resource": resource,
                "match_mode": match_mode,
                "features": features,
                "feature_identities": feature_identities,
                "expected_judgement": expected,
                "expected_localization": expected_localization,
            }
        )
    return cases


def _judgements_by_location(
    judged: dict[str, Any] | None,
) -> dict[tuple[str, str, tuple[str, str] | None], dict[str, Any]]:
    """Index judged rows by the same resource-qualified identity as the oracle."""
    if judged is None:
        return {}
    kinds_by_resource: dict[str, set[str]] = {}
    for index, raw_group in enumerate(_array(judged.get("groups"), "judged.groups")):
        group = _object(raw_group, f"judged.groups[{index}]")
        resource = _text(group.get("resource"), f"judged.groups[{index}].resource")
        resource_kind = _text(
            group.get("resource_kind"), f"judged.groups[{index}].resource_kind"
        )
        kinds_by_resource.setdefault(resource, set()).add(resource_kind)

    out: dict[tuple[str, str, tuple[str, str] | None], dict[str, Any]] = {}
    for index, raw_record in enumerate(
        _array(judged.get("judgements"), "judged.judgements")
    ):
        record = _object(raw_record, f"judged.judgements[{index}]")
        # Infrastructure failures are not model opinions and do not count as
        # judge coverage. The renderer follows the same fail-open rule.
        if record.get("error") is not None:
            continue
        raw_features = record.get("features")
        kind = record.get("kind", "interaction")
        if kind == "invariant":
            features = None
            feature_identities = None
        else:
            if not isinstance(raw_features, list) or len(raw_features) != 2:
                continue
            features = _pair(raw_features, f"judged.judgements[{index}].features")
            feature_identities = _feature_identity_pair(
                features,
                record.get("feature_identities"),
                f"judged.judgements[{index}].feature_identities",
            )
        raw_locations = record.get("locations")
        if raw_locations is not None:
            locations = _array(raw_locations, f"judged.judgements[{index}].locations")
            location_pairs = [
                (
                    _text(
                        _object(location, "cluster location").get("resource_kind"),
                        f"judged.judgements[{index}].locations.resource_kind",
                    ),
                    _text(
                        _object(location, "cluster location").get("resource"),
                        f"judged.judgements[{index}].locations.resource",
                    ),
                )
                for location in locations
            ]
        else:
            resource = _text(
                record.get("resource"), f"judged.judgements[{index}].resource"
            )
            resource_kind = record.get("resource_kind")
            if resource_kind is None:
                known_kinds = kinds_by_resource.get(resource, set())
                if len(known_kinds) != 1:
                    raise EvaluationError(
                        "cannot infer one resource kind for judged row "
                        f"{resource} ({', '.join(features)})"
                    )
                resource_kind = next(iter(known_kinds))
            location_pairs = [
                (
                    _text(
                        resource_kind,
                        f"judged.judgements[{index}].resource_kind",
                    ),
                    resource,
                )
            ]

        for resource_kind, resource in location_pairs:
            key = _identity(resource_kind, resource, feature_identities)
            if key in out:
                raise EvaluationError(
                    "judged report contains duplicate rows for "
                    f"{resource} ({', '.join(features or ())})"
                )
            out[key] = record
    return out


def _location_key(location: dict[str, Any]) -> tuple[str, str]:
    return location["resource_kind"], location["resource"]


def _evaluate_localization(
    record: dict[str, Any] | None, expected: dict[str, Any] | None
) -> dict[str, Any] | None:
    if expected is None:
        return None
    expected_primary = _location_key(expected["primary"])
    expected_relevant = {_location_key(location) for location in expected["relevant"]}
    if record is None or not isinstance(record.get("location_assessments"), list):
        return {
            "status": "not_judged",
            "primary_match": None,
            "relevant_recall": None,
            "relevant_precision": None,
            "expected_primary": expected["primary"],
            "observed_primary": None,
            "expected_relevant": expected["relevant"],
            "observed_relevant": [],
        }

    primary = record.get("primary_location")
    observed_primary = (
        None
        if not isinstance(primary, dict)
        else {
            "resource_kind": primary.get("resource_kind"),
            "resource": primary.get("resource"),
        }
    )
    observed_relevant = {
        (assessment.get("resource_kind"), assessment.get("resource"))
        for assessment in record["location_assessments"]
        if assessment.get("role") in ("decisive", "supporting")
    }
    overlap = expected_relevant & observed_relevant
    primary_match = (
        observed_primary is not None
        and _location_key(observed_primary) == expected_primary
    )
    recall = len(overlap) / len(expected_relevant)
    precision = None if not observed_relevant else len(overlap) / len(observed_relevant)
    status = (
        "matched"
        if primary_match and observed_relevant == expected_relevant
        else "mismatched"
    )
    return {
        "status": status,
        "primary_match": primary_match,
        "relevant_recall": recall,
        "relevant_precision": precision,
        "expected_primary": expected["primary"],
        "observed_primary": observed_primary,
        "expected_relevant": expected["relevant"],
        "observed_relevant": [
            {"resource_kind": kind, "resource": resource}
            for kind, resource in sorted(observed_relevant)
        ],
    }


def evaluate(
    selected: dict[str, Any],
    oracle: dict[str, Any],
    judged: dict[str, Any] | None = None,
    *,
    budget: int = 6,
    tiers: tuple[str, ...] = ("review", "consider"),
    judged_selection_verified: bool | None = None,
) -> dict[str, Any]:
    """Return presentation-ready metrics without reading or writing files."""
    if budget < 0:
        raise EvaluationError("budget must be zero or greater")

    if not tiers:
        raise EvaluationError("tiers must contain at least one tier")
    selected_object = _object(selected, "selected")
    clusters = _ranked_clusters(selected_object, tiers)
    resource_rows = _ranked_resource_rows(selected_object, tiers)
    cases = _oracle_cases(_object(oracle, "oracle"))
    cluster_rank_by_identity: dict[tuple[str, str, tuple[str, str] | None], int] = {}
    cluster_by_identity: dict[
        tuple[str, str, tuple[str, str] | None], dict[str, Any]
    ] = {}
    for cluster in clusters:
        for location in cluster["locations"]:
            identity = _identity(
                location["resource_kind"],
                location["resource"],
                cluster["feature_identities"],
            )
            cluster_rank_by_identity.setdefault(identity, cluster["rank"])
            cluster_by_identity.setdefault(identity, cluster)
    resource_rank_by_identity = {
        _identity(
            row["resource_kind"], row["resource"], row["feature_identities"]
        ): row["rank"]
        for row in resource_rows
    }

    judgement_index = _judgements_by_location(judged)
    evaluated_cases: list[dict[str, Any]] = []
    for case in cases:
        identity = _identity(
            case["resource_kind"], case["resource"], case["feature_identities"]
        )
        cluster_rank = cluster_rank_by_identity.get(identity)
        selected_cluster = cluster_by_identity.get(identity)
        resource_rank = resource_rank_by_identity.get(identity)
        within_cluster_budget = cluster_rank is not None and cluster_rank <= budget
        within_resource_budget = resource_rank is not None and resource_rank <= budget
        expected = case["expected_judgement"]
        record = judgement_index.get(identity)

        if not expected:
            judge_status = "not_evaluated"
            mismatches: list[dict[str, Any]] = []
        elif record is None:
            judge_status = "not_judged"
            mismatches = []
        else:
            mismatches = []
            for field, allowed in expected.items():
                observed = record.get(field)
                if observed not in allowed:
                    mismatches.append(
                        {
                            "field": field,
                            "expected": allowed,
                            "observed": observed,
                        }
                    )
            judge_status = "matched" if not mismatches else "mismatched"

        observed_judgement = None
        if record is not None:
            fields = set(expected) | {"verdict", "confidence"}
            observed_judgement = {
                field: record[field] for field in sorted(fields) if field in record
            }

        localization = _evaluate_localization(record, case["expected_localization"])

        evaluated_cases.append(
            {
                "id": case["id"],
                "resource_kind": case["resource_kind"],
                "resource": case["resource"],
                "match_mode": case["match_mode"],
                "features": list(case["features"]),
                "feature_identities": list(case["feature_identities"]),
                "observed_selection": (
                    {
                        "resource_kind": case["resource_kind"],
                        "resource": case["resource"],
                        "authorized_features": list(
                            selected_cluster.get("observed_features", ())
                        ),
                    }
                    if case["match_mode"] == MATCH_INVARIANT_ROUTE
                    and selected_cluster is not None
                    else None
                ),
                "cluster_rank": cluster_rank,
                "resource_row_rank": resource_rank,
                "cluster_reciprocal_rank": (
                    0.0 if cluster_rank is None else 1.0 / cluster_rank
                ),
                "resource_row_reciprocal_rank": (
                    0.0 if resource_rank is None else 1.0 / resource_rank
                ),
                "within_cluster_budget": within_cluster_budget,
                "within_resource_row_budget": within_resource_budget,
                "expected_judgement": expected,
                "observed_judgement": observed_judgement,
                "judge_status": judge_status,
                "judge_mismatches": mismatches,
                "localization": localization,
            }
        )

    total = len(evaluated_cases)
    cluster_found = sum(case["cluster_rank"] is not None for case in evaluated_cases)
    cluster_hits = sum(case["within_cluster_budget"] for case in evaluated_cases)
    resource_found = sum(
        case["resource_row_rank"] is not None for case in evaluated_cases
    )
    resource_hits = sum(case["within_resource_row_budget"] for case in evaluated_cases)
    selection = {
        "budget_unit": "feature_pair_cluster",
        "oracle_cases": total,
        "ranked_clusters": len(clusters),
        "ranked_locations": len(resource_rows),
        "cluster_found": cluster_found,
        "cluster_hits_at_budget": cluster_hits,
        "cluster_recall_at_budget": cluster_hits / total,
        "cluster_mean_reciprocal_rank": sum(
            case["cluster_reciprocal_rank"] for case in evaluated_cases
        )
        / total,
        "legacy_resource_rows": {
            "ranked_rows": len(resource_rows),
            "found": resource_found,
            "hits_at_budget": resource_hits,
            "recall_at_budget": resource_hits / total,
            "mean_reciprocal_rank": sum(
                case["resource_row_reciprocal_rank"] for case in evaluated_cases
            )
            / total,
        },
    }

    judge_metrics = None
    if judged is not None:
        expected_cases = [
            case for case in evaluated_cases if case["expected_judgement"]
        ]
        judged_cases = [
            case for case in expected_cases if case["judge_status"] != "not_judged"
        ]
        matched = sum(case["judge_status"] == "matched" for case in expected_cases)
        mismatched = sum(
            case["judge_status"] == "mismatched" for case in expected_cases
        )
        judge_metrics = {
            "expected_cases": len(expected_cases),
            "judged": len(judged_cases),
            "matched": matched,
            "mismatched": mismatched,
            "not_judged": len(expected_cases) - len(judged_cases),
            "coverage": (
                None if not expected_cases else len(judged_cases) / len(expected_cases)
            ),
            "match_rate": None if not judged_cases else matched / len(judged_cases),
        }
        localization_cases = [
            case["localization"]
            for case in evaluated_cases
            if case["localization"] is not None
        ]
        localized = [
            result for result in localization_cases if result["status"] != "not_judged"
        ]
        if localization_cases:
            judge_metrics["localization"] = {
                "expected_cases": len(localization_cases),
                "judged": len(localized),
                "matched": sum(result["status"] == "matched" for result in localized),
                "primary_accuracy": (
                    None
                    if not localized
                    else sum(result["primary_match"] is True for result in localized)
                    / len(localized)
                ),
                "mean_relevant_recall": (
                    None
                    if not localized
                    else sum(result["relevant_recall"] for result in localized)
                    / len(localized)
                ),
                "mean_relevant_precision": (
                    None
                    if not [
                        result
                        for result in localized
                        if result["relevant_precision"] is not None
                    ]
                    else sum(
                        result["relevant_precision"]
                        for result in localized
                        if result["relevant_precision"] is not None
                    )
                    / sum(
                        result["relevant_precision"] is not None for result in localized
                    )
                ),
            }

    return {
        "schema": REPORT_SCHEMA,
        "name": oracle.get("name", "Interaction review evaluation"),
        "description": oracle.get("description", ""),
        "base": selected.get("base"),
        "head": selected.get("head"),
        "budget": budget,
        "budget_unit": "feature_pair_cluster",
        "tiers": list(tiers),
        "judged_selection_verified": judged_selection_verified,
        "selection": selection,
        "judge": judge_metrics,
        "cases": evaluated_cases,
    }


def _percent(value: float | None) -> str:
    return "n/a" if value is None else f"{value:.1%}"


def _expected_text(expected: dict[str, list[str]]) -> str:
    return "; ".join(
        f"{field}={'|'.join(values)}" for field, values in sorted(expected.items())
    )


def _observed_text(observed: dict[str, Any] | None) -> str:
    if observed is None:
        return "not judged"
    return "; ".join(f"{field}={value}" for field, value in observed.items())


def _selection_target_text(case: dict[str, Any]) -> str:
    """Describe what static selection actually established for an oracle case."""
    if case.get("match_mode") == MATCH_INVARIANT_ROUTE:
        observed = case.get("observed_selection") or {}
        holders = observed.get("authorized_features") or []
        holder_text = ", ".join(holders) if holders else "authorization holder unknown"
        return (
            f"invariant route {case['resource_kind']}:{case['resource']} "
            f"(observed holder: {holder_text}; oracle context: "
            f"{' x '.join(case['features'])})"
        )
    return (
        f"{' x '.join(case['features'])} @ "
        f"{case['resource_kind']}:{case['resource']}"
    )


def format_console(report: dict[str, Any]) -> str:
    """Compact text intended to be useful both live and in captured logs."""
    selection = report["selection"]
    lines = [
        str(report["name"]),
        (
            f"Cluster selection: {selection['cluster_hits_at_budget']}/"
            f"{selection['oracle_cases']} in top {report['budget']} clusters "
            f"(Cluster Recall@{report['budget']} "
            f"{_percent(selection['cluster_recall_at_budget'])}); "
            f"{selection['cluster_found']}/{selection['oracle_cases']} found "
            f"overall; cluster MRR "
            f"{selection['cluster_mean_reciprocal_rank']:.3f}"
        ),
        (
            "Legacy resource-row comparison: "
            f"{selection['legacy_resource_rows']['hits_at_budget']}/"
            f"{selection['oracle_cases']} in top {report['budget']} rows; MRR "
            f"{selection['legacy_resource_rows']['mean_reciprocal_rank']:.3f}"
        ),
    ]
    judge = report["judge"]
    if judge is None:
        lines.append("Judge: not supplied (selection-only evaluation)")
    elif judge["expected_cases"] == 0:
        lines.append("Judge: no oracle outcomes specified")
    else:
        lines.append(
            f"Judge: {judge['matched']}/{judge['judged']} judged outcomes matched; "
            f"{judge['judged']}/{judge['expected_cases']} expected cases judged "
            f"(coverage {_percent(judge['coverage'])}, "
            f"match {_percent(judge['match_rate'])})"
        )
        localization = judge.get("localization")
        if localization and localization["expected_cases"]:
            lines.append(
                "Localization: "
                f"{localization['matched']}/{localization['judged']} exact; "
                f"primary accuracy {_percent(localization['primary_accuracy'])}; "
                "relevant-location recall "
                f"{_percent(localization['mean_relevant_recall'])}"
            )
        if report["judged_selection_verified"] is None:
            lines.append(
                "Judge artifact pairing: unverified (no selected_sha256 metadata)"
            )

    lines.append("Cases:")
    for case in report["cases"]:
        rank = (
            "not selected"
            if case["cluster_rank"] is None
            else f"cluster rank {case['cluster_rank']}"
        )
        selected_status = (
            "hit"
            if case["within_cluster_budget"]
            else f"miss@{report['budget']} clusters"
        )
        judge_text = case["judge_status"].replace("_", " ")
        if case["observed_judgement"] is not None:
            judge_text += f" ({_observed_text(case['observed_judgement'])})"
        lines.append(
            f"  [{selected_status}] {case['id']}: "
            f"{_selection_target_text(case)} — {rank}; {judge_text}"
        )
    return "\n".join(lines)


def _md(value: Any) -> str:
    return str(value).replace("|", "\\|").replace("\n", " ")


def format_markdown(report: dict[str, Any]) -> str:
    """A small report suitable for a slide appendix or PR description."""
    selection = report["selection"]
    title = _md(report["name"])
    lines = [
        f"# {title}",
        "",
    ]
    if report.get("description"):
        lines += [_md(report["description"]), ""]
    lines += [
        f"- **Cluster Recall@{report['budget']}:** "
        f"{_percent(selection['cluster_recall_at_budget'])} "
        f"({selection['cluster_hits_at_budget']}/{selection['oracle_cases']})",
        f"- **Found at any cluster rank:** "
        f"{selection['cluster_found']}/{selection['oracle_cases']}",
        f"- **Cluster mean reciprocal rank:** "
        f"{selection['cluster_mean_reciprocal_rank']:.3f}",
        f"- **Legacy resource-row Recall@{report['budget']}:** "
        f"{_percent(selection['legacy_resource_rows']['recall_at_budget'])} "
        f"({selection['legacy_resource_rows']['hits_at_budget']}/"
        f"{selection['oracle_cases']})",
    ]
    judge = report["judge"]
    if judge is None:
        lines.append("- **Judge:** not supplied; selection only")
    elif judge["expected_cases"] == 0:
        lines.append("- **Judge:** no expected outcomes in the oracle")
    else:
        lines.append(
            f"- **Judge match:** {_percent(judge['match_rate'])} "
            f"({judge['matched']}/{judge['judged']} judged), with "
            f"{_percent(judge['coverage'])} oracle coverage"
        )
        localization = judge.get("localization")
        if localization and localization["expected_cases"]:
            lines.append(
                f"- **Localization:** {localization['matched']}/"
                f"{localization['judged']} exact; primary accuracy "
                f"{_percent(localization['primary_accuracy'])}; relevant-location "
                f"recall {_percent(localization['mean_relevant_recall'])}"
            )
        if report["judged_selection_verified"] is None:
            lines.append(
                "- **Artifact pairing:** unverified; judged metadata has no "
                "`selected_sha256`"
            )

    lines += [
        "",
        f"| Case | Selection target | Cluster rank | Row rank (v1) | "
        f"Top {report['budget']} clusters | "
        "Expected judge | Observed judge | Match |",
        "| --- | --- | ---: | ---: | :---: | --- | --- | :---: |",
    ]
    status = {
        "matched": "yes",
        "mismatched": "no",
        "not_judged": "not judged",
        "not_evaluated": "—",
    }
    for case in report["cases"]:
        pair = _selection_target_text(case)
        cluster_rank = "—" if case["cluster_rank"] is None else case["cluster_rank"]
        row_rank = (
            "—" if case["resource_row_rank"] is None else case["resource_row_rank"]
        )
        expected = _expected_text(case["expected_judgement"]) or "—"
        observed = _observed_text(case["observed_judgement"])
        lines.append(
            f"| {_md(case['id'])} | {_md(pair)} | {cluster_rank} | {row_rank} | "
            f"{'yes' if case['within_cluster_budget'] else 'no'} | {_md(expected)} | "
            f"{_md(observed)} | {status[case['judge_status']]} |"
        )
    lines.append("")
    return "\n".join(lines)


def _fails_oracle(report: dict[str, Any], judge_supplied: bool) -> bool:
    if (
        report["selection"]["cluster_hits_at_budget"]
        != report["selection"]["oracle_cases"]
    ):
        return True
    if judge_supplied:
        return any(
            case["expected_judgement"] and case["judge_status"] != "matched"
            for case in report["cases"]
        )
    return False


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--selected", type=Path, required=True)
    parser.add_argument("--oracle", type=Path, required=True)
    parser.add_argument("--judged", type=Path)
    parser.add_argument(
        "--budget",
        type=int,
        default=6,
        help="number of ranked feature-pair clusters available for review (default: 6)",
    )
    parser.add_argument(
        "--tiers",
        default="review,consider",
        help="comma-separated tiers eligible for the judge budget (default: review,consider)",
    )
    parser.add_argument("--markdown-out", type=Path)
    parser.add_argument("--json-out", type=Path)
    parser.add_argument(
        "--fail-on-miss",
        action="store_true",
        help="exit 1 when an oracle case misses the budget or expected judgement",
    )
    args = parser.parse_args(argv)

    try:
        selected = _load(args.selected, "selected report")
        oracle = _load(args.oracle, "oracle")
        judged = _load(args.judged, "judged report") if args.judged else None
        pairing_verified = None
        if judged is not None:
            expected_hash = judged.get("judge", {}).get("selected_sha256")
            if expected_hash is not None:
                actual_hash = hashlib.sha256(args.selected.read_bytes()).hexdigest()
                if expected_hash != actual_hash:
                    raise EvaluationError(
                        "judged report was produced from a different selected "
                        "artifact (selected_sha256 mismatch)"
                    )
                pairing_verified = True
        tiers = tuple(tier.strip() for tier in args.tiers.split(",") if tier.strip())
        report = evaluate(
            selected,
            oracle,
            judged,
            budget=args.budget,
            tiers=tiers,
            judged_selection_verified=pairing_verified,
        )
        if args.markdown_out:
            args.markdown_out.parent.mkdir(parents=True, exist_ok=True)
            args.markdown_out.write_text(format_markdown(report), encoding="utf-8")
        if args.json_out:
            args.json_out.parent.mkdir(parents=True, exist_ok=True)
            args.json_out.write_text(
                json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
            )
    except EvaluationError as error:
        print(f"evaluation error: {error}", file=sys.stderr)
        return 2

    print(format_console(report))
    if args.fail_on_miss and _fails_oracle(report, args.judged is not None):
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
