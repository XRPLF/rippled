#!/usr/bin/env python3
"""Render selected.json as the advisory PR comment.

Pure formatting: every claim in the output comes from `selected.json`, so the
comment can be regenerated from an artifact without re-running the extractors.

The comment is advisory. It never says a boundary is untested -- the test locator
does not exist yet -- only that a boundary is in scope and what its state space
is, so a reviewer can check the states themselves.

The reader is any rippled engineer opening their own PR, not someone who has read
DESIGN.md. So none of this tool's vocabulary reaches the page: `resource`,
`fork`, `mediator`, `consumer`, `node`, `lever`, `boundary`, `in scope` and
`signal` all get glossed into ordinary words at the point of rendering. The model
those terms name is still the model -- it just is not the reader's problem. Keep
new strings in the same register: plain, direct, and specific about what the
reader is being asked to do.
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
    "review": "🔴 worth checking",
    "consider": "🟡 maybe",
    "context": "⚪ background",
}
# How the two features relate to the code they share. "Changes it" is the
# mediator role, "uses it" the consumer role -- two rule-writers meeting at one
# decision is the combination that actually bites, so say so in words.
KIND_GLOSS = {
    "mediator×mediator": "both change how this code behaves",
    "mediator×consumer": "one changes how it behaves, the other just uses it",
    "consumer×consumer": "both just use it",
}
ROLE_GLOSS = {
    "mediator": "changes it",
    "consumer": "uses it",
    "wrapper": "wraps it",
}
# What kind of shared thing the two features met at.
RESOURCE_GLOSS = {
    "fork": "a shared decision",
    "invariant": "a shared invariant check",
    "shared_sfield": "a shared field",
}
# Whether the diff hit the shared code itself, or only something that uses it.
MATCH_GLOSS = {
    "span": "you changed it directly",
    "file": "you changed the file it lives in",
    None: "you did not change it, but you changed something that uses it",
}
# Stated once, at the top: the reason any of this is worth a reader's time.
PREMISE = (
    "Two features can each be correct on their own and still break in "
    "combination, and that can only happen where they touch the same code. "
    "Here is where this diff does that."
)
DOCS = "bin/interaction_review/README.md"
# GitHub rejects comment bodies over 65536 characters.
MAX_BODY = 60000


def _n(count: int, singular: str, plural: str | None = None) -> str:
    """`1 file` / `3 files`. "(s)" on every noun reads like a form letter."""
    return f"{count} {singular if count == 1 else (plural or singular + 's')}"


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
        "## Features that meet in this diff",
        "",
    ]
    if not report["groups"]:
        lines += [
            f"Nothing to flag — this diff does not touch any code that separate "
            f"features share ({_n(summary['changed_files'], 'changed file')}).",
        ]
        return lines

    counts = summary["by_tier"]
    tiers = ", ".join(
        f"{counts[tier]} {TIER_LABEL[tier]}"
        for tier in ("review", "consider", "context")
        if counts.get(tier)
    )
    # Only worth naming when there is more than one, or it just echoes the
    # count above it and every group already has its own heading below.
    spread = (
        f" across {_n(len(report['groups']), 'shared spot')}"
        if len(report["groups"]) > 1
        else ""
    )
    lines += [
        PREMISE,
        "",
        f"You touched **{_n(summary['touched_nodes'], 'piece')} of code that "
        f"features share**, in {_n(summary['changed_files'], 'changed file')}. "
        f"That brings **{_n(summary['selected'], 'feature pair')}** into view"
        f"{spread}: {tiers}.",
    ]
    if summary["new_levers"]:
        levers = ", ".join(f"`{lever}`" for lever in summary["new_levers"])
        lines += [
            "",
            f"⚠️ **This diff branches on {levers} somewhere it did not before.** "
            f"That changes *which* features meet here, not just how they behave "
            f"once they do — the strongest signal this tool has.",
        ]
    return lines


def _group_section(group: dict, show_omitted: bool) -> list[str]:
    hit = MATCH_GLOSS[group["resource_match"]]
    kind = RESOURCE_GLOSS.get(group["resource_kind"], group["resource_kind"])
    lines = [
        "",
        f"### `{group['resource']}` — {kind} ({hit})",
    ]
    if group["boundary_states"]:
        states = ", ".join(f"`{state}`" for state in group["boundary_states"])
        lines += [
            "",
            f"This code picks between {states}. A test that uses both features "
            f"but only ever reaches one of those has not really tested the "
            f"combination.",
        ]
    if group["new_levers"]:
        levers = ", ".join(f"`{lever}`" for lever in group["new_levers"])
        lines += ["", f"New here: this code now branches on {levers}."]

    lines += [
        "",
        "| features | how they meet | why it is here | where |",
        "| --- | --- | --- | --- |",
    ]
    for item in group["interactions"]:
        a, b = item["features"]
        roles = dict(zip(item["features"], item["roles"], strict=True))
        pair = (
            f"`{a}` ({ROLE_GLOSS.get(roles[a], roles[a])}) × "
            f"`{b}` ({ROLE_GLOSS.get(roles[b], roles[b])})"
        )
        how = f"{TIER_LABEL[item['tier']]}<br>{KIND_GLOSS.get(item['kind'], item['kind'])}"
        why = "<br>".join(item["why"])
        lines.append(
            f"| {pair} | {how} | {why} | {_short_evidence(item['evidence'])} |"
        )
    if group["omitted"] and show_omitted:
        lines += [
            "",
            f"_{_n(group['omitted'], 'more pair')} here, ranked lower and not shown._",
        ]
    return lines


def render(report: dict) -> str:
    """The full comment body."""
    summary = report["summary"]
    # With one group its per-group count and the overall count are the same
    # number, and printing both reads like two different omissions.
    per_group = len(report["groups"]) > 1
    lines = _header(report)
    for group in report["groups"]:
        lines += _group_section(group, show_omitted=per_group)

    if summary["truncated"]:
        extra = (
            f", including {_n(summary['omitted_resources'], 'other shared spot')}"
            if summary["omitted_resources"]
            else ""
        )
        lines += [
            "",
            f"_{summary['truncated']} of {_n(summary['candidates'], 'pair')} are "
            f"not shown, lowest-ranked first{extra}. Code this widely shared pairs "
            f"with nearly every transaction type, so most of that tail is "
            f"combinations rather than findings — what is above is the part with "
            f"something behind it._",
        ]
    if summary["dropped_low_signal"]:
        lines += [
            "",
            f"_{_n(summary['dropped_low_signal'], 'pair')} dropped as noise: they "
            f"only share a common field, and neither feature changed it._",
        ]

    lines += ["", "<details>", "<summary>What this misses</summary>", ""]
    lines += [f"- {caveat}" for caveat in report["caveats"]]
    lines += [
        "",
        "</details>",
        "",
        f"<sub>Advisory — this does not block the PR, and the ranking is a "
        f"heuristic rather than a verdict. Compared against `{report['base'][:12]}`. "
        f"How it works: [`{DOCS}`](../blob/develop/{DOCS}).</sub>",
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
