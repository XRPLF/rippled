You are updating a per-module skill file for the xrpld codebase.

A "skill" is a single markdown file at `docs/skills/<module>.md` that
captures the institutional knowledge for one module: what it does, key
classes, conventions, gotchas, and how to work in it. The skill file is
loaded as context whenever an agent works on code in that module.

## Inputs

You will be given:
- The current skill file for the module (the baseline to update)
- A list of `.ai.md` files describing the source files in this module
  (one per source file, with high-signal prose about purpose and design)

## Your task

Produce a new, improved skill file that integrates the knowledge from the
ai.md files into the existing skill. Specifically:

1. Update the description of the module's responsibility if the ai.md files
   reveal more accurate or detailed framing
2. Add any classes, patterns, or invariants the skill is missing
3. Update lists of key files / entry points / conventions
4. Add gotchas and non-obvious behavior surfaced by the ai.md files
5. Keep the structure of the existing skill (don't reorganize for the sake
   of it — only restructure if the existing structure is genuinely failing)
6. Be terse. A skill file is a reference card, not a textbook. 200-500 lines
   is typical; over 1000 means you're padding.

## Quality rules

- **Do not duplicate the ai.md content.** Aggregate, synthesize, distill.
  The skill is the module-level view; individual file details belong in
  ai.md (and eventually in inline Doxygen comments).
- **Preserve accurate existing content.** Don't rewrite working sections.
- **Cite file paths** for specific claims (e.g., "see `STAmount.h:roundToScale`").
- **Flag contradictions.** If two ai.md files describe the same concept
  differently, surface the conflict rather than silently picking one.
- **Keep prose grounded.** No marketing language. No "robust, scalable,
  enterprise-grade" filler. Engineers reading this need facts.

## Output — Chunked Writing (REQUIRED)

You have a per-turn output cap (32K tokens). For larger modules, a
complete skill file will not fit in a single tool call. You MUST write
the file in chunks across multiple tool calls. Do not try to emit the
whole file in one Write — it will be truncated mid-content.

Process:
1. **First chunk (Write)**: Call the `Write` tool with the start of the
   skill: the title heading, the opening overview, and the first 1–2
   major sections. Keep this chunk under ~20K characters of content.
2. **Subsequent chunks (Edit)**: For each remaining section, call the
   `Edit` tool with:
   - `old_string` = the last line currently at the end of the file (must
     be unique enough to match unambiguously — use the full last line)
   - `new_string` = that same last line **plus the next 1–2 sections**
   appended
   Keep each chunk under ~20K characters.
3. **Repeat** until the skill is complete. There is no maximum number
   of Edit calls.

After the file is fully written, respond with a one-line confirmation
listing how many chunks you wrote.

DO NOT emit the skill content in your text response. The file is the
output; the text response is only for confirmation.
