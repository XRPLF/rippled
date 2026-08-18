#!/usr/bin/env python3
"""Select and rank the feature interactions a PR puts in scope.

An interaction is a candidate when the diff touches either feature or their
shared resource. Scores order candidates for a fixed review budget; they are
priorities, not probabilities. The artifact retains a complete candidate list
for clustering and evaluation alongside the capped presentation groups.
"""

from __future__ import annotations

import argparse
import json
from dataclasses import dataclass, field
from pathlib import Path

from jsonschema import Draft202012Validator

from graph import SCHEMA_VERSION, SIGNAL_HIGH, SIGNAL_LOW, SIGNAL_MEDIUM, resource_id
from source_snapshot import SourceSnapshot

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parent.parent

# Score contributions. A resource's own signal and a precise span hit on it
# dominate: the boundary lives in the resource, and a span hit means the diff
# landed inside the fork body rather than merely in a file that hosts it.
SIGNAL_SCORE = {SIGNAL_HIGH: 30, SIGNAL_MEDIUM: 18, SIGNAL_LOW: 6}
# A mediator changes what the resource does for everyone, so a pair of mediators
# on one resource is the shape the Batch x Delegation bug had.
KIND_SCORE = {
    "mediator×mediator": 30,
    "mediator×consumer": 15,
    "consumer×consumer": 0,
}
RESOURCE_MATCH_SCORE = {"span": 25, "file": 15}
FEATURE_MATCH_SCORE = {"span": 12, "file": 8}
# A lever the graph has never seen means this diff adds an edge, which is the
# strongest signal available -- the graph a reviewer would reason from is the one
# the PR is changing.
NEW_LEVER_SCORE = 40
# A lever newly referenced by an edited feature is especially useful when the
# other endpoint already owns that exact field or amendment.  This is the
# feature-local counterpart to NEW_LEVER_SCORE: it promotes the concrete pair
# implied by the new edge without promoting every pair involving the feature.
FEATURE_LEVER_OWNER_SCORE = 35
# An extracted state space is what a boundary claim can be made concrete against.
BOUNDARY_STATE_SCORE = 5

TIER_REVIEW = "review"
TIER_CONSIDER = "consider"
TIER_CONTEXT = "context"
TIER_THRESHOLDS = ((TIER_REVIEW, 60), (TIER_CONSIDER, 35))

RESOURCE_WHY = {
    "fork": "shared decision",
    "invariant": "authorization check",
    "shared_sfield": "shared field",
}

# Caps. Without them a fork edit emits every pair on that fork (up to 82 x 82),
# which is a wall of text a reviewer scrolls past.
DEFAULT_MAX_INTERACTIONS = 25
DEFAULT_MAX_PER_RESOURCE = 6
# One touched transactor makes it a consumer on every fork and every SField it
# declares, so without a resource cap a one-line edit produces ~20 sections.
DEFAULT_MAX_RESOURCES = 6


def tier_for(score: int) -> str:
    for name, floor in TIER_THRESHOLDS:
        if score >= floor:
            return name
    return TIER_CONTEXT


@dataclass
class TouchedNode:
    """One entry from `touched.json`, indexed for scoring."""

    id: str
    kind: str
    name: str
    match: str
    signal: str | None
    new_levers: list[str] = field(default_factory=list)
    known_levers: list[str] = field(default_factory=list)
    evidence: list[dict] = field(default_factory=list)

    @classmethod
    def from_json(cls, entry: dict) -> TouchedNode:
        return cls(
            id=entry["id"],
            kind=entry["kind"],
            name=entry["name"],
            match=entry["match"],
            signal=entry.get("signal"),
            new_levers=list(entry.get("new_levers", ())),
            known_levers=list(entry.get("known_levers", ())),
            evidence=list(entry.get("evidence", ())),
        )

    def brief_evidence(self) -> list[dict]:
        """The changed spans, flattened to `{file, lines}` for the artifact."""
        return [
            {"file": e["file"], "lines": [list(pair) for pair in e["lines"]]}
            for e in self.evidence
        ]


def _amendment_name(lever: str) -> str | None:
    """Normalize a source-level amendment global to its feature name."""
    for prefix in ("feature", "fix"):
        if lever.startswith(prefix) and len(lever) > len(prefix):
            return lever[len(prefix) :]
    return None


def _feature_lever_owner_matches(
    interaction: dict, hits: list[TouchedNode | None]
) -> list[tuple[str, str, str]]:
    """New levers on one endpoint that are owned by the opposite endpoint.

    SField/flag ownership is already recorded in an interaction's ``vias``.
    Amendment globals appear in changed C++ as ``featureFoo`` or ``fixFoo`` but
    feature endpoints are named ``Foo``, so those two prefixes are normalized.
    No fuzzy name matching is used: a lever must have one of those exact,
    graph-derived relationships to the other endpoint.
    """
    matches: list[tuple[str, str, str]] = []
    for index, hit in enumerate(hits):
        if hit is None:
            continue
        opposite = 1 - index
        owner = interaction["features"][opposite]
        owner_vias = interaction["vias"][opposite]
        for lever in hit.new_levers:
            if lever in owner_vias or _amendment_name(lever) == owner:
                matches.append((interaction["features"][index], lever, owner))
    return matches


def _feature_ids_by_resource(graph: dict) -> dict[str, dict[str, list[str]]]:
    """`resource id -> feature name -> the feature ids edged to it`.

    Interactions name their features bare (`"Batch"`), while `touched.json` names
    nodes by id (`feature:transactor:Batch`), and a transactor and an amendment
    can share a name. Resolving through the edge list rather than by string
    match keeps that distinction: only features actually edged to this resource
    are candidates for this interaction.
    """
    out: dict[str, dict[str, list[str]]] = {}
    names = {f["id"]: f["name"] for f in graph["features"]}
    for edge in graph["edges"]:
        name = names.get(edge["src"])
        if name is None:
            continue
        out.setdefault(edge["dst"], {}).setdefault(name, [])
        if edge["src"] not in out[edge["dst"]][name]:
            out[edge["dst"]][name].append(edge["src"])
    return out


def _feature_names_by_resource(graph: dict) -> dict[str, list[str]]:
    """All feature names edged to each resource, in stable order."""
    names = {f["id"]: f["name"] for f in graph["features"]}
    out: dict[str, set[str]] = {}
    for edge in graph["edges"]:
        name = names.get(edge["src"])
        if name is not None:
            out.setdefault(edge["dst"], set()).add(name)
    return {rid: sorted(members) for rid, members in out.items()}


def _invariant_candidates(
    graph: dict,
    nodes: dict[str, TouchedNode],
    ids_by_resource: dict[str, dict[str, list[str]]],
    names_by_resource: dict[str, list[str]],
) -> list[dict]:
    """One authorization candidate per in-scope invariant resource.

    Invariants are set-membership rules, not feature pairs.  Enumerating common
    neighbors only produces a row when at least two transaction types hold the
    privilege, so a directly edited single-holder invariant used to disappear
    entirely.  Build the semantic unit from the graph resource and its complete
    authorized set instead.
    """

    feature_names = {feature["id"]: feature["name"] for feature in graph["features"]}
    out: list[dict] = []
    for metadata in graph["resources"]:
        # Early graph fixtures predate the explicit resource ``kind`` field;
        # the canonical id has always carried it.
        kind = metadata.get("kind") or metadata["id"].split(":", 2)[1]
        if kind != "invariant":
            continue
        rid = metadata["id"]
        resource = nodes.get(rid)
        authorized_ids = sorted(
            feature_id
            for feature_ids in ids_by_resource.get(rid, {}).values()
            for feature_id in feature_ids
        )
        feature_hits = [
            nodes[feature_id] for feature_id in authorized_ids if feature_id in nodes
        ]
        if resource is None and not feature_hits:
            continue

        score = SIGNAL_SCORE.get(metadata["signal"], 0)
        why: list[str] = []
        if resource is not None:
            score += RESOURCE_MATCH_SCORE.get(resource.match, 0)
            where = (
                "edited"
                if resource.match == "span"
                else "edited (whole-file attribution)"
            )
            why.append(f"authorization check `{metadata['name']}` {where}")
            if resource.new_levers:
                score += NEW_LEVER_SCORE
        for hit in feature_hits:
            score += FEATURE_MATCH_SCORE.get(hit.match, 0)
            why.append(
                f"`{feature_names.get(hit.id, hit.name)}` "
                f"{'edited' if hit.match == 'span' else 'implementation edited'}"
            )
        boundary_states = list(metadata.get("state_space", ()))
        if boundary_states:
            score += BOUNDARY_STATE_SCORE

        evidence: list[dict] = []
        for node in (resource, *feature_hits):
            if node is not None:
                evidence.extend(node.brief_evidence())

        out.append(
            {
                "resource": metadata["name"],
                "resource_kind": "invariant",
                "resource_id": rid,
                "signal": metadata["signal"],
                "resource_match": resource.match if resource else None,
                "new_levers": list(resource.new_levers) if resource else [],
                "features": names_by_resource.get(rid, []),
                "feature_ids": authorized_ids,
                "roles": [],
                "kind": "invariant",
                "vias": [],
                "boundary_states": boundary_states,
                "features_touched": len(feature_hits),
                "score": score,
                "tier": tier_for(score),
                "why": why or ["an invariant privilege holder was edited"],
                "evidence": evidence,
                "authorized_features": names_by_resource.get(rid, []),
            }
        )
    return out


def _is_noise(interaction: dict, resource_touched: bool, features_touched: int) -> bool:
    """Drop consumer-x-consumer pairs on low-signal resources with weak evidence.

    Two transactors both declaring `sfAmount` interact only in the weakest sense.
    That pair is worth naming when the shared field itself changed, or when the
    PR edited both transactors -- otherwise it is the bulk of the 4350
    interactions and it buries the forks.
    """
    if interaction["kind"] != "consumer×consumer":
        return False
    if interaction["signal"] != SIGNAL_LOW:
        return False
    return not resource_touched and features_touched < 2


def select(
    graph: dict,
    interactions: dict,
    touched: dict,
    *,
    max_interactions: int = DEFAULT_MAX_INTERACTIONS,
    max_per_resource: int = DEFAULT_MAX_PER_RESOURCE,
    max_resources: int = DEFAULT_MAX_RESOURCES,
) -> dict:
    """Rank the interactions this diff puts in scope, grouped by resource."""
    nodes = {e["id"]: TouchedNode.from_json(e) for e in touched["touched"]}
    ids_by_resource = _feature_ids_by_resource(graph)
    names_by_resource = _feature_names_by_resource(graph)

    # ``candidates`` is the bounded audit projection. The investigation view
    # retains every non-noise semantic location before presentation caps or
    # cohort compaction, so a pair can be clustered across all of its resources.
    candidates: list[dict] = []
    investigation_candidates: list[dict] = []
    # Editing a base-pipeline decision puts every transaction type that passes
    # through it into scope. Materializing mediator x transaction rows adds no
    # information when the transaction itself was not edited, so retain the
    # exact population here and represent it once as a cohort.
    cohorts: dict[str, dict] = {}
    dropped_noise = 0
    for interaction in interactions["interactions"]:
        rid = resource_id(interaction["resource_kind"], interaction["resource"])
        resource = nodes.get(rid)
        by_name = ids_by_resource.get(rid, {})

        hits: list[TouchedNode | None] = []
        endpoint_ids = interaction.get("feature_ids")
        for index, name in enumerate(interaction["features"]):
            if endpoint_ids:
                hit = nodes.get(endpoint_ids[index])
            else:
                # Backward compatibility for interaction artifacts produced
                # before canonical endpoint ids were carried end-to-end.
                hit = next(
                    (nodes[fid] for fid in by_name.get(name, ()) if fid in nodes),
                    None,
                )
            hits.append(hit)

        n_features = sum(1 for h in hits if h is not None)
        if resource is None and n_features == 0:
            continue

        cohort_indices: tuple[int, int] | None = None
        if resource is not None and interaction["roles"].count("consumer") == 1:
            consumer_index = interaction["roles"].index("consumer")
            if hits[consumer_index] is None:
                mediator_index = 1 - consumer_index
                cohort_indices = mediator_index, consumer_index

        if _is_noise(interaction, resource is not None, n_features):
            dropped_noise += 1
            continue

        score = SIGNAL_SCORE.get(interaction["signal"], 0)
        score += KIND_SCORE.get(interaction["kind"], 0)
        why: list[str] = []

        if resource is not None:
            score += RESOURCE_MATCH_SCORE.get(resource.match, 0)
            where = (
                "edited"
                if resource.match == "span"
                else "edited (whole-file attribution)"
            )
            label = RESOURCE_WHY.get(interaction["resource_kind"], "shared code")
            why.append(f"{label} `{interaction['resource']}` {where}")
            if resource.new_levers:
                score += NEW_LEVER_SCORE
                levers = ", ".join(f"`{lever}`" for lever in resource.new_levers)
                why.append(
                    f"{levers} appears in the changed code but is not recorded "
                    f"for this {label}"
                )
        for name, hit in zip(interaction["features"], hits, strict=True):
            if hit is None:
                continue
            score += FEATURE_MATCH_SCORE.get(hit.match, 0)
            why.append(
                f"`{name}` {'edited' if hit.match == 'span' else 'implementation edited'}"
            )
        lever_owner_matches = _feature_lever_owner_matches(interaction, hits)
        if lever_owner_matches:
            score += FEATURE_LEVER_OWNER_SCORE
            why.extend(
                f"`{changed}` now references `{lever}`, which connects it "
                f"directly to `{owner}`"
                for changed, lever, owner in lever_owner_matches
            )
        if n_features == 2:
            why.append("both endpoints of the pair were edited")
        if interaction["boundary_states"]:
            score += BOUNDARY_STATE_SCORE

        evidence: list[dict] = []
        for node in (resource, *hits):
            if node is not None:
                evidence.extend(node.brief_evidence())

        candidate = {
            "resource": interaction["resource"],
            "resource_kind": interaction["resource_kind"],
            "resource_id": rid,
            "signal": interaction["signal"],
            "resource_match": resource.match if resource else None,
            "new_levers": list(resource.new_levers) if resource else [],
            "features": list(interaction["features"]),
            "roles": list(interaction["roles"]),
            "kind": interaction["kind"],
            "vias": [list(v) for v in interaction["vias"]],
            "boundary_states": list(interaction["boundary_states"]),
            "features_touched": n_features,
            "score": score,
            "tier": tier_for(score),
            "why": why,
            "evidence": evidence,
            "authorized_features": [],
        }
        if interaction.get("feature_ids") is not None:
            candidate["feature_ids"] = list(interaction["feature_ids"])

        # Invariants are emitted once below as an authorization-set unit. Pair
        # enumeration is retained only for the legacy presentation group.
        if interaction["resource_kind"] != "invariant":
            investigation_candidates.append(candidate)

        if cohort_indices is not None:
            mediator_index, consumer_index = cohort_indices
            cohort = cohorts.setdefault(
                rid,
                {
                    "resource": interaction["resource"],
                    "resource_kind": interaction["resource_kind"],
                    "signal": interaction["signal"],
                    "resource_match": resource.match,
                    "new_levers": list(resource.new_levers),
                    "boundary_states": list(interaction["boundary_states"]),
                    "mediators": set(),
                    "wrappers": set(),
                    "consumers": set(),
                    "pair_count": 0,
                    "score": score,
                },
            )
            cohort["score"] = max(cohort["score"], score)
            mediator = interaction["features"][mediator_index]
            cohort["mediators"].add(mediator)
            if "wrapper" in interaction["vias"][mediator_index]:
                cohort["wrappers"].add(mediator)
            cohort["consumers"].add(interaction["features"][consumer_index])
            cohort["pair_count"] += 1
            continue

        candidates.append(candidate)

    standalone_invariants = _invariant_candidates(
        graph, nodes, ids_by_resource, names_by_resource
    )
    investigation_candidates.extend(standalone_invariants)

    return _group(
        candidates,
        touched,
        investigation_candidates=investigation_candidates,
        standalone_invariants=standalone_invariants,
        cohorts=cohorts,
        names_by_resource=names_by_resource,
        dropped_noise=dropped_noise,
        max_interactions=max_interactions,
        max_per_resource=max_per_resource,
        max_resources=max_resources,
    )


def _sort_key(candidate: dict) -> tuple:
    """Descending score, then a stable alphabetical tiebreak."""
    return (
        -candidate["score"],
        candidate["resource_kind"],
        candidate["resource"],
        candidate["features"],
    )


def _investigation_json(candidate: dict) -> dict:
    """Compact public form of one uncapped semantic location."""

    out = {
        key: candidate[key]
        for key in (
            "resource",
            "resource_kind",
            "signal",
            "resource_match",
            "new_levers",
            "boundary_states",
            "features",
            "roles",
            "kind",
            "vias",
            "score",
            "tier",
            "why",
            "evidence",
            "authorized_features",
        )
    }
    if "feature_ids" in candidate:
        out["feature_ids"] = candidate["feature_ids"]
    return out


def _group(
    candidates: list[dict],
    touched: dict,
    *,
    investigation_candidates: list[dict],
    standalone_invariants: list[dict],
    cohorts: dict[str, dict],
    names_by_resource: dict[str, list[str]],
    dropped_noise: int,
    max_interactions: int,
    max_per_resource: int,
    max_resources: int,
) -> dict:
    """Cap, then bucket by resource, because the boundary belongs to the resource."""
    candidates.sort(key=_sort_key)

    # Resources ranked by their best pair, so the cap keeps whole sections rather
    # than a thin slice of every resource.
    best_by_resource: dict[str, int] = {}
    for candidate in candidates:
        rid = candidate["resource_id"]
        best_by_resource[rid] = max(
            best_by_resource.get(rid, candidate["score"]), candidate["score"]
        )
    for rid, cohort in cohorts.items():
        best_by_resource[rid] = max(
            best_by_resource.get(rid, cohort["score"]), cohort["score"]
        )
    for invariant in standalone_invariants:
        rid = invariant["resource_id"]
        best_by_resource[rid] = max(
            best_by_resource.get(rid, invariant["score"]), invariant["score"]
        )
    ranked = sorted(best_by_resource, key=lambda rid: (-best_by_resource[rid], rid))
    allowed = set(ranked[:max_resources])
    omitted_resources = len(ranked) - len(allowed)

    kept: list[dict] = []
    per_resource: dict[str, int] = {}
    truncated_per_resource: dict[str, int] = {}
    for candidate in candidates:
        rid = candidate["resource_id"]
        if rid not in allowed:
            continue
        if per_resource.get(rid, 0) >= max_per_resource:
            truncated_per_resource[rid] = truncated_per_resource.get(rid, 0) + 1
            continue
        if len(kept) >= max_interactions:
            break
        per_resource[rid] = per_resource.get(rid, 0) + 1
        kept.append(candidate)

    groups: dict[str, dict] = {}
    for candidate in kept:
        rid = candidate["resource_id"]
        group = groups.get(rid)
        if group is None:
            group = groups[rid] = {
                "resource": candidate["resource"],
                "resource_kind": candidate["resource_kind"],
                "signal": candidate["signal"],
                "resource_match": candidate["resource_match"],
                "new_levers": candidate["new_levers"],
                "boundary_states": candidate["boundary_states"],
                "score": candidate["score"],
                "omitted": truncated_per_resource.get(rid, 0),
                "consumer_cohort": _cohort_json(cohorts.get(rid)),
                "authorized_features": (
                    names_by_resource.get(rid, [])
                    if candidate["resource_kind"] == "invariant"
                    else []
                ),
                "interactions": [],
            }
        group["score"] = max(group["score"], candidate["score"])
        if candidate["boundary_states"] and not group["boundary_states"]:
            group["boundary_states"] = candidate["boundary_states"]
        item = {
            key: candidate[key]
            for key in (
                "features",
                "roles",
                "kind",
                "vias",
                "score",
                "tier",
                "why",
                "evidence",
            )
        }
        if "feature_ids" in candidate:
            item["feature_ids"] = candidate["feature_ids"]
        group["interactions"].append(item)

    # A fork with one behavior-changing feature has no mediator x mediator row,
    # but its pass-through cohort is still meaningful and must not disappear.
    for rid, cohort in cohorts.items():
        if rid not in allowed or rid in groups:
            continue
        groups[rid] = {
            "resource": cohort["resource"],
            "resource_kind": cohort["resource_kind"],
            "signal": cohort["signal"],
            "resource_match": cohort["resource_match"],
            "new_levers": cohort["new_levers"],
            "boundary_states": cohort["boundary_states"],
            "score": cohort["score"],
            "omitted": 0,
            "consumer_cohort": _cohort_json(cohort),
            "authorized_features": [],
            "interactions": [],
        }

    # A privilege held by only one transaction type has no pairwise interaction
    # row. It is still one meaningful, bounded authorization section.
    for invariant in standalone_invariants:
        rid = invariant["resource_id"]
        if rid not in allowed:
            continue
        if rid in groups:
            groups[rid]["score"] = max(groups[rid]["score"], invariant["score"])
            groups[rid]["authorized_features"] = invariant["authorized_features"]
            continue
        groups[rid] = {
            "resource": invariant["resource"],
            "resource_kind": "invariant",
            "signal": invariant["signal"],
            "resource_match": invariant["resource_match"],
            "new_levers": invariant["new_levers"],
            "boundary_states": invariant["boundary_states"],
            "score": invariant["score"],
            "omitted": 0,
            "consumer_cohort": None,
            "authorized_features": invariant["authorized_features"],
            "interactions": [],
        }

    ordered = sorted(
        groups.values(),
        key=lambda g: (-g["score"], g["resource_kind"], g["resource"]),
    )

    by_tier: dict[str, int] = {}
    for candidate in kept:
        by_tier[candidate["tier"]] = by_tier.get(candidate["tier"], 0) + 1

    return {
        "schema": SCHEMA_VERSION,
        "base": touched["base"],
        "head": touched["head"],
        "summary": {
            "changed_files": touched["summary"]["changed_files"],
            "touched_nodes": touched["summary"]["touched_nodes"],
            # Keep the bounded audit projection's pair-row accounting separate
            # from the semantic plane. A singleton invariant is one
            # investigation but zero pair rows; a cohort member is one
            # investigation even though the projection summarizes it.
            "candidates": len(candidates)
            + sum(c["pair_count"] for c in cohorts.values()),
            "investigation_candidates": len(investigation_candidates),
            "selected": len(kept),
            "cohort_pairs": sum(c["pair_count"] for c in cohorts.values()),
            "dropped_low_signal": dropped_noise,
            "truncated": len(candidates) - len(kept),
            "omitted_resources": omitted_resources,
            "by_tier": by_tier,
            "new_levers": sorted(
                {lever for e in touched["touched"] for lever in e["new_levers"]}
            ),
        },
        "groups": ordered,
        # ``groups`` is a bounded audit projection kept for artifact
        # compatibility. The judge clusters this complete view, so presentation
        # caps and cohort compaction cannot erase a resource-qualified location
        # before the semantic budget is applied.
        "investigation_candidates": [
            _investigation_json(candidate)
            for candidate in sorted(investigation_candidates, key=_sort_key)
        ],
        "caveats": _caveats(touched),
    }


def _cohort_json(cohort: dict | None) -> dict | None:
    if cohort is None:
        return None
    return {
        "mediators": sorted(cohort["mediators"]),
        "wrappers": sorted(cohort["wrappers"]),
        "consumers": sorted(cohort["consumers"]),
        "pair_count": cohort["pair_count"],
    }


def _n(count: int, singular: str, plural: str | None = None) -> str:
    """`1 file` / `3 files`. "(s)" on every noun reads like a form letter."""
    return f"{count} {singular if count == 1 else (plural or singular + 's')}"


def _caveats(touched: dict) -> list[str]:
    """Limitations stored with the static selection artifact."""
    out: list[str] = []
    for entry in touched["off_span_changes"]:
        out.append(
            f"`{entry['file']}` changed, but outside the parts of it this tool "
            f"tracks ({_n(len(entry['hosts']), 'tracked piece')} live in that "
            f"file). Those edits are not counted anywhere above."
        )
    for entry in touched["structural_changes"]:
        out.append(
            f"`{entry['file']}` changed without any line edits ({entry['kind']}), "
            f"and {_n(len(entry['hosted']), 'tracked piece')} live there. There "
            f"are no changed lines to match against."
        )
    unmapped = touched["summary"]["unmapped_files"]
    if unmapped:
        out.append(
            f"{_n(unmapped, 'changed file')} not tracked at all — that includes "
            f"everything under `src/test`, the payment engine in `tx/paths/`, and "
            f"most invariants. The known gaps are listed in "
            f"`bin/interaction_review/README.md`."
        )
    out.append(
        "Static selection does not check test coverage. The optional model pass "
        "looks for combination tests."
    )
    return out


def validate(report: dict, schema_path: Path) -> None:
    schema = json.loads(schema_path.read_text())
    errors = sorted(
        Draft202012Validator(schema).iter_errors(report),
        key=lambda e: [str(p) for p in e.path],
    )
    if errors:
        msgs = "\n".join(f"  {list(e.path)}: {e.message}" for e in errors[:10])
        raise ValueError(f"selected.json failed schema validation:\n{msgs}")


def _require_schema(path: Path, expected: int, what: str) -> dict:
    data = json.loads(path.read_text())
    if data.get("schema") != expected:
        raise ValueError(
            f"{what} has schema version {data.get('schema')!r}, expected {expected}. "
            f"Rebuild it with the current tools."
        )
    return data


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--graph", default=str(HERE / "out" / "graph.json"))
    parser.add_argument(
        "--interactions", default=str(HERE / "out" / "interactions.json")
    )
    parser.add_argument("--touched", default=str(HERE / "out" / "touched.json"))
    parser.add_argument("--out", help="default: out/selected.json beside --touched")
    parser.add_argument(
        "--repo-root",
        default=str(REPO_ROOT),
        help="repository used to bind the selected source snapshot",
    )
    parser.add_argument(
        "--max-interactions", type=int, default=DEFAULT_MAX_INTERACTIONS
    )
    parser.add_argument(
        "--max-per-resource", type=int, default=DEFAULT_MAX_PER_RESOURCE
    )
    parser.add_argument("--max-resources", type=int, default=DEFAULT_MAX_RESOURCES)
    args = parser.parse_args(argv)

    for flag, path in (
        ("--graph", args.graph),
        ("--interactions", args.interactions),
        ("--touched", args.touched),
    ):
        if not Path(path).is_file():
            parser.error(f"{flag} not found: {path}")

    graph = _require_schema(Path(args.graph), SCHEMA_VERSION, "graph.json")
    interactions = json.loads(Path(args.interactions).read_text())
    touched = _require_schema(Path(args.touched), SCHEMA_VERSION, "touched.json")

    report = select(
        graph,
        interactions,
        touched,
        max_interactions=args.max_interactions,
        max_per_resource=args.max_per_resource,
        max_resources=args.max_resources,
    )
    report["source_binding"] = SourceSnapshot.capture(
        Path(args.repo_root),
        base=report["base"],
        head=report["head"],
    ).binding_metadata()
    validate(report, HERE / "selected.schema.json")

    out_path = (
        Path(args.out) if args.out else Path(args.touched).parent / "selected.json"
    )
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(report, indent=2))

    summary = report["summary"]
    print(
        f"candidates: {summary['candidates']} "
        f"({summary['dropped_low_signal']} low-signal pairs dropped)"
    )
    print(
        f"selected: {summary['selected']} across {len(report['groups'])} "
        f"resource(s) {summary['by_tier']}"
    )
    if summary["truncated"]:
        print(f"omitted by cap: {summary['truncated']}")
    if summary["new_levers"]:
        print(f"new levers: {', '.join(summary['new_levers'])}")
    print(f"wrote {out_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
