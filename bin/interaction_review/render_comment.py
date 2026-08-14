#!/usr/bin/env python3
"""Render selected.json -- or judged.json -- as the advisory PR comment.

Pure formatting: every claim in the output comes from the report, so the
comment can be regenerated from an artifact without re-running the extractors.

Two kinds of claim can appear, and they are not equally strong. The static pass
only ever says a boundary is *in scope* and what its state space is, so a
reviewer can check the states themselves. When `judge_interactions.py` has also
run, a subset of rows additionally carries a verdict from a model that read the
code and the tests. Those are rendered as what they are -- a second opinion that
cites its evidence and can be wrong -- never as a result of the static pass.

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

# A verdict from the second-opinion pass. Deliberately hedged wording: the
# static pass either found a shared spot or did not, but this one read code and
# formed an opinion, and the difference has to survive into the reader's head.
VERDICT_LABEL = {
    "gap": "🔴 found a possible gap",
    "handled": "🟢 looks covered",
    "unclear": "⚪ could not tell",
}
VERDICT_BADGE = {"gap": "🔴 gap?", "handled": "🟢 covered?", "unclear": "⚪ unresolved"}
CONFIDENCE_GLOSS = {
    "high": "confident",
    "medium": "fairly sure",
    "low": "not sure",
}


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


def _plain_list(parts: list[str]) -> str:
    if len(parts) < 2:
        return "".join(parts)
    if len(parts) == 2:
        return " and ".join(parts)
    return ", ".join(parts[:-1]) + f", and {parts[-1]}"


def _role_description(item: dict, index: int) -> str:
    if "wrapper" in item["vias"][index]:
        return "wraps transactions through it"
    role = item["roles"][index]
    return ROLE_GLOSS.get(role, role)


def interaction_identity(item: dict) -> tuple:
    """Identity of a feature relationship independent of shared location.

    Public because `judge_interactions.plan` has to pick exactly the rows this
    renders: a pair reaching several shared spots is deduped down to one row at
    the first spot, and a verdict judged against any other set of rows would
    have nowhere to land.
    """
    endpoints = tuple(sorted(zip(item["features"], item["roles"], strict=True)))
    return (item["kind"], endpoints)


def judgement_key(resource: str, item: dict | None) -> str:
    """Stable identity for a judged row, shared with `judge_interactions`.

    Location-qualified where `interaction_identity` is not: the verdict is about
    this pair *at this shared spot*, and the same pair elsewhere is a different
    question. `item` of None keys an invariant group, which is judged whole.
    """
    if item is None:
        return f"{resource}|invariant"
    kind, endpoints = interaction_identity(item)
    pair = ",".join(f"{name}:{role}" for name, role in endpoints)
    return f"{resource}|{kind}|{pair}"


def _checked_summary(judgements: dict[str, dict]) -> list[str]:
    """One paragraph, near the top: what the second opinion looked at and found.

    Says up front that it is experimental and can be wrong. A reader who trusts
    these rows more than they deserve is a worse outcome than one who ignores
    them, because the cost of a false lead is a reviewer's afternoon.
    """
    counts: dict[str, int] = {}
    for record in judgements.values():
        counts[record["verdict"]] = counts.get(record["verdict"], 0) + 1
    gaps = counts.get("gap", 0)
    found = (
        f"**{_n(gaps, 'possible gap')}** flagged below"
        if gaps
        else "nothing conclusive"
    )
    return [
        "",
        f"🧪 _Experimental:_ **{_n(len(judgements), 'of these')}** "
        f"{'was' if len(judgements) == 1 else 'were'} also read by an automated "
        f"reviewer, which opened the shared code and looked for a test covering "
        f"the combination — {found}. It cites what it read so you can check it "
        f"in a few seconds, and it is wrong often enough that you should.",
    ]


def _quote(text: str) -> list[str]:
    """Blockquote every line of a model-written string.

    The summary and detail are the only free text in this comment, and they
    arrive with newlines in them. Prefixing only the first line silently drops
    the rest out of the quote, so the verdict stops being visually separated
    from the report's own claims — the one distinction this comment most needs
    to keep. An empty line becomes a bare `>` so the quote survives it.
    """
    return [f"> {line}".rstrip() for line in text.splitlines() or [""]]


def _judgement_block(record: dict) -> list[str]:
    """One verdict, rendered under the row it belongs to.

    Below the table rather than inside it: a finding needs a sentence and its
    evidence, and a fifth column of prose makes every other column unreadable.
    """
    verdict = record["verdict"]
    who = (
        " × ".join(f"`{name}`" for name in record["features"])
        if record["kind"] == "interaction"
        else "this permission"
    )
    lines = [
        "",
        *_quote(
            f"**{VERDICT_LABEL.get(verdict, verdict)}** — {who} "
            f"({CONFIDENCE_GLOSS.get(record['confidence'], record['confidence'])})"
        ),
        ">",
        *_quote(record["summary"]),
    ]
    if record["detail"] and record["detail"] != record["summary"]:
        lines.append(">")
        lines += _quote(record["detail"])
    if record["states_unreached"]:
        states = ", ".join(f"`{s}`" for s in record["states_unreached"])
        lines += [
            ">",
            *_quote(f"No evidence anything reaches {states} with both active."),
        ]
    if record["citations"]:
        cited = ", ".join(
            f"`{c['file']}:{c['line']}` ({c['what']})" for c in record["citations"][:4]
        )
        lines += [">", *_quote(f"Read: {cited}")]
    return lines


def _recurring_spots(report: dict) -> dict[tuple, list[str]]:
    """Feature relationships selected at more than one shared location."""
    spots: dict[tuple, list[str]] = {}
    for group in report["groups"]:
        if group["resource_kind"] == "invariant":
            continue
        for item in group["interactions"]:
            key = interaction_identity(item)
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
            key = interaction_identity(item)
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
            f"⚠️ **The changed code contains {levers}, but that dependency is "
            f"not recorded for the code this diff was mapped to.** It may be "
            f"new in this diff, or it may have already existed without being "
            f"declared. Either way, this is about *which* features meet here, "
            f"not just how they behave once they do.",
        ]
    return lines


def _group_section(
    group: dict,
    show_omitted: bool,
    recurring: dict[tuple, list[str]],
    rendered: set[tuple],
    judgements: dict[str, dict],
) -> list[str]:
    hit = MATCH_GLOSS[group["resource_match"]]
    kind = RESOURCE_GLOSS.get(group["resource_kind"], group["resource_kind"])
    visible: list[dict] = []
    if group["resource_kind"] != "invariant":
        for item in group["interactions"]:
            key = interaction_identity(item)
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
            entry for item in group["interactions"] for entry in item["evidence"]
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
        checked = judgements.get(judgement_key(group["resource"], None))
        if checked:
            lines += _judgement_block(checked)
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
        wrappers = set(cohort["wrappers"])
        changing_side = _plain_list(
            [
                (
                    f"`{name}` (wraps transactions through it)"
                    if name in wrappers
                    else f"`{name}` (changes it)"
                )
                for name in cohort["mediators"]
            ]
        )
        lines += [
            "",
            f"This decision is part of the common transaction path: "
            f"**{_n(len(cohort['consumers']), 'transaction type')}** "
            f"{'passes' if len(cohort['consumers']) == 1 else 'pass'} through it. "
            f"The other side is {changing_side}. Their "
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
        pair = (
            f"`{a}` ({_role_description(item, 0)}) × "
            f"`{b}` ({_role_description(item, 1)})"
        )
        checked = judgements.get(judgement_key(group["resource"], item))
        how = f"{TIER_LABEL[item['tier']]}<br>{KIND_GLOSS.get(item['kind'], item['kind'])}"
        if checked:
            how += f"<br>{VERDICT_BADGE.get(checked['verdict'], checked['verdict'])}"
        why_parts = list(item["why"])
        other_spots = [
            spot
            for spot in recurring.get(interaction_identity(item), ())
            if spot != group["resource"]
        ]
        if other_spots:
            why_parts.append(f"Same pair also reaches {_code_list(other_spots)}")
        why = "<br>".join(why_parts)
        lines.append(
            f"| {pair} | {how} | {why} | {_short_evidence(item['evidence'])} |"
        )
    # Findings come after the whole table, so the table stays scannable and a
    # reader who only wants the list of pairs never has to scroll past prose.
    for item in visible:
        checked = judgements.get(judgement_key(group["resource"], item))
        if checked:
            lines += _judgement_block(checked)
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
    # Absent when only the static pass ran, which is the normal case on a fork
    # PR or wherever the second opinion is not configured. Everything below
    # degrades to the unjudged comment rather than branching on a flag.
    judgements = {record["key"]: record for record in report.get("judgements", ())}
    lines = _header(report)
    if judgements and report["groups"]:
        lines += _checked_summary(judgements)
    for group in report["groups"]:
        lines += _group_section(
            group,
            show_omitted=per_group,
            recurring=recurring,
            rendered=rendered,
            judgements=judgements,
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
            f"not shown, lowest-ranked first{extra}. Widely shared code pairs "
            f"with nearly every transaction type, so most of that tail is "
            f"combinations rather than findings — the rows above are the "
            f"highest-ranked relationships._",
        ]
    if summary["dropped_low_signal"]:
        lines += [
            "",
            f"_{_n(summary['dropped_low_signal'], 'pair')} dropped as noise: they "
            f"only share a common field, and neither feature changed it._",
        ]

    lines += ["", "<details>", "<summary>What this misses</summary>", ""]
    caveats = list(report["caveats"])
    if judgements:
        judge = report.get("judge", {})
        unresolved = sum(
            len(record.get("dropped_citations") or []) for record in judgements.values()
        )
        caveats += [
            f"Only the {_n(judge.get('items', len(judgements)), 'highest-ranked row')} "
            f"{'was' if judge.get('items', len(judgements)) == 1 else 'were'} read by "
            f"the automated reviewer; the rest carry no verdict either way, and a row "
            f"with no verdict is not a row that passed.",
            "Each row was judged on its own, so nothing here reasons about two "
            "findings together.",
            "A verdict is one model's reading of the code under a time limit. It can "
            "misread control flow, miss a test that covers the case indirectly, and "
            "claim more certainty than it has.",
        ]
        if unresolved:
            caveats.append(
                f"{_n(unresolved, 'citation')} pointed at lines that do not exist and "
                f"{'was' if unresolved == 1 else 'were'} discarded before this was "
                f"written — a sign to weigh the surviving verdicts more carefully."
            )
    lines += [f"- {caveat}" for caveat in caveats]
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
    parser.add_argument(
        "--selected",
        help=(
            "Report to render. Defaults to out/judged.json when it exists, so a "
            "run that judged renders its verdicts without the caller tracking "
            "which stages ran; otherwise out/selected.json."
        ),
    )
    parser.add_argument("--out", help="write markdown here (default: stdout)")
    args = parser.parse_args(argv)

    if args.selected:
        path = Path(args.selected)
    else:
        judged = HERE / "out" / "judged.json"
        path = judged if judged.is_file() else HERE / "out" / "selected.json"
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
