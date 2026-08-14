#!/usr/bin/env python3
"""Run one bounded, tool-using Claude judgement against a source snapshot.

Static analysis chooses the feature pair and shared locations. This module lets
the model inspect their code and tests through three read-only tools, then
validates the structured verdict locally. The explicit message loop uses the
stable Bedrock Messages API and keeps tool use, caching, and turn limits visible.
"""

from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass, field

from jsonschema import Draft202012Validator

from evidence_trace import EvidenceTrace
from source_snapshot import SourceSnapshot

# Bedrock model IDs take the `anthropic.` provider prefix; the bare first-party
# ID 400s. Opus-tier because this is bug-finding in unfamiliar C++, which is
# exactly where the capability difference shows up.
DEFAULT_MODEL = "anthropic.claude-opus-5"
# Effort drives thinking depth and how hard the model works the tools. `xhigh`
# is the recommended setting for coding and agentic work; it is also the
# expensive one, hence the caps in judge_interactions.py rather than here.
DEFAULT_EFFORT = "xhigh"
# At xhigh the model needs room to think *and* act across tool calls. max_tokens
# caps thinking plus text together, so a tight budget truncates mid-judgement.
DEFAULT_MAX_TOKENS = 32000
# Model turns. A turn may contain several parallel read/grep calls, so this is
# not a total tool-call budget; the run metadata reports the observed turns.
# Clustered investigations need enough room to inspect several connected
# locations and their combination tests. The first live historical replay
# required 18 turns, including one schema-correction turn, so keep a small
# margin while retaining a hard per-investigation bound.
DEFAULT_MAX_ITERATIONS = 20

# Adaptive thinking and `output_config.effort` arrived with the 4.6 generation.
# Sending either to an older model -- Haiku 4.5 is the one anybody actually
# reaches for here, to try the pipeline cheaply -- is a 400, so the request is
# shaped from the model instead of assuming the newest surface. Matched on
# substrings because Bedrock IDs carry a provider prefix and sometimes a version
# suffix around the family name.
_ADAPTIVE_THINKING_MODELS = (
    "fable-5",
    "mythos-5",
    "opus-5",
    "opus-4-6",
    "opus-4-7",
    "opus-4-8",
    "sonnet-5",
    "sonnet-4-6",
)

# How many turns before the cap to start telling the model to wrap up. Two is
# enough to write a verdict and one over-run.
_WRAP_UP_TURNS = 2

# Tool output is untrusted PR content on its way into the context window, and
# an unbounded `grep` on this repo can return megabytes. Bound both.
_MAX_TOOL_RESULT_CHARS = 24000
_MAX_READ_LINES = 400
_MAX_GREP_MATCHES = 60

VERDICT_HANDLED = "handled"
VERDICT_GAP = "gap"
VERDICT_COVERAGE_GAP = "coverage_gap"
VERDICT_UNCLEAR = "unclear"

BEHAVIOR_BROKEN = "broken"
BEHAVIOR_CORRECT = "correct"
BEHAVIOR_UNCLEAR = "unclear"

COVERAGE_COVERED = "covered"
COVERAGE_MISSING = "missing"
COVERAGE_UNCLEAR = "unclear"

LOCATION_DECISIVE = "decisive"
LOCATION_SUPPORTING = "supporting"
LOCATION_NOT_RELEVANT = "not_relevant"
LOCATION_UNRESOLVED = "unresolved"

# The verdict's shape. Enforced locally by `_validate_verdict` rather than by
# `strict` on the tool (see SUBMIT_TOOL), and kept to types and enums so it stays
# usable as a structured-output schema if `strict` becomes available. The
# semantic checks live in judge_interactions.verify_citations, which can also
# see the filesystem.
VERDICT_SCHEMA = {
    "type": "object",
    "additionalProperties": False,
    "required": [
        "behavior",
        "coverage",
        "confidence",
        "summary",
        "detail",
        "primary_location",
        "location_assessments",
        "states_reached",
        "states_unreached",
        "citations",
    ],
    "properties": {
        "behavior": {
            "type": "string",
            "enum": [BEHAVIOR_BROKEN, BEHAVIOR_CORRECT, BEHAVIOR_UNCLEAR],
            "description": (
                "broken: a specific reachable combination is mishandled and has "
                "a concrete consequence. correct: every relevant reachable state "
                "you traced is handled correctly. unclear: the behavior could not "
                "be established from what you read. Default to unclear."
            ),
        },
        "coverage": {
            "type": "string",
            "enum": [COVERAGE_COVERED, COVERAGE_MISSING, COVERAGE_UNCLEAR],
            "description": (
                "covered: a test exercises the relevant combination and asserts "
                "its outcome. missing: you searched the relevant tests and found "
                "no such case. unclear: you did not search enough to decide. "
                "Coverage is independent of whether the code looks correct."
            ),
        },
        "confidence": {"type": "string", "enum": ["high", "medium", "low"]},
        # The length floors are not padding rules. A real run produced the
        # summary "Test" at high confidence off a single citation -- a verdict
        # shaped correctly and carrying nothing, which is the one failure a
        # reviewer cannot catch by reading. Enforced here because validating
        # locally means the schema is not limited to what structured outputs
        # accept, which rejects minLength.
        "summary": {
            "type": "string",
            "minLength": 40,
            "description": (
                "One sentence a reviewer can act on, naming the code involved. "
                "No preamble."
            ),
        },
        "detail": {
            "type": "string",
            "minLength": 80,
            "description": (
                "For broken behavior: the concrete sequence that reaches the "
                "mishandled state and what goes wrong. For correct behavior: the "
                "states and control flow you checked. Separately name the test "
                "that covers the combination or what coverage is missing."
            ),
        },
        "primary_location": {
            "description": (
                "The listed shared location that most directly establishes the "
                "overall verdict, or null when no location can be chosen."
            ),
            "oneOf": [
                {"type": "null"},
                {
                    "type": "object",
                    "additionalProperties": False,
                    "required": ["resource_kind", "resource"],
                    "properties": {
                        "resource_kind": {
                            "type": "string",
                            "enum": ["fork", "invariant", "shared_sfield"],
                        },
                        "resource": {"type": "string"},
                    },
                },
            ],
        },
        "location_assessments": {
            "type": "array",
            "minItems": 1,
            "description": (
                "One entry for every shared location in the investigation packet, "
                "stating its actual role in the consolidated conclusion."
            ),
            "items": {
                "type": "object",
                "additionalProperties": False,
                "required": ["resource_kind", "resource", "role", "what"],
                "properties": {
                    "resource_kind": {
                        "type": "string",
                        "enum": ["fork", "invariant", "shared_sfield"],
                    },
                    "resource": {"type": "string"},
                    "role": {
                        "type": "string",
                        "enum": [
                            LOCATION_DECISIVE,
                            LOCATION_SUPPORTING,
                            LOCATION_NOT_RELEVANT,
                            LOCATION_UNRESOLVED,
                        ],
                    },
                    "what": {
                        "type": "string",
                        "minLength": 12,
                        "description": "Why this location has that role.",
                    },
                },
            },
        },
        "states_reached": {
            "type": "array",
            "items": {"type": "string"},
            "description": (
                "Boundary states you found evidence of being exercised with "
                "both features active. Empty when the pair has no state space "
                "or you found none."
            ),
        },
        "states_unreached": {
            "type": "array",
            "items": {"type": "string"},
            "description": (
                "Boundary states you traced to an explicit guard and therefore "
                "concluded cannot occur in the scoped configuration. Do not put "
                "reachable-but-untested or merely unresolved states here; discuss "
                "those in detail instead."
            ),
        },
        "citations": {
            "type": "array",
            "description": (
                "Every file:line you are relying on. Any conclusive behavior "
                "or coverage classification requires at least one citation; a "
                "result whose citations do not resolve is discarded."
            ),
            "items": {
                "type": "object",
                "additionalProperties": False,
                "required": ["file", "line", "what"],
                "properties": {
                    "file": {
                        "type": "string",
                        "description": "Repo-relative path, as the tools report it.",
                    },
                    "line": {"type": "integer"},
                    "what": {
                        "type": "string",
                        "description": "What this line shows, in a few words.",
                    },
                },
            },
        },
    },
}

SUBMIT_TOOL = {
    "name": "submit_verdict",
    "description": (
        "Record your judgement and end the investigation. Call this exactly "
        "once, when you have read enough to answer or have concluded you "
        "cannot."
    ),
    # No `strict: true`. The Bedrock Messages endpoint rejects the field outright
    # ("tools.N.custom.strict: Extra inputs are not permitted"), and a guarantee
    # that depends on which endpoint you happen to be pointed at is not one.
    # `_validate_verdict` enforces the same schema here instead, and hands a
    # violation back to the model as a tool error so it can correct itself --
    # which also covers the models where `strict` was never available.
    "input_schema": VERDICT_SCHEMA,
}

READ_TOOL = {
    "name": "read_file",
    "description": (
        "Read a slice of a repo-relative file. Prefer a range around a line you "
        "already know over reading a whole file."
    ),
    "input_schema": {
        "type": "object",
        "required": ["path"],
        "properties": {
            "path": {"type": "string", "description": "Repo-relative path."},
            "start_line": {"type": "integer", "description": "1-based, default 1."},
            "end_line": {
                "type": "integer",
                "description": f"Inclusive. At most {_MAX_READ_LINES} lines per call.",
            },
        },
    },
}

GREP_TOOL = {
    "name": "grep",
    "description": (
        "Search tracked files for a POSIX extended regex, returning file:line "
        "matches. Use this to find the tests for a transaction type "
        "(src/test/app/) or the other callers of a shared function."
    ),
    "input_schema": {
        "type": "object",
        "required": ["pattern"],
        "properties": {
            "pattern": {"type": "string", "description": "POSIX extended regex."},
            "path": {
                "type": "string",
                "description": (
                    "Optional repo-relative directory or glob to restrict the "
                    "search to, e.g. src/test/app."
                ),
            },
        },
    },
}

DIFF_TOOL = {
    "name": "git_diff",
    "description": (
        "Show this PR's diff for one repo-relative file, against the base "
        "commit the review was computed from. This is the change under review."
    ),
    "input_schema": {
        "type": "object",
        "required": ["path"],
        "properties": {
            "path": {"type": "string", "description": "Repo-relative path."}
        },
    },
}

TOOLS = [READ_TOOL, GREP_TOOL, DIFF_TOOL, SUBMIT_TOOL]


@dataclass
class AgentResult:
    """One judgement, plus what it cost and how it got there."""

    # Exact observed spans plus hashes of the arguments/results, but never raw
    # repository content.
    evidence_trace: EvidenceTrace = field(repr=False)
    verdict: dict | None = None
    error: str | None = None
    iterations: int = 0
    input_tokens: int = 0
    output_tokens: int = 0
    cache_read_tokens: int = 0
    cache_write_tokens: int = 0
    # `tool: arg` lines, for debugging a verdict that looks wrong without
    # re-running it.
    trail: list[str] = field(default_factory=list)


def make_client(
    aws_region: str,
    aws_profile: str | None = None,
    timeout: float = 600.0,
    max_retries: int = 4,
):
    """A Bedrock Messages API client.

    Imported lazily so every other module in this tool -- and every test that
    does not judge -- keeps working without the Anthropic SDK installed.
    `AnthropicBedrockMantle` is the Messages-API Bedrock endpoint; the older
    `AnthropicBedrock` is the legacy InvokeModel path. Region is required: unlike
    the legacy client there is no default.

    Credentials come from the standard AWS chain, so an assumed role in CI and a
    local SSO profile both work with no code path of their own. `aws_profile` is
    only a convenience over exporting `AWS_PROFILE`.

    Note for anyone writing the IAM policy: this endpoint is
    `bedrock-mantle.<region>.api.aws` and signs SigV4 as **`bedrock-mantle`**,
    not `bedrock`. A policy written for the classic Bedrock InvokeModel actions
    will not authorize it.
    """
    from anthropic import AnthropicBedrockMantle

    return AnthropicBedrockMantle(
        aws_region=aws_region,
        aws_profile=aws_profile,
        timeout=timeout,
        max_retries=max_retries,
    )


def _roll_cache_breakpoint(messages: list[dict]) -> None:
    """Move the conversation's cache breakpoint to its current end.

    Without this only the tools and system prompt are cached, and every turn
    re-pays full price for the entire transcript so far -- which in an agentic
    loop *is* the bill: a measured Opus run spent 583K input tokens on a
    conversation whose final context was a fraction of that, with under 9% of it
    served from cache.

    The breakpoint has to move rather than accumulate: a request accepts at most
    four, and this loop runs far more turns than that. Stripping the old marker
    does not discard the entry it wrote -- the new breakpoint looks backward for
    an existing prefix, and the previous turn's entry is a couple of blocks away,
    well inside the lookback window. Only blocks this module built are touched;
    assistant turns come back as SDK objects and are left exactly as received.
    """
    for message in messages:
        content = message.get("content")
        if isinstance(content, list):
            for block in content:
                if isinstance(block, dict):
                    block.pop("cache_control", None)
    tail = messages[-1].get("content")
    if isinstance(tail, list) and tail and isinstance(tail[-1], dict):
        tail[-1]["cache_control"] = {"type": "ephemeral"}


def _n_turns(count: int) -> str:
    return f"{count} turn" if count == 1 else f"{count} turns"


def _validate_verdict(payload: object) -> str | None:
    """The first schema violation in a submitted verdict, or None if it is sound."""
    errors = sorted(
        Draft202012Validator(VERDICT_SCHEMA).iter_errors(payload),
        key=lambda e: [str(p) for p in e.path],
    )
    if not errors:
        return None
    return "; ".join(f"{list(e.path) or 'input'}: {e.message}" for e in errors[:4])


def display_verdict(payload: dict) -> str:
    """Derive the one-line UI badge from independent model judgements.

    The model is deliberately not allowed to collapse these itself. Correct code
    with no boundary test is useful information, but it is not the same result as
    either a demonstrated behavior bug or a covered, correct combination.
    """
    if payload.get("behavior") == BEHAVIOR_BROKEN:
        return VERDICT_GAP
    if payload.get("coverage") == COVERAGE_MISSING:
        return VERDICT_COVERAGE_GAP
    if (
        payload.get("behavior") == BEHAVIOR_CORRECT
        and payload.get("coverage") == COVERAGE_COVERED
    ):
        return VERDICT_HANDLED
    return VERDICT_UNCLEAR


def tuning_for(model: str, effort: str, max_tokens: int) -> dict:
    """The thinking/effort request fields this model actually accepts.

    Judging wants the model reasoning either way; what differs is how you ask.
    On a pre-4.6 model `effort` is rejected outright and thinking needs an
    explicit budget, which must be under `max_tokens` and at least 1024.
    """
    if any(family in model for family in _ADAPTIVE_THINKING_MODELS):
        return {
            "thinking": {"type": "adaptive"},
            "output_config": {"effort": effort},
        }
    budget = max(1024, min(8000, max_tokens // 2))
    return {"thinking": {"type": "enabled", "budget_tokens": budget}}


def _dispatch_snapshot(snapshot: SourceSnapshot, name: str, args: dict) -> str:
    """Serve one model tool from the exact selected source snapshot."""
    if name == "read_file":
        return snapshot.read_file(
            str(args["path"]),
            start_line=int(args.get("start_line") or 1),
            end_line=(None if args.get("end_line") is None else int(args["end_line"])),
            max_lines=_MAX_READ_LINES,
        )
    if name == "grep":
        return snapshot.grep(
            str(args["pattern"]),
            path=None if not args.get("path") else str(args["path"]),
            max_matches=_MAX_GREP_MATCHES,
        )
    if name == "git_diff":
        return snapshot.git_diff(str(args["path"]))
    raise ValueError(f"unknown tool: {name}")


def run_judgement(
    client,
    *,
    source_snapshot: SourceSnapshot,
    system: str,
    question: str,
    model: str = DEFAULT_MODEL,
    effort: str = DEFAULT_EFFORT,
    max_tokens: int = DEFAULT_MAX_TOKENS,
    max_iterations: int = DEFAULT_MAX_ITERATIONS,
    verdict_validator: Callable[[dict], str | None] | None = None,
) -> AgentResult:
    """Investigate one interaction and return its verdict.

    Terminates on `submit_verdict`, on the iteration cap, or on a turn that ends
    without either -- in which case the model gets exactly one nudge before the
    result is recorded as unclear. Never raises for model behaviour; a caller
    gets an AgentResult with `error` set instead, because one failed judgement
    must not cost the whole comment.
    """
    evidence_trace = EvidenceTrace(source_snapshot.repo_root)
    result = AgentResult(evidence_trace=evidence_trace)
    # A block list rather than a bare string so the rolling cache breakpoint has
    # something to attach to on the very first turn.
    messages: list[dict] = [
        {"role": "user", "content": [{"type": "text", "text": question}]}
    ]
    nudged = False

    while result.iterations < max_iterations:
        _roll_cache_breakpoint(messages)
        try:
            with client.messages.stream(
                model=model,
                max_tokens=max_tokens,
                # One of two breakpoints. This one covers the tools and the
                # system prompt -- byte-identical across every item in a run,
                # and rendered before the messages -- while
                # `_roll_cache_breakpoint` covers the growing transcript.
                # Bedrock has no automatic caching, so both are hand-placed.
                system=[
                    {
                        "type": "text",
                        "text": system,
                        "cache_control": {"type": "ephemeral"},
                    }
                ],
                # Thinking is on by default on Opus 5; stated explicitly so the
                # configuration is readable rather than inherited, and shaped
                # per model so an older one does not 400 on `effort`.
                **tuning_for(model, effort, max_tokens),
                tools=TOOLS,
                messages=messages,
            ) as stream:
                response = stream.get_final_message()
        except Exception as exc:  # noqa: BLE001 - advisory tool; degrade, never fail
            result.error = f"{type(exc).__name__}: {exc}"
            return result

        result.iterations += 1
        usage = response.usage
        result.input_tokens += usage.input_tokens or 0
        result.output_tokens += usage.output_tokens or 0
        result.cache_read_tokens += getattr(usage, "cache_read_input_tokens", 0) or 0
        result.cache_write_tokens += (
            getattr(usage, "cache_creation_input_tokens", 0) or 0
        )

        # Appended whole: thinking blocks must be echoed back unmodified, and
        # tool_use blocks must survive to be matched by their results.
        messages.append({"role": "assistant", "content": response.content})

        calls = [b for b in response.content if b.type == "tool_use"]
        submissions = [c for c in calls if c.name == "submit_verdict"]
        submission_problem: str | None = None
        if submissions:
            submitted = dict(submissions[-1].input)
            submission_problem = _validate_verdict(submitted)
            if submission_problem is None and len(submissions) != 1:
                submission_problem = "submit_verdict must be called exactly once"
            if submission_problem is None and verdict_validator is not None:
                submission_problem = verdict_validator(submitted)
            if submission_problem is None:
                result.verdict = submitted
                return result
            # Malformed: hand it straight back rather than discarding a
            # judgement that may be sound but badly typed. Bounded by the
            # iteration cap like any other turn. We still answer every other
            # tool_use in this assistant turn below; Messages requires one
            # matching tool_result per id before the conversation can continue.
            result.trail.append(f"submit_verdict rejected: {submission_problem}"[:200])

        if not calls:
            if nudged:
                result.error = "ended without submitting a verdict"
                return result
            nudged = True
            messages.append(
                {
                    "role": "user",
                    "content": [
                        {
                            "type": "text",
                            "text": (
                                "Call submit_verdict now with what you have. If "
                                "you did not read enough to decide, that is a "
                                "legitimate answer: submit `unclear` and say "
                                "what was missing."
                            ),
                        }
                    ],
                }
            )
            continue

        results = []
        for call in calls:
            args = dict(call.input)
            label = args.get("path") or args.get("pattern") or ""
            result.trail.append(f"{call.name}: {label}"[:200])
            if call.name == "submit_verdict" and submission_problem is not None:
                content = (
                    "That verdict does not match the schema or investigation packet: "
                    f"{submission_problem}. Call submit_verdict again, fixing "
                    "only what is named here."
                )
                is_error = True
            else:
                try:
                    content = _dispatch_snapshot(source_snapshot, call.name, args)
                    is_error = False
                except Exception as exc:  # noqa: BLE001 - hand model its mistake
                    content = f"{type(exc).__name__}: {exc}"
                    is_error = True
            if len(content) > _MAX_TOOL_RESULT_CHARS:
                content = (
                    content[:_MAX_TOOL_RESULT_CHARS]
                    + "\n... truncated; request a narrower range."
                )
            # Record the exact string placed in the tool_result block. Doing
            # this after the cap is essential: unseen output must never ground
            # a later citation merely because the tool produced it locally.
            evidence_trace.record(
                call.name,
                args,
                content,
                is_error=is_error,
            )
            results.append(
                {
                    "type": "tool_result",
                    "tool_use_id": call.id,
                    "content": content,
                    "is_error": is_error,
                }
            )
        # Tell it when the budget is nearly gone. Without this the cap arrives
        # as a silent guillotine: the model is still mid-investigation, spends
        # the whole run, and returns nothing -- which is strictly worse than the
        # abstention it would happily have written had it known.
        remaining = max_iterations - result.iterations
        if remaining <= _WRAP_UP_TURNS:
            results.append(
                {
                    "type": "text",
                    "text": (
                        f"You have {_n_turns(remaining)} left before this "
                        f"investigation is cut off. Call submit_verdict on the "
                        f"next turn with what you have — `unclear`, saying what "
                        f"you were still missing, is a perfectly good answer and "
                        f"much better than being cut off with none."
                    ),
                }
            )
        # All results in one user message: splitting them trains the model out
        # of parallel tool calls.
        messages.append({"role": "user", "content": results})

    result.error = f"hit the {max_iterations}-iteration cap"
    return result


def describe_cost(results: list[AgentResult]) -> str:
    """One line of token accounting, so the cost of a run is never invisible."""
    inp = sum(r.input_tokens for r in results)
    out = sum(r.output_tokens for r in results)
    read = sum(r.cache_read_tokens for r in results)
    # Writes are ~1.25x input price and reads ~0.1x, so a line that omits writes
    # understates a cached run's bill -- which is the number this tool is judged
    # on in CI.
    written = sum(r.cache_write_tokens for r in results)
    return (
        f"{len(results)} judgement(s): {inp:,} input, {read:,} cache read, "
        f"{written:,} cache write, {out:,} output, "
        f"{sum(r.iterations for r in results)} model turns"
    )
