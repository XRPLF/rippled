"""Build deterministic, cross-resource investigation units for the AI judge.

The same feature pair may meet at several locations. This module combines those
locations so the judge investigates the pair once. The best eligible location
determines cluster rank, but every selected location remains in the packet.

New selected artifacts provide the complete ``investigation_candidates`` list;
older artifacts fall back to rendered groups. Graph feature IDs are canonical.
When an older artifact has display names only, the names are explicitly
namespaced so they cannot be confused with graph IDs.
"""

from __future__ import annotations

import json
from collections.abc import Callable, Sequence
from copy import deepcopy
from typing import Any

from budget_order import budget_sort_key

FeatureIdentity = Callable[[dict[str, Any], dict[str, Any], int], str]
LEGACY_FEATURE_PREFIX = "feature:display-name:"


class ClusterError(ValueError):
    """The selected report cannot be clustered without losing identity."""


def normalized_feature_identities(
    features: Sequence[Any],
    feature_ids: Sequence[Any] | None = None,
    *,
    where: str = "feature pair",
) -> tuple[str, str]:
    """Return one canonical, order-independent pair identity.

    New artifacts carry graph feature IDs. Older artifacts only have display
    names, so namespace those names explicitly instead of mixing them with real
    graph IDs. Every cluster consumer uses this helper so legacy selection,
    judging, rendering, and evaluation cannot disagree about the same pair.
    """
    names = list(features)
    if len(names) != 2:
        raise ClusterError(f"{where}.features must contain two entries")
    for index, name in enumerate(names):
        _text(name, f"{where}.features[{index}]")

    if feature_ids is None:
        identities = [f"{LEGACY_FEATURE_PREFIX}{name}" for name in names]
    else:
        identities = list(feature_ids)
        if len(identities) != 2:
            raise ClusterError(f"{where}.feature_ids must contain two entries")
        identities = [
            _text(identity, f"{where}.feature_ids[{index}]")
            for index, identity in enumerate(identities)
        ]
    if identities[0] == identities[1]:
        raise ClusterError(
            f"{where} resolves both endpoints to {identities[0]!r}; "
            "supply graph feature IDs to disambiguate them"
        )
    return tuple(sorted(identities))  # type: ignore[return-value]


def _array(value: Any, where: str) -> list[Any]:
    if not isinstance(value, list):
        raise ClusterError(f"{where} must be an array")
    return value


def _text(value: Any, where: str) -> str:
    if not isinstance(value, str) or not value:
        raise ClusterError(f"{where} must be a non-empty string")
    return value


def _integer(value: Any, where: str) -> int:
    if not isinstance(value, int) or isinstance(value, bool):
        raise ClusterError(f"{where} must be an integer")
    return value


def _default_feature_identity(
    group: dict[str, Any], item: dict[str, Any], index: int
) -> str:
    del group
    features = _array(item.get("features"), "interaction.features")
    feature_ids = item.get("feature_ids")
    identities = normalized_feature_identities(
        features,
        None if feature_ids is None else _array(feature_ids, "interaction.feature_ids"),
        where="interaction",
    )
    # ``normalized_feature_identities`` sorts for cluster identity, while this
    # callback must preserve endpoint order so names/roles/vias stay aligned.
    if feature_ids is not None:
        return _text(feature_ids[index], f"interaction.feature_ids[{index}]")
    name = _text(features[index], f"interaction.features[{index}]")
    identity = f"{LEGACY_FEATURE_PREFIX}{name}"
    assert identity in identities
    return identity


def _pair_identity(
    group: dict[str, Any],
    item: dict[str, Any],
    resolver: FeatureIdentity,
) -> tuple[tuple[str, str], tuple[str, str]]:
    identities = tuple(resolver(group, item, index) for index in range(2))
    if any(not isinstance(value, str) or not value for value in identities):
        raise ClusterError("feature_identity must return non-empty strings")
    if identities[0] == identities[1]:
        names = item.get("features", [])
        raise ClusterError(
            "an interaction's endpoints resolved to the same feature identity: "
            f"{names!r}; supply graph feature IDs to disambiguate them"
        )
    return tuple(sorted(identities)), identities


def _identity_key(identity: tuple[str, ...]) -> str:
    """Readable, collision-free JSON identity suitable for artifact keys."""
    return json.dumps(identity, ensure_ascii=True, separators=(",", ":"))


def _tier_score_key(row: dict[str, Any]) -> tuple[int, int]:
    # Keep the shared budget policy in one place.  ``budget_sort_key`` places
    # unknown tiers after review/consider, which is exactly where context goes.
    return budget_sort_key(row)


def _location_tiebreak(location: dict[str, Any]) -> tuple[str, ...]:
    return (
        location["resource_kind"],
        location["resource"],
        location["cluster_key"],
    )


def _location_sort_key(location: dict[str, Any]) -> tuple[Any, ...]:
    return (*_tier_score_key(location), *_location_tiebreak(location))


def _interaction_location(
    group: dict[str, Any],
    item: dict[str, Any],
    identity: tuple[str, str],
    row_identities: tuple[str, str],
) -> dict[str, Any]:
    features = _array(item.get("features"), "interaction.features")
    if len(features) != 2:
        raise ClusterError("interaction.features must contain two entries")
    names = tuple(
        _text(name, f"interaction.features[{index}]")
        for index, name in enumerate(features)
    )
    tier = _text(item.get("tier"), "interaction.tier")
    score = _integer(item.get("score"), "interaction.score")
    resource = _text(group.get("resource"), "group.resource")
    resource_kind = _text(group.get("resource_kind"), "group.resource_kind")
    cluster_key = _identity_key(("interaction", *identity))

    canonical_names = tuple(
        name
        for identity_value, name in sorted(
            zip(row_identities, names, strict=True), key=lambda pair: pair[0]
        )
    )

    return {
        "key": _identity_key(("location", resource_kind, resource, *identity)),
        "cluster_key": cluster_key,
        "resource": resource,
        "resource_kind": resource_kind,
        "features": list(names),
        "feature_identities": list(row_identities),
        "canonical_features": list(canonical_names),
        "canonical_feature_identities": list(identity),
        "kind": _text(item.get("kind"), "interaction.kind"),
        "roles": deepcopy(_array(item.get("roles"), "interaction.roles")),
        "vias": deepcopy(_array(item.get("vias"), "interaction.vias")),
        "tier": tier,
        "score": score,
        "why": deepcopy(_array(item.get("why"), "interaction.why")),
        "evidence": deepcopy(_array(item.get("evidence"), "interaction.evidence")),
        "signal": group.get("signal"),
        "resource_match": group.get("resource_match"),
        "new_levers": deepcopy(group.get("new_levers", [])),
        "boundary_states": deepcopy(group.get("boundary_states", [])),
        # These copies let the current prompt builders integrate without
        # reaching back into or mutating the input report.
        "group": deepcopy(group),
        "item": deepcopy(item),
    }


def _invariant_location(group: dict[str, Any]) -> dict[str, Any] | None:
    interactions = _array(group.get("interactions"), "group.interactions")
    if not interactions:
        return None
    item = interactions[0]
    if not isinstance(item, dict):
        raise ClusterError("group.interactions[0] must be an object")
    resource = _text(group.get("resource"), "group.resource")
    resource_kind = _text(group.get("resource_kind"), "group.resource_kind")
    tier = _text(item.get("tier"), "interaction.tier")
    score = _integer(item.get("score"), "interaction.score")
    identity = ("invariant", resource_kind, resource)
    key = _identity_key(identity)
    evidence = [
        deepcopy(entry)
        for interaction in interactions
        if isinstance(interaction, dict)
        for entry in _array(interaction.get("evidence"), "interaction.evidence")
    ]
    return {
        "key": _identity_key(("location", resource_kind, resource)),
        "cluster_key": key,
        "resource": resource,
        "resource_kind": resource_kind,
        "features": deepcopy(group.get("authorized_features", [])),
        "feature_identities": [],
        "canonical_features": deepcopy(group.get("authorized_features", [])),
        "canonical_feature_identities": [],
        "kind": "invariant",
        "roles": [],
        "vias": [],
        "tier": tier,
        "score": score,
        "why": [],
        "evidence": evidence,
        "signal": group.get("signal"),
        "resource_match": group.get("resource_match"),
        "new_levers": deepcopy(group.get("new_levers", [])),
        "boundary_states": deepcopy(group.get("boundary_states", [])),
        "group": deepcopy(group),
        "item": None,
    }


def _candidate_group(candidate: dict[str, Any]) -> dict[str, Any]:
    """Group-shaped context expected by the existing prompt builders."""

    return {
        "resource": _text(candidate.get("resource"), "candidate.resource"),
        "resource_kind": _text(
            candidate.get("resource_kind"), "candidate.resource_kind"
        ),
        "signal": candidate.get("signal"),
        "resource_match": candidate.get("resource_match"),
        "new_levers": deepcopy(
            _array(candidate.get("new_levers"), "candidate.new_levers")
        ),
        "boundary_states": deepcopy(
            _array(candidate.get("boundary_states"), "candidate.boundary_states")
        ),
        "score": _integer(candidate.get("score"), "candidate.score"),
        "omitted": 0,
        "consumer_cohort": None,
        "authorized_features": deepcopy(
            _array(
                candidate.get("authorized_features"),
                "candidate.authorized_features",
            )
        ),
        "interactions": [],
    }


def _invariant_candidate_location(candidate: dict[str, Any]) -> dict[str, Any]:
    group = _candidate_group(candidate)
    resource = group["resource"]
    resource_kind = group["resource_kind"]
    if resource_kind != "invariant" or candidate.get("kind") != "invariant":
        raise ClusterError("an invariant candidate must name an invariant resource")
    authorized = group["authorized_features"]
    identity = ("invariant", resource_kind, resource)
    key = _identity_key(identity)
    return {
        "key": _identity_key(("location", resource_kind, resource)),
        "cluster_key": key,
        "resource": resource,
        "resource_kind": resource_kind,
        "features": deepcopy(authorized),
        "feature_identities": [],
        "canonical_features": deepcopy(authorized),
        "canonical_feature_identities": [],
        "kind": "invariant",
        "roles": [],
        "vias": [],
        "tier": _text(candidate.get("tier"), "candidate.tier"),
        "score": _integer(candidate.get("score"), "candidate.score"),
        "why": deepcopy(_array(candidate.get("why"), "candidate.why")),
        "evidence": deepcopy(_array(candidate.get("evidence"), "candidate.evidence")),
        "signal": candidate.get("signal"),
        "resource_match": candidate.get("resource_match"),
        "new_levers": deepcopy(candidate.get("new_levers", [])),
        "boundary_states": deepcopy(candidate.get("boundary_states", [])),
        "group": group,
        "item": None,
    }


def _interaction_candidate_location(
    candidate: dict[str, Any], resolver: FeatureIdentity
) -> dict[str, Any]:
    group = _candidate_group(candidate)
    item = {
        key: deepcopy(candidate[key])
        for key in (
            "features",
            "roles",
            "kind",
            "vias",
            "tier",
            "score",
            "why",
            "evidence",
        )
    }
    if "feature_ids" in candidate:
        item["feature_ids"] = deepcopy(candidate["feature_ids"])
    group["interactions"] = [item]
    identity, row_identities = _pair_identity(group, item, resolver)
    return _interaction_location(group, item, identity, row_identities)


def _collect_investigation_candidates(
    report: dict[str, Any], resolver: FeatureIdentity
) -> list[dict[str, Any]]:
    candidates = _array(
        report.get("investigation_candidates"), "selected.investigation_candidates"
    )
    locations: list[dict[str, Any]] = []
    seen: set[str] = set()
    for index, raw_candidate in enumerate(candidates):
        if not isinstance(raw_candidate, dict):
            raise ClusterError(
                f"selected.investigation_candidates[{index}] must be an object"
            )
        location = (
            _invariant_candidate_location(raw_candidate)
            if raw_candidate.get("kind") == "invariant"
            else _interaction_candidate_location(raw_candidate, resolver)
        )
        if location["key"] in seen:
            raise ClusterError(
                "duplicate resource-qualified investigation candidate: "
                f"{location['key']}"
            )
        seen.add(location["key"])
        locations.append(location)
    return locations


def _collect_locations(
    report: dict[str, Any], resolver: FeatureIdentity
) -> list[dict[str, Any]]:
    if "investigation_candidates" in report:
        return _collect_investigation_candidates(report, resolver)

    groups = _array(report.get("groups"), "selected.groups")
    locations: list[dict[str, Any]] = []
    seen: set[str] = set()

    for group_index, raw_group in enumerate(groups):
        if not isinstance(raw_group, dict):
            raise ClusterError(f"selected.groups[{group_index}] must be an object")
        group = raw_group
        resource_kind = _text(
            group.get("resource_kind"),
            f"selected.groups[{group_index}].resource_kind",
        )
        if resource_kind == "invariant":
            location = _invariant_location(group)
            candidates = [] if location is None else [location]
        else:
            candidates = []
            for item_index, raw_item in enumerate(
                _array(
                    group.get("interactions"),
                    f"selected.groups[{group_index}].interactions",
                )
            ):
                if not isinstance(raw_item, dict):
                    raise ClusterError(
                        "selected.groups"
                        f"[{group_index}].interactions[{item_index}] must be an object"
                    )
                identity, row_identities = _pair_identity(group, raw_item, resolver)
                candidates.append(
                    _interaction_location(group, raw_item, identity, row_identities)
                )

        for location in candidates:
            if location["key"] in seen:
                raise ClusterError(
                    "duplicate resource-qualified interaction: " f"{location['key']}"
                )
            seen.add(location["key"])
            locations.append(location)

    return locations


def build_review_clusters(
    report: dict[str, Any],
    tiers: Sequence[str] = ("review", "consider"),
    max_items: int = 8,
    *,
    feature_identity: FeatureIdentity | None = None,
) -> list[dict[str, Any]]:
    """Return the highest-ranked feature-pair investigation clusters.

    ``max_items`` is a maximum number of clusters.  Once a cluster wins a slot,
    all of its resource-qualified locations are retained, including context
    locations.  ``selection_rank`` orders every selected row; ``budget_rank``
    orders only rows in ``tiers`` and is ``None`` for an ineligible location.

    The returned dictionaries are independent copies: prompt construction may
    annotate them without mutating the selected report used for hashing and
    evaluation.
    """
    if not isinstance(report, dict):
        raise ClusterError("selected must be an object")
    if not isinstance(max_items, int) or isinstance(max_items, bool):
        raise ClusterError("max_items must be an integer")
    if max_items < 0:
        raise ClusterError("max_items must be non-negative")
    eligible_tiers = tuple(_text(tier, "tiers[]") for tier in tiers)
    if not eligible_tiers:
        raise ClusterError("tiers must contain at least one tier")
    if len(set(eligible_tiers)) != len(eligible_tiers):
        raise ClusterError("tiers must not contain duplicates")

    resolver = feature_identity or _default_feature_identity
    locations = _collect_locations(report, resolver)

    display_name_by_identity: dict[str, str] = {}
    for location in locations:
        if location["kind"] == "invariant":
            continue
        for identity, name in zip(
            location["feature_identities"], location["features"], strict=True
        ):
            previous = display_name_by_identity.setdefault(identity, name)
            if previous != name:
                raise ClusterError(
                    f"feature identity {identity!r} has inconsistent display names: "
                    f"{previous!r} and {name!r}"
                )

    locations.sort(key=_location_sort_key)
    for rank, location in enumerate(locations, start=1):
        location["selection_rank"] = rank

    eligible = [
        location for location in locations if location["tier"] in eligible_tiers
    ]
    for rank, location in enumerate(eligible, start=1):
        location["budget_rank"] = rank
    for location in locations:
        location.setdefault("budget_rank", None)

    by_cluster: dict[str, list[dict[str, Any]]] = {}
    for location in locations:
        by_cluster.setdefault(location["cluster_key"], []).append(location)

    clusters: list[dict[str, Any]] = []
    for key, members in by_cluster.items():
        eligible_members = [
            location for location in members if location["tier"] in eligible_tiers
        ]
        if not eligible_members:
            continue
        eligible_members.sort(key=_location_sort_key)
        best = eligible_members[0]
        members.sort(key=_location_sort_key)
        for rank, location in enumerate(members, start=1):
            location["cluster_location_rank"] = rank

        clusters.append(
            {
                "key": key,
                "kind": best["kind"] if best["kind"] == "invariant" else "interaction",
                "features": deepcopy(best["canonical_features"]),
                "feature_identities": deepcopy(best["canonical_feature_identities"]),
                "tier": best["tier"],
                "score": best["score"],
                "best_location_key": best["key"],
                "best_budget_rank": best["budget_rank"],
                "locations": members,
            }
        )

    clusters.sort(
        key=lambda cluster: (
            *_tier_score_key(cluster),
            cluster["key"],
        )
    )
    for rank, cluster in enumerate(clusters, start=1):
        cluster["rank"] = rank
    return clusters[:max_items]
