#!/usr/bin/env python3
"""Render selected.json -- or judged.json -- as a concise advisory PR comment.

The workflow artifact retains the static candidate data. The PR comment only
shows conclusions produced by the model: one block per successful investigation,
with its evidence and full analysis. This keeps the review useful without asking
the reader to work through the intersection inventory that led to it.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from budget_order import budget_sort_key

HERE = Path(__file__).resolve().parent

# Lets the posting step find and update its own previous comment instead of
# adding one per push.
MARKER = "<!-- interaction-review -->"

DOCS = "bin/interaction_review/README.md"
# The posting workflow enforces GitHub's 65,536-byte body limit.
MAX_BODY = 60000

# A verdict from the second-opinion pass. Deliberately hedged wording: the
# static pass either found a shared spot or did not, but this one read code and
# formed an opinion, and the difference has to survive into the reader's head.
VERDICT_LABEL = {
    "gap": "Possible behavior gap",
    "coverage_gap": "Possible test coverage gap",
    "handled": "No issue found",
    "unclear": "Inconclusive",
}
CONFIDENCE_GLOSS = {
    "high": "high confidence",
    "medium": "medium confidence",
    "low": "low confidence",
}
BEHAVIOR_GLOSS = {
    "broken": "broken",
    "correct": "correct",
    "unclear": "unresolved",
}
COVERAGE_GLOSS = {
    "covered": "combination test found",
    "missing": "combination test missing",
    "unclear": "test coverage unresolved",
}


def _n(count: int, singular: str, plural: str | None = None) -> str:
    """`1 file` / `3 files`. "(s)" on every noun reads like a form letter."""
    return f"{count} {singular if count == 1 else (plural or singular + 's')}"


def _quote(text: str) -> list[str]:
    """Blockquote every line of a model-written string.

    The summary and detail are the only free text in this comment, and they
    arrive with newlines in them. Prefixing only the first line silently drops
    the rest out of the quote, so the verdict stops being visually separated
    from the report's own claims — the one distinction this comment most needs
    to keep. An empty line becomes a bare `>` so the quote survives it.
    """
    return [f"> {line}".rstrip() for line in text.splitlines() or [""]]


def _utf8_prefix(text: str, max_bytes: int) -> str:
    """Return at most ``max_bytes`` without splitting a UTF-8 code point."""
    return text.encode("utf-8")[:max_bytes].decode("utf-8", errors="ignore")


def _judgement_block(record: dict) -> list[str]:
    """Render one model conclusion with evidence and expandable analysis."""
    verdict = record["verdict"]
    location = _judgement_primary_location(record)
    who = (
        " × ".join(f"`{name}`" for name in record["features"])
        if record.get("kind", "interaction") == "interaction"
        else (f"`{location}` authorization rule" if location else "authorization rule")
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
    if record.get("behavior") and record.get("coverage"):
        lines += [
            ">",
            *_quote(
                f"Behavior: {BEHAVIOR_GLOSS.get(record['behavior'], record['behavior'])}. "
                f"Test coverage: "
                f"{COVERAGE_GLOSS.get(record['coverage'], record['coverage'])}."
            ),
        ]
    if record["states_unreached"]:
        states = ", ".join(f"`{s}`" for s in record["states_unreached"])
        lines += [
            ">",
            *_quote(f"Not established in this investigation: {states}."),
        ]
    if record["citations"]:
        cited = ", ".join(
            f"`{c['file']}:{c['line']}` ({c['what']})" for c in record["citations"][:4]
        )
        lines += [">", *_quote(f"Read: {cited}")]
    if location:
        lines += [">", *_quote(f"Primary location: `{location}`.")]
    if record["detail"] and record["detail"] != record["summary"]:
        lines += [
            ">",
            "> <details>",
            "> <summary>Full analysis</summary>",
            ">",
            *_quote(record["detail"]),
            ">",
            "> </details>",
        ]
    return lines


def _judgement_primary_location(record: dict) -> str | None:
    """Return the model-selected location, with a deterministic legacy fallback."""
    locations = record.get("locations", ())
    if locations:
        return _record_primary_location(record)["resource"]
    if record.get("resource"):
        return record["resource"]
    key = record.get("key")
    return key.split("|", 1)[0] if key and "|" in key else None


def _review_records(report: dict) -> list[dict]:
    """Return model records in judge rank order."""
    return [
        record
        for _, record in sorted(
            enumerate(report.get("judgements", ())),
            key=lambda entry: (entry[1].get("rank", float("inf")), entry[0]),
        )
    ]


def _cluster_location_sort_key(location: dict) -> tuple[int, int, str, str]:
    """Best tier and score first, with a deterministic tie-break."""
    return (
        *budget_sort_key(location),
        location["resource_kind"],
        location["resource"],
    )


def _record_primary_location(record: dict) -> dict:
    """Model-selected primary, with deterministic static fallback for legacy."""
    locations = sorted(record.get("locations", ()), key=_cluster_location_sort_key)
    requested = record.get("primary_location")
    return next(
        (
            location
            for location in locations
            if requested
            and location["resource_kind"] == requested.get("resource_kind")
            and location["resource"] == requested.get("resource")
        ),
        locations[0],
    )


def render(report: dict) -> str:
    """Render model comments only; keep the static inventory in the artifact."""
    records = _review_records(report)
    successful_records = [record for record in records if not record.get("error")]
    failed_judgements = sum(1 for record in records if record.get("error"))
    lines = [MARKER, "## Model review"]

    if successful_records:
        for record in successful_records:
            lines += _judgement_block(record)
    else:
        lines += ["", "No model comments are available for this change."]

    if failed_judgements:
        lines += [
            "",
            f"_{_n(failed_judgements, 'automated-review attempt')} failed before "
            f"producing a verdict._",
        ]

    dropped_citations = sum(
        len(record.get("dropped_citations") or []) for record in successful_records
    )
    if dropped_citations:
        lines += [
            "",
            f"_{_n(dropped_citations, 'model citation')} could not be verified and "
            f"{'was' if dropped_citations == 1 else 'were'} omitted._",
        ]

    base = report.get("base", "")
    compared = f" Compared against `{base[:12]}`." if base else ""
    if successful_records:
        caution = (
            "Experimental and advisory only; this does not block the PR. The "
            "model can be wrong, so verify the cited code before acting."
        )
    else:
        caution = "This status is advisory and does not block the PR."
    footer = (
        f"<sub>{caution}{compared} How it works: "
        f"[`{DOCS}`](../blob/develop/{DOCS}).</sub>"
    )
    content = "\n".join(lines)
    body = f"{content}\n\n{footer}\n"
    if len(body.encode("utf-8")) > MAX_BODY:
        suffix = (
            f"\n\n_Comment truncated at {MAX_BODY} bytes; the full report "
            f"is in the `interaction-review` workflow artifact._\n\n{footer}\n"
        )
        keep = _utf8_prefix(content, MAX_BODY - len(suffix.encode("utf-8"))).rsplit(
            "\n", 1
        )[0]
        body = f"{keep}{suffix}"
    return body


def _current_report_path(selected: Path, judged: Path) -> Path:
    """Prefer judged output only when it came from this exact selection.

    Local iteration commonly re-runs selection without re-running the expensive
    model pass. A leftover judged.json must not silently attach yesterday's
    verdicts to today's ranking. Older judged artifacts remain renderable when
    explicitly passed with `--selected`.
    """
    if not judged.is_file():
        return selected
    if not selected.is_file():
        return judged
    try:
        document = json.loads(judged.read_text())
        expected = document.get("judge", {}).get("selected_sha256")
        actual = hashlib.sha256(selected.read_bytes()).hexdigest()
    except (OSError, ValueError, TypeError):
        return selected
    return judged if expected == actual else selected


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--selected",
        help=(
            "Report to render. Defaults to out/judged.json only when its recorded "
            "selection hash matches out/selected.json; otherwise renders the "
            "current unjudged selection."
        ),
    )
    parser.add_argument("--out", help="write markdown here (default: stdout)")
    args = parser.parse_args(argv)

    if args.selected:
        path = Path(args.selected)
    else:
        judged = HERE / "out" / "judged.json"
        selected = HERE / "out" / "selected.json"
        path = _current_report_path(selected, judged)
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
