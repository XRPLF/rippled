You are auditing a C++ source file in the xrpld (XRP Ledger daemon)
codebase to determine how completely the file's existing Doxygen
documentation reflects the authoritative design intent captured in its
sibling `.ai.md` file.

This is a read-only audit. Do NOT modify the file.

## Input

You receive up to four pieces of context:
- A **primary** C++ file (.h, .hpp, or .cpp) — the file this audit is
  scoped to.
- The **primary's `.ai.md`** — authoritative prose about the primary file's
  purpose, design, invariants, failure modes, and non-obvious behavior.
- A **partner** file — the header/source counterpart of the primary
  (e.g., the `.h` partner of a `.cpp` primary), if one exists.
- The **partner's `.ai.md`** — authoritative prose about the partner
  file, if one exists.

The **primary's `.ai.md`** is the source of truth for what concepts must
be documented for the primary file. The partner's `.ai.md` is context:
it tells you which concepts the project considers a *partner-file*
responsibility (e.g., a "this class is the public contract for X" theme
that naturally lives in the header). Use it to avoid flagging concepts
that the project's own intent assigns to the partner.

Documentation that satisfies a primary-file concept may live in **either**
the primary file or the partner file — both count as "reflected." Header
docs (the contract) and source docs (the implementation) together form
the full documentation surface, so a concept covered on the header is
not "missed" on the source even if the primary is the source.

## Task

For every distinct concept, invariant, design decision, state transition,
ordering constraint, or failure mode in the `.ai.md`, decide:

1. **Where it belongs.** Each concept has a *correct home* in the
   documentation:
   - `"header"` — the public *contract*: what the function/class promises
     to its caller. Examples: parameter meanings, return-value semantics,
     thread-safety guarantees, when an exception is thrown, "this class
     represents X". These belong on the declaration in the header.
   - `"source"` — the *implementation*: algorithm, ordering of checks,
     state transitions, internal invariants, failure modes, the **why**
     behind non-obvious choices. These belong on the definition in the
     `.cpp` file.
   - `"either"` — concepts that are equally at home in either place
     (e.g., a file-level `@file` block describing overall role).
2. **Whether it is reflected** in the correct home. A concept is
   reflected if a reader of that file's docstrings can understand the
   same point without reading the `.ai.md`. Verbatim wording is not
   required; equivalent meaning is enough. A concept whose correct home
   is the source but only appears on the header is **not** correctly
   placed — it should also (or instead) be on the `.cpp` definition.

A concept is **missed** if it is silent, paraphrased so thinly the
reader cannot rely on the docstring, or documented only in the wrong
home (e.g., implementation depth on the header instead of the source).

Do **not** flag implementation details the `.ai.md` does not call out as
design-significant. Do **not** invent concepts not in the `.ai.md`.

## Output

Respond with **only** a JSON object — no prose, no markdown fences:

```
{
  "file": "<path relative to repo root>",
  "ai_md_concepts": <integer count of distinct concepts identified in the .ai.md>,
  "translated": <integer count of those concepts correctly placed in the docstrings>,
  "missed": [
    {
      "function": "<FunctionOrClassName::method, or 'file-level' for @file content>",
      "topic": "<short topic name, e.g. 'Cumulative balance model'>",
      "home": "header" | "source" | "either",
      "current_state": "absent" | "wrong-home" | "thin",
      "ai_md_quote": "<a short quote from the .ai.md establishing the claim, max ~200 chars>"
    }
  ],
  "verdict": "rerun" | "leave"
}
```

`current_state` values:
- `"absent"`: not mentioned anywhere.
- `"wrong-home"`: present in the partner file but not in the correct home
  (e.g., implementation invariant lives on the header but not the source).
- `"thin"`: mentioned in the correct home but too briefly to convey the
  point.

## Verdict rules

The bar is 100% correctly placed coverage.

- `"leave"` if and only if `missed` is empty — every `.ai.md` concept is
  reflected in its correct home with adequate depth.
- `"rerun"` otherwise. Any missed concept (absent, wrong-home, or thin)
  produces a `"rerun"` verdict.

Be specific in `topic` — "missing invariant X" is useful; "could be more
detailed" is not. Quote the `.ai.md` directly in `ai_md_quote` so a
human can verify the call. Be honest — under-reporting misses defeats
the audit's purpose, but inventing misses is equally wrong.
