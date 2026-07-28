#!/usr/bin/env python3
"""Map a PR diff onto graph nodes (Phase 1, Component B).

Reads a graph built by `build_graph.py` and a git diff, and reports which
feature and resource nodes the diff touches. This is the narrowing step every
later phase depends on: the graph holds thousands of interactions, and only the
ones reachable from the diff are worth a reviewer's attention.

A node is touched when a changed line falls inside one of its spans. Match
strength follows the precision of the span that was hit:

- `span` — a `macro` or `definition` span, which bounds the node exactly. The
  diff edited this node.
- `file` — only a whole-file `impl` span, which says the change is somewhere in
  a file belonging to this transactor and no more than that.

Changes landing in a file that hosts nodes but outside every span in it are
reported separately as `off_span_changes`, never attributed to a node. An edit
to a free helper in `Transactor.cpp` concerns the base pipeline, but it is not
evidence about any one of the two dozen forks that file happens to contain, and
attributing it to all of them would drown the signal it is meant to surface.

Changed lines of every touched node are scanned for lever tokens (`sf*`, `tf*`,
`feature*`, `fix*`), validated against the vocabulary the graph records. A token
a node does not already carry is the strongest available signal: on a fork it
means the diff is changing the graph's own edge set, and on a transactor it
means the implementation is consulting an amendment its node never declared.

This phase stops at touched nodes. Selecting and ranking the affected
interactions, and locating their tests, is the next step.
"""

from __future__ import annotations

import argparse
import json
from dataclasses import dataclass, field
from pathlib import Path

from jsonschema import Draft202012Validator

from diffparse import FileDiff, diff_ranges, merge_base, resolve_rev, subtract_ranges
from graph import LOC_IMPL, SCHEMA_VERSION

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parents[1]

MATCH_SPAN = "span"
MATCH_FILE = "file"

# Lever tokens, mirroring the prefixes fork_extractor keys on.
_LEVER_PREFIXES = ("sf", "tf", "feature", "fix")


def _lever_shaped(text: str) -> set[str]:
    """Identifiers that merely look like levers, before validation."""
    found: set[str] = set()
    for token in _identifiers(text):
        for prefix in _LEVER_PREFIXES:
            if token.startswith(prefix) and token[len(prefix) :][:1].isupper():
                found.add(token)
                break
    return found


def _lever_tokens(text: str, vocabulary: set[str] | None) -> set[str]:
    """Real lever tokens on a line of changed source.

    Shape alone is not enough. rippled is full of locals like
    `bool const fixEnabled = view.rules().enabled(fixCleanup3_2_0);` and
    `featureSAVEnabled`, and of SFields such as `sfBalance` that are not
    cross-cutting and so can never be a fork lever. Reporting those as levers —
    and worse, as levers *new* to the graph — would turn the strongest signal
    this tool emits into its noisiest. `vocabulary` is the set of names the
    graph actually knows (common SFields, transaction flags, amendment gates),
    carried in graph.json so there is one derived source of truth.
    """
    shaped = _lever_shaped(text)
    if vocabulary is None:
        return shaped
    return shaped & vocabulary


def _identifiers(text: str) -> list[str]:
    out: list[str] = []
    current: list[str] = []
    for ch in text:
        if ch.isalnum() or ch == "_":
            current.append(ch)
        elif current:
            out.append("".join(current))
            current = []
    if current:
        out.append("".join(current))
    return out


@dataclass
class TouchedNode:
    node: dict
    match: str
    evidence: list[dict] = field(default_factory=list)
    known_levers: set[str] = field(default_factory=set)
    new_levers: set[str] = field(default_factory=set)


def _known_levers(node: dict) -> set[str]:
    return {
        *node.get("lever_fields", ()),
        *node.get("lever_flags", ()),
        # Gates are stored as bare amendment names but appear in source as
        # `featureX` / `fixX`; both spellings count as known.
        *node.get("amendment_gates", ()),
        *(f"feature{g}" for g in node.get("amendment_gates", ())),
        *(f"fix{g}" for g in node.get("amendment_gates", ())),
    }


def graph_vocabulary(graph: dict) -> set[str] | None:
    """Every lever name the graph knows, or None if the graph predates the
    vocabulary block (in which case token shape is all we have)."""
    vocab = graph.get("vocabulary")
    if not vocab:
        return None
    return {name for names in vocab.values() for name in names}


def load_graph(path: Path, schema_path: Path | None = None) -> dict:
    """Read and validate a graph artifact.

    Validated against the same schema `build_graph` writes it with, rather than
    sniffed: a stale or hand-edited graph should fail here with a pointed
    message, not as a KeyError somewhere downstream.
    """
    graph = json.loads(Path(path).read_text())
    if not isinstance(graph, dict) or "features" not in graph:
        raise ValueError(f"{path} is not a graph document")
    version = graph.get("schema")
    if version != SCHEMA_VERSION:
        raise ValueError(
            f"{path} has graph schema version {version!r}, expected "
            f"{SCHEMA_VERSION}. Rebuild it with build_graph.py."
        )
    schema = json.loads(Path(schema_path or HERE / "graph.schema.json").read_text())
    errors = sorted(
        Draft202012Validator(schema).iter_errors(graph),
        key=lambda e: [str(p) for p in e.path],
    )
    if errors:
        msgs = "\n".join(f"  {list(e.path)}: {e.message}" for e in errors[:10])
        raise ValueError(f"{path} failed graph schema validation:\n{msgs}")
    return graph


def index_locations(graph: dict) -> dict[str, list[tuple[dict, dict]]]:
    """file -> [(node, location)], for every located node."""
    index: dict[str, list[tuple[dict, dict]]] = {}
    for node in [*graph["features"], *graph["resources"]]:
        for loc in node["locations"]:
            index.setdefault(loc["file"], []).append((node, loc))
    return index


def resolve_touched(
    graph: dict, diffs: dict[str, FileDiff]
) -> tuple[list[TouchedNode], list[str], list[dict], list[dict]]:
    """Touched nodes, unmapped files, off-span changes, and structural changes.

    Every changed file lands in exactly one of: unmapped, structural, or at
    least one of touched/off-span. `test_every_changed_file_is_accounted_for`
    pins that partition — a file falling through all of them is a silent drop,
    which is the failure mode this module exists to avoid.
    """
    index = index_locations(graph)
    vocabulary = graph_vocabulary(graph)
    touched: dict[str, TouchedNode] = {}
    unmapped: list[str] = []
    off_span: list[dict] = []
    structural: list[dict] = []

    for path, file_diff in sorted(diffs.items()):
        entries = index.get(path)
        if not entries:
            unmapped.append(path)
            continue

        if file_diff.deleted or not file_diff.ranges:
            # No new-side lines, so nothing can match a span. Two shapes reach
            # here: an outright deletion, and a change with no content diff at
            # all (a 100%-similarity rename, a mode change). Both must be
            # surfaced — reporting silence for a PR that removes Transactor.cpp,
            # and with it all 25 forks, would be the worst failure this tool has.
            structural.append(
                {
                    "file": path,
                    "kind": "deleted" if file_diff.deleted else "no_line_changes",
                    "hosted": sorted({n["id"] for n, _ in entries}),
                }
            )
            continue
        for node, loc in entries:
            # Clip to the span: a single contiguous hunk can straddle two
            # definitions and the gap between them, and lines outside this span
            # are not evidence about this node.
            hits = [
                (max(start, loc["start_line"]), min(end, loc["end_line"]))
                for start, end in file_diff.touches(loc["start_line"], loc["end_line"])
            ]
            if not hits:
                continue
            match = MATCH_FILE if loc["role"] == LOC_IMPL else MATCH_SPAN
            existing = touched.get(node["id"])
            if existing is None:
                existing = TouchedNode(node=node, match=match)
                touched[node["id"]] = existing
            # A precise hit anywhere upgrades the node's overall strength.
            if match == MATCH_SPAN:
                existing.match = MATCH_SPAN
            existing.evidence.append(
                {
                    "file": path,
                    "role": loc["role"],
                    "span": [loc["start_line"], loc["end_line"]],
                    "lines": [list(r) for r in hits],
                }
            )
            _attribute_levers(existing, node, file_diff, hits, vocabulary)

        uncovered = subtract_ranges(
            file_diff.ranges,
            [(loc["start_line"], loc["end_line"]) for _, loc in entries],
        )
        if uncovered:
            off_span.append(
                {
                    "file": path,
                    "lines": [list(r) for r in uncovered],
                    "hosts": sorted({node["id"] for node, _ in entries}),
                }
            )

    return _sorted_touched(touched.values()), unmapped, off_span, structural


def _attribute_levers(
    entry: TouchedNode,
    node: dict,
    file_diff: FileDiff,
    hits: list[tuple[int, int]],
    vocabulary: set[str] | None,
) -> None:
    """Split lever tokens on changed lines into ones this node already records
    and ones it does not.

    Run for every node kind, not just forks: most amendment gating lives in
    transactor implementations, and a diff adding `rules().enabled(featureX)` to
    Payment.cpp is exactly the cross-amendment coupling this tool exists to
    surface. For a node with no levers of its own, every token is "new".
    """
    known = _known_levers(node)
    for start, end in hits:
        for line_no in range(start, end + 1):
            text = file_diff.added_lines.get(line_no)
            if text is None:
                continue
            for token in _lever_tokens(text, vocabulary):
                if token in known:
                    entry.known_levers.add(token)
                else:
                    entry.new_levers.add(token)


def _sorted_touched(entries) -> list[TouchedNode]:
    """Span matches first, then by node id — a stable, reviewable order."""
    return sorted(entries, key=lambda e: (e.match != MATCH_SPAN, e.node["id"]))


def build_report(
    graph: dict,
    diffs: dict[str, FileDiff],
    base: str,
    head: str,
) -> dict:
    touched, unmapped, off_span, structural = resolve_touched(graph, diffs)

    by_kind: dict[str, int] = {}
    by_match: dict[str, int] = {}
    for entry in touched:
        kind = entry.node["kind"]
        by_kind[kind] = by_kind.get(kind, 0) + 1
        by_match[entry.match] = by_match.get(entry.match, 0) + 1

    return {
        "schema": 1,
        "base": base,
        "head": head,
        "summary": {
            "changed_files": len(diffs),
            "unmapped_files": len(unmapped),
            "touched_nodes": len(touched),
            "off_span_files": len(off_span),
            "structural_files": len(structural),
            "by_kind": by_kind,
            "by_match": by_match,
        },
        "touched": [
            {
                "id": e.node["id"],
                "kind": e.node["kind"],
                "name": e.node["name"],
                "match": e.match,
                "signal": e.node.get("signal"),
                "evidence": sorted(
                    e.evidence,
                    key=lambda ev: (
                        ev["file"],
                        ev["span"][0],
                        ev["span"][1],
                        ev["role"],
                    ),
                ),
                "known_levers": sorted(e.known_levers),
                "new_levers": sorted(e.new_levers),
            }
            for e in touched
        ],
        "off_span_changes": sorted(off_span, key=lambda o: o["file"]),
        "structural_changes": sorted(structural, key=lambda d: d["file"]),
        "unmapped_files": sorted(unmapped),
    }


def validate_report(report: dict, schema_path: Path) -> None:
    schema = json.loads(Path(schema_path).read_text())
    errors = sorted(
        Draft202012Validator(schema).iter_errors(report),
        key=lambda e: [str(p) for p in e.path],
    )
    if errors:
        msgs = "\n".join(f"  {list(e.path)}: {e.message}" for e in errors[:10])
        raise ValueError(f"touched.json failed schema validation:\n{msgs}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--graph",
        default=str(HERE / "out" / "graph.json"),
        help="graph.json built from the head revision",
    )
    parser.add_argument(
        "--base", default="develop", help="base branch or revision (default: develop)"
    )
    parser.add_argument(
        "--head",
        help="head revision (default: the working tree)",
    )
    parser.add_argument("--repo-root", default=str(REPO_ROOT))
    parser.add_argument(
        "--out", help="write the report here (default: alongside the graph)"
    )
    args = parser.parse_args(argv)

    repo_root = Path(args.repo_root).resolve()
    graph_path = Path(args.graph)
    if not graph_path.is_file():
        parser.error(f"graph not found: {graph_path}. Run build_graph.py first.")
    graph = load_graph(graph_path)

    diffs = diff_ranges(repo_root, args.base, args.head)
    report = build_report(
        graph,
        diffs,
        base=merge_base(repo_root, args.base, args.head or "HEAD"),
        head=resolve_rev(repo_root, args.head) if args.head else "worktree",
    )
    validate_report(report, HERE / "touched.schema.json")

    out_path = Path(args.out) if args.out else graph_path.parent / "touched.json"
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(report, indent=2))

    summary = report["summary"]
    print(
        f"changed files: {summary['changed_files']} "
        f"({summary['unmapped_files']} mapped to no node)"
    )
    print(f"touched nodes: {summary['touched_nodes']} {summary['by_kind']}")
    print(f"match strength: {summary['by_match']}")
    if summary["off_span_files"]:
        print(
            f"off-span changes in {summary['off_span_files']} file(s) hosting "
            f"nodes (not attributed to any node)"
        )
    if summary["structural_files"]:
        print(
            f"{summary['structural_files']} file(s) hosting nodes changed with no "
            f"line content (deleted, renamed, or mode-only)"
        )
    new_levers = sorted({t for e in report["touched"] for t in e["new_levers"]})
    if new_levers:
        print(f"levers not yet recorded on the node they appear in: {new_levers}")
    print(f"wrote {out_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
