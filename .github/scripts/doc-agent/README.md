# doc-agent

Automated documentation agent for the xrpld C++ codebase. Built on the
Claude Agent SDK.

## What it does

Two modes:

- **document** — Add Doxygen `/** */` documentation to a C++ file or
  directory. The agent reads the file, related tests, and module skill
  context, then writes documentation comments per the project standards in
  `docs/DOCUMENTATION_STANDARDS.md`.
- **review** — Given a git diff range, detect documentation drift. Used by
  the `doc-review` GitHub Action and locally for testing.

## Requirements

- Node.js >= 20
- `ANTHROPIC_API_KEY` environment variable
- Tools the agent uses: `git`, `gh` (for `--pr`)

## Install

```sh
cd .github/scripts/doc-agent
npm install
```

## Build and lint

```sh
npm run typecheck     # type check without emitting
npm run build         # compile to dist/
npm run lint          # biome lint
npm run format        # biome format --write
npm run check         # lint + format check (read-only)
npm run check:fix     # lint + format + fix
```

## Usage

```sh
export ANTHROPIC_API_KEY=sk-ant-...

# Document a single file
npm run document include/xrpl/basics/base_uint.h

# Document an entire module
npm run document include/xrpl/basics/

# Review a git range
npm run review develop..HEAD

# Review a PR
npm run review -- --pr 1234
```

When invoked outside the xrpld repo, set `XRPLD_ROOT` to the path of the
checkout you want to operate on.

## Outputs

The `review` mode writes two files in the current directory:

- `doc-review-report.md` — markdown summary, posted as the PR comment
- `doc-review-comments.json` — array of inline review comments, posted
  individually on the PR diff

## Layout

```
doc-agent/
├── package.json
├── tsconfig.json
├── biome.json
├── prompts/
│   ├── document-file.md     # System prompt for documentation mode
│   └── review-diff.md       # System prompt for review mode
└── src/
    ├── index.ts             # CLI entry point
    ├── config.ts            # Paths, model, module-skill map
    ├── prompt-loader.ts     # Loads prompts + module skill context
    ├── document.ts          # Document mode
    ├── review.ts            # Review mode
    └── types.ts             # Shared types
```

## Module skills

The agent injects per-module context from `docs/skills/soul/*.md` into its
system prompt based on the file path being processed. The mapping lives in
`src/config.ts` (`MODULE_SKILL_MAP`).

## Notes

- Prompts live in markdown files, not source, so they can be edited without
  touching code.
- The `document` mode uses `permissionMode: 'acceptEdits'` so the agent
  writes directly to the target files. Run against a clean git tree so you
  can review and revert if needed.
