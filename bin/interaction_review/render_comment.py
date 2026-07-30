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
    "invariant": "an authorization check",
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


def _code_list(names: list[str]) -> str:
    quoted = [f"`{name}`" for name in names]
    if len(quoted) < 2:
        return "".join(quoted)
    if len(quoted) == 2:
        return " and ".join(quoted)
    return ", ".join(quoted[:-1]) + f", and {quoted[-1]}"


def _interaction_key(item: dict) -> tuple:
    """Identity of a feature relationship independent of shared location."""
    endpoints = tuple(sorted(zip(item["features"], item["roles"], strict=True)))
    return (item["kind"], endpoints)


def _recurring_spots(report: dict) -> dict[tuple, list[str]]:
    """Feature relationships selected at more than one shared location."""
    spots: dict[tuple, list[str]] = {}
    for group in report["groups"]:
        if group["resource_kind"] == "invariant":
            continue
        for item in group["interactions"]:
            key = _interaction_key(item)
            names = spots.setdefault(key, [])
            if group["resource"] not in names:
                names.append(group["resource"])
    return {key: names for key, names in spots.items() if len(names) > 1}


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

    counts: dict[str, int] = {}
    display_items = 0
    has_authorization_check = False
    seen: set[tuple] = set()
    for group in report["groups"]:
        if group["resource_kind"] == "invariant":
            has_authorization_check = True
            if group["interactions"]:
                tier = group["interactions"][0]["tier"]
                counts[tier] = counts.get(tier, 0) + 1
                display_items += 1
            continue
        for item in group["interactions"]:
            key = _interaction_key(item)
            if key in seen:
                continue
            seen.add(key)
            tier = item["tier"]
            counts[tier] = counts.get(tier, 0) + 1
            display_items += 1
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
    lines += [PREMISE, ""]
    if display_items:
        item_name = "review item" if has_authorization_check else "feature pair"
        lines += [
            f"You touched **{_n(summary['touched_nodes'], 'piece')} of code that "
            f"features share**, in {_n(summary['changed_files'], 'changed file')}. "
            f"That brings **{_n(display_items, item_name)}** into view"
            f"{spread}: {tiers}.",
        ]
    else:
        lines += [
            f"You touched **{_n(summary['touched_nodes'], 'piece')} of code that "
            f"features share**, in {_n(summary['changed_files'], 'changed file')}. "
            f"The transaction types that pass through it are summarized below.",
        ]
    if summary["new_levers"]:
        levers = ", ".join(f"`{lever}`" for lever in summary["new_levers"])
        lines += [
            "",
            # Two different things produce this, and the banner is global, so it
            # cannot tell them apart: on a shared decision the diff added a
            # branch, on a transaction type the implementation reads an
            # amendment its declaration never mentions (pr_map.py). Claiming the
            # first when it was the second is an overclaim a reader catches by
            # opening the diff, so say what is actually known.
            f"⚠️ **The code you touched reads {levers}, which is not part of "
            f"what it declares.** Either this diff started branching on it, or "
            f"the code was already consulting an amendment nothing declares. "
            f"Either way, this is about *which* features meet here, not just "
            f"how they behave once they do.",
        ]
    return lines


def _group_section(
    group: dict,
    show_omitted: bool,
    recurring: dict[tuple, list[str]],
    rendered: set[tuple],
) -> list[str]:
    hit = MATCH_GLOSS[group["resource_match"]]
    kind = RESOURCE_GLOSS.get(group["resource_kind"], group["resource_kind"])
    visible: list[dict] = []
    if group["resource_kind"] != "invariant":
        for item in group["interactions"]:
            key = _interaction_key(item)
            if key in rendered:
                continue
            rendered.add(key)
            visible.append(item)
        if not visible and not group["consumer_cohort"]:
            return []

    lines = [
        "",
        f"### `{group['resource']}` — {kind} ({hit})",
    ]
    if group["resource_kind"] == "invariant":
        authorized = group["authorized_features"]
        evidence = [
            entry
            for item in group["interactions"]
            for entry in item["evidence"]
        ]
        lines += [
            "",
            f"The transaction declarations grant this permission to "
            f"{_code_list(authorized)}. Every other transaction type takes the "
            f"protected branch.",
            "",
            f"Check both sides of that rule: a listed transaction may make the "
            f"protected change, while an unlisted transaction must fail if it "
            f"does.",
        ]
        where = _short_evidence(evidence)
        if where:
            lines += ["", f"Changed here: {where}."]
        return lines

    if group["boundary_states"]:
        states = ", ".join(f"`{state}`" for state in group["boundary_states"])
        lines += [
            "",
            f"This code picks between {states}. A test that uses both features "
            f"but only ever reaches one of those has not really tested the "
            f"combination.",
        ]
    cohort = group["consumer_cohort"]
    if cohort:
        lines += [
            "",
            f"This decision is part of the common transaction path: "
            f"**{_n(len(cohort['consumers']), 'transaction type')}** "
            f"{'passes' if len(cohort['consumers']) == 1 else 'pass'} through it. "
            f"The features that change the decision are "
            f"{_code_list(cohort['mediators'])}. Their "
            f"{_n(cohort['pair_count'], 'feature-by-transaction combination')} "
            f"{'is' if cohort['pair_count'] == 1 else 'are'} summarized here "
            f"because every transaction type has the same "
            f"pass-through relationship.",
        ]
    if group["new_levers"]:
        levers = ", ".join(f"`{lever}`" for lever in group["new_levers"])
        lines += ["", f"New here: this code now branches on {levers}."]

    if not group["interactions"]:
        return lines

    lines += [
        "",
        "| features | how they meet | why it is here | where |",
        "| --- | --- | --- | --- |",
    ]
    for item in visible:
        a, b = item["features"]
        roles = dict(zip(item["features"], item["roles"], strict=True))
        pair = (
            f"`{a}` ({ROLE_GLOSS.get(roles[a], roles[a])}) × "
            f"`{b}` ({ROLE_GLOSS.get(roles[b], roles[b])})"
        )
        how = f"{TIER_LABEL[item['tier']]}<br>{KIND_GLOSS.get(item['kind'], item['kind'])}"
        why_parts = list(item["why"])
        other_spots = [
            spot
            for spot in recurring.get(_interaction_key(item), ())
            if spot != group["resource"]
        ]
        if other_spots:
            why_parts.append(
                f"Same pair also reaches {_code_list(other_spots)}"
            )
        why = "<br>".join(why_parts)
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
    recurring = _recurring_spots(report)
    rendered: set[tuple] = set()
    lines = _header(report)
    for group in report["groups"]:
        lines += _group_section(
            group,
            show_omitted=per_group,
            recurring=recurring,
            rendered=rendered,
        )

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
