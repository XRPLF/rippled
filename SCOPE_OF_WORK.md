# XRPLD Automated Documentation System — Scope of Work

## 1. Problem Statement

The XRP Ledger daemon (`xrpld`) is a ~275,000 line C++ codebase with 1,183
source files across the core library, protocol layer, and application server.
It is the single implementation of the XRP Ledger protocol and processes
billions of dollars in value.

Despite this criticality, the codebase has minimal inline documentation. Only
569 of 1,183 files contain any Doxygen-style doc comments, and most of those
are sparse — a class-level sentence or two, rarely covering individual methods,
parameters, or behavioral invariants.

The only formal documentation effort — an external specification by Common
Prefix — has fundamental structural problems:

- **Drift is the default state.** The spec lives in a separate repository
  with no CI linkage to the codebase. Every commit to `rippled` that changes
  behavior silently invalidates the spec. Even one week of drift makes
  the spec unreliable.
- **Separate repo, separate context.** No contributor has both repos open.
  When a bug comes in, the developer reads the code, not the spec. A
  recent bug would have been caught if the code itself was documented.
- **No code-level documentation.** The spec describes system-level behavior
  (payment engine, DEX) but does not document individual functions, classes,
  parameters, or invariants. A developer working on a specific function
  gets no help.
- **Vendor dependency.** Ripple has a critical documentation dependency on a
  single external firm. If the contract ends, the spec orphans.
- **Perverse incentive.** The vendor profits from complexity and drift.
  Cleaner code and better inline docs reduce the need for external
  specification work.

## 2. Solution as Built

An automated, in-repo documentation system with five components, all living
alongside the code with no external repos and no external vendor dependency:

1. **Module skills** — Per-module knowledge files in [docs/skills/](docs/skills/)
   that capture the "soul" of each subsystem (key files, patterns, pitfalls,
   invariants). These are the durable, human-maintained context that the
   automated agent and human contributors both consult.
2. **doc-agent (Claude Agent SDK app)** — A TypeScript tool at
   [.github/scripts/doc-agent/](.github/scripts/doc-agent/) with three modes:
   `document` (write Doxygen comments), `review` (detect drift on a diff),
   and `regen-skills` (rebuild a skill file from current code).
3. **Doc-review GitHub Action** — Runs the review mode on every PR; posts
   inline comments and a sticky summary. Currently warning-only.
4. **Coverage enforcement** — CI-enforced documentation coverage thresholds
   that ratchet up over time, preventing regression.
5. **Developer slash commands** — Claude Code commands in
   [.claude/commands/](.claude/commands/) for onboarding, architecture
   questions, doc review, and bug pattern detection.

Documentation accuracy is enforced by CI the same way code style and test
coverage are enforced today.

## 3. Deliverables — Built

### 3.1 Documentation Standards

[docs/DOCUMENTATION_STANDARDS.md](docs/DOCUMENTATION_STANDARDS.md) — canonical
format guide defining:
- Javadoc-style `/** ... */` Doxygen comments (matches existing convention)
- Documentation levels: file, class, public method, free function, enum
- Required Doxygen tags: `@param`, `@return`, `@note`, `@invariant`
- Quality rules: document behavior and invariants, never paraphrase
  signatures, terse style (2–5 lines for classes, 1–3 for functions)

### 3.2 Doxygen Configuration Changes

[docs/Doxyfile](docs/Doxyfile):
- `EXTRACT_ALL = NO` (was `YES`) — undocumented entities are flagged rather
  than silently extracted
- `GENERATE_XML = YES` (was `NO`) — required for coverxygen to parse and
  measure documentation coverage

### 3.3 Module Skills

Thirteen module-level skill files in [docs/skills/](docs/skills/), each one
a self-contained guide to a subsystem's responsibilities, key types, control
flow, conventions, and common pitfalls:

| Skill | Covers |
|-------|--------|
| [consensus.md](docs/skills/consensus.md) | XRPL consensus algorithm + RCL adapters |
| [cryptography.md](docs/skills/cryptography.md) | CSPRNG, secure erasure, key handling |
| [ledger.md](docs/skills/ledger.md) | ReadView/ApplyView, state tables, sandbox |
| [nodestore.md](docs/skills/nodestore.md) | RocksDB/NuDB/Memory backends |
| [peering.md](docs/skills/peering.md) | Overlay + peerfinder |
| [protocol.md](docs/skills/protocol.md) | STObject, SField, Serializer, TER, Keylets |
| [rpc.md](docs/skills/rpc.md) | RPC handler conventions |
| [shamap.md](docs/skills/shamap.md) | SHA-256 Merkle radix tree |
| [sql.md](docs/skills/sql.md) | SOCI database wrapper, checkpointing |
| [test.md](docs/skills/test.md) | Beast unit test framework conventions |
| [transactors.md](docs/skills/transactors.md) | Full transactor template |
| [websockets.md](docs/skills/websockets.md) | WS subscriptions/streams |
| [index.md](docs/skills/index.md) | Top-level codebase map |

These skills serve a dual purpose: they are reference docs for human
contributors, and they are injected as system-prompt context by the
doc-agent (mapping in [src/config.ts](.github/scripts/doc-agent/src/config.ts)).

[install-skills.sh](.github/scripts/doc-agent/install-skills.sh) installs
the same files as Claude Code skills under `.claude/skills/<name>/SKILL.md`,
so any Claude Code session in the repo picks them up automatically.

### 3.4 doc-agent (Claude Agent SDK)

A TypeScript application at [.github/scripts/doc-agent/](.github/scripts/doc-agent/),
built on `@anthropic-ai/claude-agent-sdk`. Three modes:

| Mode | Purpose |
|------|---------|
| `document` | Add Doxygen comments to a file or directory. Reads sibling `<file>.ai.md` context, the module skill, and the source file; uses `permissionMode: 'acceptEdits'` to write directly. |
| `review` | Given a git range or PR number, detect doc drift. Emits `doc-review-report.md` (sticky comment) and `doc-review-comments.json` (inline comments). |
| `regen-skills` | Rebuild a module's skill file at `docs/skills/<module>.md` from the module's `.ai.md` files plus existing skill content. |

Layout:

```
doc-agent/
├── package.json          # Node >= 20.12, @anthropic-ai/claude-agent-sdk
├── biome.json            # lint + format
├── install-skills.sh     # copies docs/skills/*.md → .claude/skills/*/SKILL.md
├── prompts/              # System prompts as markdown (editable without code changes)
│   ├── document-file.md
│   ├── review-diff.md
│   └── regen-skill.md
└── src/
    ├── index.ts          # CLI entry (document | review | regen-skills)
    ├── config.ts         # Paths, model, MODULE_SKILL_MAP
    ├── prompt-loader.ts  # Loads prompts + injects module skill
    ├── document.ts
    ├── review.ts
    ├── regen-skills.ts
    └── types.ts
```

Notable design decisions:
- **Prompts as markdown, not strings.** Operators tune prompts without
  touching TypeScript or redeploying.
- **`.ai.md` sidecar input.** When documenting a file, the agent reads a
  sibling `<file>.ai.md` (high-signal prose generated upstream by the
  `athenah-ai` pipeline) as the authoritative source of intent. These are
  gitignored (`*.ai.md` in [.gitignore](.gitignore)) and discarded once
  the initial pass is complete.
- **Model selection via env.** `DOC_AGENT_MODEL` env var; default
  `claude-sonnet-4-6`.
- **Repo root override.** `XRPLD_ROOT` env var allows running the agent
  against a different checkout (useful in CI and local testing).

### 3.5 Documentation Coverage Pipeline

| File | Purpose |
|------|---------|
| [.github/doc-coverage-thresholds.json](.github/doc-coverage-thresholds.json) | Per-module thresholds + quarterly ratchet schedule |
| [.github/scripts/doc-coverage-check.py](.github/scripts/doc-coverage-check.py) | Parses coverxygen LCOV, checks thresholds, generates PR report |
| [.github/workflows/doc-coverage.yml](.github/workflows/doc-coverage.yml) | CI workflow: builds Doxygen XML, runs coverxygen, posts coverage to PR |
| [cmake/XrplDocs.cmake](cmake/XrplDocs.cmake) | `docs` CMake target wiring |

Flow:
1. On every PR touching C++ files, the workflow builds Doxygen XML for
   both the PR branch and the base branch (using
   `ghcr.io/xrplf/ci/tools-rippled-documentation`).
2. Coverxygen generates LCOV-format coverage from the XML.
3. The check script compares coverage against per-module thresholds.
4. Ratchet mode (`no_decrease`) prevents any PR from reducing coverage.
5. New files added in a PR require ≥ 80% doc coverage.
6. Results are posted as a sticky PR comment with per-module breakdown.

### 3.6 Doc-Review GitHub Action

| File | Purpose |
|------|---------|
| [.github/scripts/doc-review.py](.github/scripts/doc-review.py) | Diff-aware LLM analysis script (Python, uses `anthropic` SDK) |
| [.github/workflows/doc-review.yml](.github/workflows/doc-review.yml) | CI workflow: runs on PR, posts review |

Flow:
1. On every PR, determine which C++ files changed.
2. For each changed file, extract git diff hunks and existing doc comments.
3. Send both to the Anthropic API with a prompt tuned for xrpld:
   "Given this diff, are existing docs still accurate?"
4. Post results as **inline review comments** on specific lines (via
   `actions/github-script`) AND a **sticky summary comment** on the PR.
5. Runs in **warning-only mode** — does not block merge.

The doc-agent `review` mode is the same logic exposed as a local CLI
(`npm run review develop..HEAD` or `npm run review -- --pr 1234`), useful
for testing prompt changes before they ship to CI.

Cost: only changed files and changed hunks within those files are
processed. Estimated ~$0.05–0.15 per PR.

### 3.7 Claude Code Slash Commands

Four developer-facing commands in [.claude/commands/](.claude/commands/):

| Command | Purpose |
|---------|---------|
| [doc-review](.claude/commands/doc-review.md) | Review doc accuracy for files changed on current branch |
| [explain-module](.claude/commands/explain-module.md) | Explain a module's architecture, classes, control flow, entry points |
| [how-does-x-work](.claude/commands/how-does-x-work.md) | Trace a feature through the codebase with file/line references |
| [find-bug-patterns](.claude/commands/find-bug-patterns.md) | Scan code for common xrpld bug patterns (unchecked TER, integer overflow, missing amendment gates, etc.) |

### 3.8 Full Codebase Documentation

The initial documentation pass covers 1,183 C++ files organized into 21
module-level PRs (see Section 5). The doc-agent `document` mode is the
tool that produces each PR; each file's output is then domain-expert
reviewed before merge.

**Status: Pending execution.** Tooling is built; the per-module PRs are
the primary remaining work.

## 4. Resources Required

### 4.1 People

| Role | Responsibility | Estimated Time |
|------|---------------|----------------|
| **Documentation lead** (1 person) | Runs `doc-agent document` per module, reviews output, submits PRs, iterates on prompts in [prompts/](.github/scripts/doc-agent/prompts/) | 50–60% for 15 weeks |
| **Domain reviewers** (3–5 people, rotating) | Review doc PRs for semantic accuracy in their area of expertise. Each reviewer handles 3–5 PRs. | 2–4 hours per PR |
| **CI/infrastructure** (1 person) | Deploys workflows, monitors costs, tunes false-positive rate on doc-review action | 10–15% for 15 weeks |

**Total estimated effort:** ~1 FTE for 15 weeks + ~80–120 hours of
reviewer time spread across 3–5 engineers.

### 4.2 Infrastructure & Tools

| Resource | Purpose | Cost |
|----------|---------|------|
| **Anthropic API access** | Powers the doc-agent (`document`, `review`, `regen-skills`) and the doc-review GitHub Action | ~$50–150/month steady state |
| **Claude Agent SDK** | `@anthropic-ai/claude-agent-sdk` Node package | Free |
| **Node.js >= 20.12** | Native `--env-file` support; runs the doc-agent | Free |
| **GitHub Actions minutes** | Doc-coverage workflow (Doxygen XML build + coverxygen) and doc-review workflow | ~5–10 min per PR on existing runners |
| **Coverxygen** | Python package, open source (MIT) | Free |
| **Doxygen** | Already configured — uses existing `ghcr.io/xrplf/ci/tools-rippled-documentation` container | Free (already in CI) |
| **GitHub Actions secret** | `ANTHROPIC_API_KEY` — for doc-review workflow | N/A |
| **athenah-ai pipeline output** | Generates `.ai.md` sidecar context files consumed by `doc-agent document` | Upstream — gitignored, removed post-pass |

### 4.3 Access & Permissions

- Write access to the `rippled` repository (or a fork for initial PRs)
- Ability to add GitHub Actions secrets (`ANTHROPIC_API_KEY`)
- Ability to modify required status checks (when promoting doc-review from
  warning to required)

## 5. Execution Plan

### Phase 0: Infrastructure — Complete

Tooling shipped as the foundation PR:

- [x] [docs/DOCUMENTATION_STANDARDS.md](docs/DOCUMENTATION_STANDARDS.md)
- [x] [docs/Doxyfile](docs/Doxyfile) modifications
- [x] [docs/skills/](docs/skills/) — 13 module skills + index
- [x] [.github/scripts/doc-agent/](.github/scripts/doc-agent/) — Agent SDK app (document / review / regen-skills)
- [x] [.github/scripts/doc-agent/install-skills.sh](.github/scripts/doc-agent/install-skills.sh)
- [x] [.github/doc-coverage-thresholds.json](.github/doc-coverage-thresholds.json)
- [x] [.github/scripts/doc-coverage-check.py](.github/scripts/doc-coverage-check.py)
- [x] [.github/workflows/doc-coverage.yml](.github/workflows/doc-coverage.yml)
- [x] [cmake/XrplDocs.cmake](cmake/XrplDocs.cmake)
- [x] [.github/workflows/doc-review.yml](.github/workflows/doc-review.yml)
- [x] [.github/scripts/doc-review.py](.github/scripts/doc-review.py)
- [x] [.claude/commands/](.claude/commands/) — 4 developer slash commands

**Exit criteria met:** All workflows pass on a test PR. Coverage report
renders correctly. Doc-review action posts comments without false positives
on a sample PR.

### Phase 1: Foundation Modules — Weeks 2–4

Document the lowest-level modules first (everything else depends on these):

| PR | Module | ~Files | ~Lines |
|----|--------|--------|--------|
| 1 | `include/xrpl/basics/` + `src/libxrpl/basics/` | 63 | ~15K |
| 2 | `include/xrpl/crypto/` + `src/libxrpl/crypto/` | 6 | ~1.5K |
| 3 | `include/xrpl/json/` + `src/libxrpl/json/` | 18 | ~4K |
| 4 | `include/xrpl/beast/` + `src/libxrpl/beast/` | 88 | ~20K |

**Process per PR:**
1. Create branch `docs/module-<name>` from `develop`.
2. Run `npm run document <path>` from `.github/scripts/doc-agent/`. The
   agent reads each file's `.ai.md` sidecar, the matching module skill,
   and the file itself, then writes Doxygen comments per the standards.
3. Domain expert reviews for semantic accuracy.
4. Run Doxygen build to validate no doc errors.
5. Merge; ratchet that module's threshold up to actual coverage level.

**Exit criteria:** 4 PRs merged. Coverage for these modules at 60%+.
Doc-review action running in warning mode on all subsequent PRs.

### Phase 2: Protocol & Transaction Engine — Weeks 4–8

| PR | Module | ~Files |
|----|--------|--------|
| 5 | `include/xrpl/protocol/` + `src/libxrpl/protocol/` | 150 |
| 6 | `include/xrpl/ledger/` + `src/libxrpl/ledger/` | 68 |
| 7 | `include/xrpl/conditions/` + `src/libxrpl/conditions/` | 8 |
| 8 | `include/xrpl/tx/` (core framework: Transactor, ApplyContext) | 15 |
| 9 | Payment transactors | 9 |
| 10 | DEX/AMM transactors | 25 |
| 11 | Escrow transactors | 7 |
| 12 | Other transactors (NFT, token, vault, check, etc.) | 60 |
| 13 | Pathfinding + invariants | 30 |

**Exit criteria:** 9 PRs merged. Global coverage at 40%+. Doc-review
false-positive rate tracked and < 10%.

### Phase 3: Server & Application Layer — Weeks 8–13

| PR | Module | ~Files |
|----|--------|--------|
| 14 | `include/xrpl/server/` + `src/libxrpl/server/` | 35 |
| 15 | `include/xrpl/nodestore/` + `src/libxrpl/nodestore/` | 30 |
| 16 | SHAMap | 25 |
| 17 | Resource management | 17 |
| 18 | Overlay + peerfinder | 56 |
| 19 | Consensus | 15 |
| 20 | Application core (ledger, main, misc, rdb) | 133 |
| 21 | RPC handlers | 131 |

**Exit criteria:** 8 PRs merged. Global coverage at 60%+. Doc-review
action promoted from warning to **required check**.

### Phase 4: Tests & Polish — Weeks 13–15

- Document test files (brief docs only — test name + what it validates)
- Global threshold at 70%
- Full coverage trend reporting on GitHub Pages
- Remove `.ai.md` sidecar files (they were transitional input only)
- Retrospective: review false-positive rate, API costs, contributor
  feedback

**Exit criteria:** 70% global doc coverage. Doc-review required check
with < 5% false-positive rate. Coverage trend visible on GitHub Pages.

## 6. Threshold Ratchet Schedule

Coverage thresholds increase quarterly to prevent regression and drive
gradual improvement:

| Quarter | Global Minimum | Enforcement |
|---------|---------------|-------------|
| Launch (2026-Q2) | 0% | `no_decrease` ratchet only |
| 2026-Q3 | 30% | Blocks PRs below threshold |
| 2026-Q4 | 40% | |
| 2027-Q1 | 50% | |
| 2027-Q2 | 60% | |
| 2027-Q3 | 70% | Target steady state |

New files always require 80% coverage regardless of the global threshold.

## 7. Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| LLM generates plausible but wrong docs | Medium | High | Every doc PR requires human domain expert review. `.ai.md` sidecars (athenah-ai) ground the agent in source-derived intent rather than free generation. |
| Doc-review action false positives annoy contributors | Medium | Medium | Warning-only mode for 3 months. Promote to required only when FP rate < 5%. Prompts live in markdown ([prompts/](.github/scripts/doc-agent/prompts/)) and can be tuned without a code release. |
| Coverage enforcement blocks unrelated PRs | Low | Medium | Start at 0% threshold with `no_decrease` only. Quarterly increases announced in advance. |
| Reviewer bandwidth bottleneck | Medium | Medium | PRs scoped to single modules. Reviewers rotate. 2–4 hours per PR is manageable. |
| API costs exceed budget | Low | Low | Only diff hunks processed. ~$0.05–0.15/PR. Monthly budget cap of $200 with alerting. |
| Doxygen XML build adds CI time | Low | Low | Runs in parallel with existing checks. Uses existing documentation container. ~5 min. |
| Doc comments add code noise | Low | Low | Terse style enforced by standards. 2–5 lines per class, 1–3 per function. |
| Skill files drift from code | Medium | Medium | `doc-agent regen-skills <module>` rebuilds a skill from current `.ai.md` files; intended to be run periodically. |
| Initial pass takes longer than 15 weeks | Medium | Low | Modules are independent. Can parallelize with multiple contributors. Lower-priority modules can slip. |

## 8. Success Metrics

| Metric | Target | Measurement |
|--------|--------|-------------|
| Documentation coverage (public API) | 70% | Coverxygen LCOV reports in CI |
| Doc drift catch rate | > 90% of behavioral changes flagged | Sample audit of merged PRs vs doc-review output |
| False positive rate (doc-review action) | < 5% | Track dismissed vs accepted suggestions |
| Zero spec-vs-code contradictions | 0 incidents | Bug reports citing wrong documentation |
| Contributor satisfaction | > 4/5 rating | Quarterly survey: "docs helped me understand the code" |
| Onboarding time reduction | 30% faster first meaningful PR | Measure across new contributors before/after |
| API cost | < $150/month steady state | Anthropic API billing dashboard |

## 9. What This Replaces

This system does **not** replace the Common Prefix formal verification
work directly — formal verification and code documentation solve different
problems. However, it eliminates the need for an external specification as
the "source of truth" for how xrpld behaves:

| Need | Before | After |
|------|--------|-------|
| "What does this function do?" | Read the code, guess | Read the inline Doxygen doc |
| "How does the payment engine work?" | Read Common Prefix spec (maybe stale) | Read [docs/skills/transactors.md](docs/skills/transactors.md) or run `/explain-module` |
| "Did this PR break any documented behavior?" | Manual review, hope someone notices | Doc-review action flags it automatically |
| "What's our documentation coverage?" | Unknown | Measured per-module in every PR |
| "Is the spec up to date?" | Check manually, probably not | Docs are in-repo, enforced by CI |
| "Where do I start in module X?" | Ask in chat | Read the module skill in [docs/skills/](docs/skills/) |

## 10. Out of Scope

- **Formal verification.** This project documents code behavior; it does
  not prove correctness. Formal verification is a separate discipline.
- **External-facing API documentation.** This covers the C++ source code,
  not the JSON-RPC API documentation on xrpl.org.
- **Test coverage.** Test file documentation (Phase 4) is brief and
  optional. Test coverage measurement is handled by existing Codecov
  integration.
- **Architectural decision records.** Module-level READMEs already exist
  for key subsystems. This project adds function/class-level docs and the
  module skills layer, not system-level ADRs.

## 11. Timeline Summary

```
Week 1        Phase 0: Infrastructure (tooling, workflows, standards, skills) — COMPLETE
Weeks 2-4     Phase 1: Foundation modules (basics, crypto, json, beast)
Weeks 4-8     Phase 2: Protocol & TX engine (protocol, ledger, tx, paths)
Weeks 8-13    Phase 3: Server & application (overlay, consensus, rpc, app)
Weeks 13-15   Phase 4: Tests & polish, promote to required check, drop .ai.md sidecars
```

**Total duration:** 15 weeks
**Total effort:** ~1 FTE + 80–120 hours reviewer time
**Ongoing cost:** ~$50–150/month API + negligible CI compute
