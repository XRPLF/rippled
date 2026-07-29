#!/usr/bin/env python3
"""Render selected.json as the advisory PR comment.

Pure formatting: every claim in the output comes from `selected.json`, so the
comment can be regenerated from an artifact without re-running the extractors.

The comment is advisory. It never says a boundary is untested -- the test locator
does not exist yet -- only that a boundary is in scope and what its state space
is, so a reviewer can check the states themselves.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parent.parent

# Lets the posting step find and update its own previous comment instead of
# adding one per push.
MARKER = "<!-- interaction-review -->"

TIER_LABEL = {
    "review": "🔴 review",
    "consider": "🟡 consider",
    "context": "⚪ context",
}
KIND_GLOSS = {
    "mediator×mediator": "both features change what this resource does",
    "mediator×consumer": "one feature changes the resource, the other rides on it",
    "consumer×consumer": "both features merely use the resource",
}
DOCS = "bin/interaction_review/README.md"
# GitHub rejects comment bodies over 65536 characters.
MAX_BODY = 60000


def _short_evidence(evidence: list[dict], limit: int = 3) -> str:
    """`file:line` references, deduplicated, capped."""
    seen: list[str] = []
    for entry in evidence:
        for start, end in entry["lines"]:
            ref = (
                f"`{entry['file']}:{start}`"
                if start == end
                else (f"`{entry['file']}:{start}-{end}`")
            )
            if ref not in seen:
                seen.append(ref)
    if len(seen) > limit:
        return ", ".join(seen[:limit]) + f", +{len(seen) - limit} more"
    return ", ".join(seen)


def _header(report: dict) -> list[str]:
    summary = report["summary"]
    lines = [
        MARKER,
        "## Feature-interaction review (advisory)",
        "",
    ]
    if not report["groups"]:
        lines += [
            f"No feature-interaction boundaries are in scope for this diff "
            f"({summary['changed_files']} changed file(s), "
            f"{summary['touched_nodes']} graph node(s) touched).",
        ]
        return lines

    counts = summary["by_tier"]
    tiers = ", ".join(
        f"{counts[tier]} {TIER_LABEL[tier]}"
        for tier in ("review", "consider", "context")
        if counts.get(tier)
    )
    lines += [
        f"This diff touches **{summary['touched_nodes']} graph node(s)** across "
        f"{summary['changed_files']} changed file(s), putting "
        f"**{summary['selected']} feature interaction(s)** in scope on "
        f"**{len(report['groups'])} shared resource(s)**: {tiers}.",
    ]
    if summary["new_levers"]:
        levers = ", ".join(f"`{lever}`" for lever in summary["new_levers"])
        lines += [
            "",
            f"⚠️ **New lever(s) in this diff: {levers}.** The diff adds a branch on "
            f"a cross-cutting field, flag, or amendment gate that the graph has "
            f"not seen before, which means it changes the interaction edge set "
            f"itself — not just one path through it.",
        ]
    return lines


def _group_section(group: dict) -> list[str]:
    hit = {
        "span": "changed in this diff",
        "file": "changed in this diff (whole-file attribution)",
        None: "not itself changed; reached through a changed feature",
    }[group["resource_match"]]
    lines = [
        "",
        f"### {group['resource_kind']} `{group['resource']}` "
        f"({group['signal']} signal — {hit})",
    ]
    if group["boundary_states"]:
        states = ", ".join(f"`{state}`" for state in group["boundary_states"])
        lines += [
            "",
            f"Boundary states: {states}. A test that exercises both features but "
            f"drives only one of these states does not cover the boundary.",
        ]
    if group["new_levers"]:
        levers = ", ".join(f"`{lever}`" for lever in group["new_levers"])
        lines += ["", f"New lever(s) on this resource: {levers}."]

    lines += [
        "",
        "| pair | interaction | why in scope | evidence |",
        "| --- | --- | --- | --- |",
    ]
    for item in group["interactions"]:
        a, b = item["features"]
        roles = dict(zip(item["features"], item["roles"], strict=True))
        pair = f"`{a}` ({roles[a]}) × `{b}` ({roles[b]})"
        kind = f"{TIER_LABEL[item['tier']]}<br>{KIND_GLOSS.get(item['kind'], item['kind'])}"
        why = "<br>".join(item["why"])
        lines.append(
            f"| {pair} | {kind} | {why} | {_short_evidence(item['evidence'])} |"
        )
    if group["omitted"]:
        lines += [
            "",
            f"_{group['omitted']} further pair(s) on this resource omitted by the "
            f"per-resource cap._",
        ]
    return lines


def render(report: dict) -> str:
    """The full comment body."""
    lines = _header(report)
    for group in report["groups"]:
        lines += _group_section(group)

    summary = report["summary"]
    if summary["truncated"]:
        extra = (
            f", including {summary['omitted_resources']} further shared resource(s)"
            if summary["omitted_resources"]
            else ""
        )
        lines += [
            "",
            f"_{summary['truncated']} of {summary['candidates']} candidate "
            f"interaction(s) omitted by the output caps, lowest-scoring first"
            f"{extra}._",
        ]
    if summary["dropped_low_signal"]:
        lines += [
            "",
            f"_{summary['dropped_low_signal']} consumer×consumer pair(s) sharing "
            f"only a low-signal SField were dropped as noise._",
        ]

    lines += ["", "<details>", "<summary>What this does not cover</summary>", ""]
    lines += [f"- {caveat}" for caveat in report["caveats"]]
    lines += [
        "",
        "</details>",
        "",
        f"<sub>Advisory only — nothing here blocks the PR. Ranking is heuristic. "
        f"Base `{report['base'][:12]}`. See [`{DOCS}`](../blob/develop/{DOCS}).</sub>",
    ]

    body = "\n".join(lines) + "\n"
    if len(body) > MAX_BODY:
        keep = body[:MAX_BODY].rsplit("\n", 1)[0]
        body = (
            f"{keep}\n\n_Comment truncated at {MAX_BODY} characters; the full "
            f"report is in the `interaction-review` workflow artifact._\n"
        )
    return body


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--selected", default=str(HERE / "out" / "selected.json"))
    parser.add_argument("--out", help="write markdown here (default: stdout)")
    args = parser.parse_args(argv)

    path = Path(args.selected)
    if not path.is_file():
        parser.error(f"--selected not found: {path}. Run select_interactions.py first.")
    body = render(json.loads(path.read_text()))

    if args.out:
        out_path = Path(args.out)
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text(body)
        print(f"wrote {out_path} ({len(body)} chars)")
    else:
        print(body, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
