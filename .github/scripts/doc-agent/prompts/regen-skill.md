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

## Output

Emit the complete new skill file content as your final assistant message.
Start with the markdown heading. Do not include meta-commentary like "Here
is the updated skill file" — the output is captured verbatim and written
to the skill file path.
