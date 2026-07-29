#!/usr/bin/env python3
"""Select and rank the feature interactions a PR puts in scope.

Component B, second half. `pr_map.py` answers "which graph nodes did this diff
touch"; this answers "which feature pairs should a reviewer therefore look at".

The join is over the two artifacts already produced: an interaction from
`interactions.json` is a candidate when the diff touched the resource the pair
shares, or either feature of the pair. Those correspond to the four cases in
DESIGN.md: editing a fork puts every pair on that fork in scope, adding or
editing a transactor puts its pairs in scope, and a new lever means the diff is
changing the graph's own edge set.

Candidacy alone is far too broad to hand a reviewer -- one line changed in
`Payment.cpp` makes Payment a member of several hundred `shared_sfield` pairs --
so candidates are scored and tiered, low-signal noise is dropped, and the result
is capped. Scores are ordinal, not probabilities: their only job is to put the
pair most likely to hide a boundary bug at the top of the comment.
"""

from __future__ import annotations

import argparse
import json
from dataclasses import dataclass, field
from pathlib import Path

from jsonschema import Draft202012Validator

from graph import SCHEMA_VERSION, SIGNAL_HIGH, SIGNAL_LOW, SIGNAL_MEDIUM

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
# An extracted state space is what a boundary claim can be made concrete against.
BOUNDARY_STATE_SCORE = 5

TIER_REVIEW = "review"
TIER_CONSIDER = "consider"
TIER_CONTEXT = "context"
TIER_THRESHOLDS = ((TIER_REVIEW, 60), (TIER_CONSIDER, 35))

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
        """The changed spans, flattened to `{file, lines}` for the comment."""
        return [
            {"file": e["file"], "lines": [list(pair) for pair in e["lines"]]}
            for e in self.evidence
        ]


def resource_id(kind: str, name: str) -> str:
    return f"resource:{kind}:{name}"


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

    candidates: list[dict] = []
    dropped_noise = 0
    for interaction in interactions["interactions"]:
        rid = resource_id(interaction["resource_kind"], interaction["resource"])
        resource = nodes.get(rid)
        by_name = ids_by_resource.get(rid, {})

        hits: list[TouchedNode] = []
        for name in interaction["features"]:
            hit = next(
                (nodes[fid] for fid in by_name.get(name, ()) if fid in nodes), None
            )
            hits.append(hit)

        n_features = sum(1 for h in hits if h is not None)
        if resource is None and n_features == 0:
            continue
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
            why.append(
                f"{interaction['resource_kind']} `{interaction['resource']}` {where}"
            )
            if resource.new_levers:
                score += NEW_LEVER_SCORE
                levers = ", ".join(f"`{lever}`" for lever in resource.new_levers)
                why.append(
                    f"new lever {levers} on this resource — the diff changes the "
                    f"graph's own edge set"
                )
        for name, hit in zip(interaction["features"], hits, strict=True):
            if hit is None:
                continue
            score += FEATURE_MATCH_SCORE.get(hit.match, 0)
            why.append(
                f"`{name}` {'edited' if hit.match == 'span' else 'implementation edited'}"
            )
        if n_features == 2:
            why.append("both endpoints of the pair were edited")
        if interaction["boundary_states"]:
            score += BOUNDARY_STATE_SCORE

        evidence: list[dict] = []
        for node in (resource, *hits):
            if node is not None:
                evidence.extend(node.brief_evidence())

        candidates.append(
            {
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
            }
        )

    return _group(
        candidates,
        touched,
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


def _group(
    candidates: list[dict],
    touched: dict,
    *,
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
                "interactions": [],
            }
        group["score"] = max(group["score"], candidate["score"])
        if candidate["boundary_states"] and not group["boundary_states"]:
            group["boundary_states"] = candidate["boundary_states"]
        group["interactions"].append(
            {
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
        )

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
            "candidates": len(candidates),
            "selected": len(kept),
            "dropped_low_signal": dropped_noise,
            "truncated": len(candidates) - len(kept),
            "omitted_resources": omitted_resources,
            "by_tier": by_tier,
            "new_levers": sorted(
                {lever for e in touched["touched"] for lever in e["new_levers"]}
            ),
        },
        "groups": ordered,
        "caveats": _caveats(touched),
    }


def _caveats(touched: dict) -> list[str]:
    """Everything the reviewer must not read this report as having covered."""
    out: list[str] = []
    for entry in touched["off_span_changes"]:
        out.append(
            f"`{entry['file']}` changed outside every node span in it "
            f"({len(entry['hosts'])} node(s) live in that file). Not attributed "
            f"to any node, so no interaction below reflects it."
        )
    for entry in touched["structural_changes"]:
        out.append(
            f"`{entry['file']}` changed with no line content ({entry['kind']}), "
            f"hosting {len(entry['hosted'])} node(s). No span can match it."
        )
    unmapped = touched["summary"]["unmapped_files"]
    if unmapped:
        out.append(
            f"{unmapped} changed file(s) map to no graph node — including all of "
            f"`src/test`, `tx/paths/`, and most invariants. See the known limits "
            f"in `bin/interaction_review/README.md`."
        )
    out.append(
        "Test coverage is **not** checked yet: this names boundaries worth a "
        "look, not boundaries that are untested."
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
