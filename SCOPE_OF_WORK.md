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

## 2. Proposed Solution

Build an automated, in-repo documentation system with four components:

1. **Initial documentation pass** — Comprehensively document all 1,183
   source files using Claude Code with deep xrpld context
2. **Continuous maintenance** — A GitHub Action on every PR that detects
   doc drift and suggests updates, using diff-aware LLM analysis
3. **Coverage enforcement** — CI-enforced documentation coverage thresholds
   that ratchet up over time, preventing regression
4. **Developer agents** — Claude Code commands for onboarding, architecture
   questions, doc review, and bug pattern detection

All documentation lives alongside the code. No external repos. No external
dependencies. Documentation accuracy is enforced by CI the same way code
style and test coverage are enforced today.

## 3. Deliverables

### 3.1 Documentation Standards (docs/DOCUMENTATION_STANDARDS.md)

A canonical format guide defining:
- Javadoc-style `/** ... */` Doxygen comments (matches 5,718 existing
  instances in the codebase)
- Documentation levels: file, class, public method, free function, enum
- Required Doxygen tags: `@param`, `@return`, `@note`, `@invariant`
- Quality rules: document behavior and invariants, never paraphrase
  signatures, terse style (2-5 lines for classes, 1-3 for functions)

**Status: Complete.** File created at `docs/DOCUMENTATION_STANDARDS.md`.

### 3.2 Doxygen Configuration Changes (docs/Doxyfile)

- `EXTRACT_ALL = NO` (was `YES`) — so undocumented entities are flagged
  rather than silently extracted
- `GENERATE_XML = YES` (was `NO`) — required for coverxygen to parse
  and measure documentation coverage

**Status: Complete.** Changes applied to `docs/Doxyfile`.

### 3.3 Documentation Coverage Pipeline

**Components:**

| File | Purpose |
|------|---------|
| `.github/doc-coverage-thresholds.json` | Per-module thresholds + quarterly ratchet schedule |
| `.github/scripts/doc-coverage-check.py` | Parses coverxygen LCOV output, checks thresholds, generates PR report |
| `.github/workflows/doc-coverage.yml` | CI workflow: builds Doxygen XML, runs coverxygen, posts coverage to PR |
| `cmake/XrplDocs.cmake` | New `docs-coverage` CMake target |

**How it works:**
1. On every PR touching C++ files, the workflow builds Doxygen XML output
   for both the PR branch and the base branch
2. Coverxygen generates LCOV-format coverage reports from the XML
3. The check script compares coverage against per-module thresholds
4. Ratchet mode (`no_decrease`) prevents any PR from reducing doc coverage
5. New files added in a PR require >= 80% doc coverage
6. Results are posted as a sticky PR comment with per-module breakdown

**Status: Complete.** All files created.

### 3.4 Doc Review GitHub Action

**Components:**

| File | Purpose |
|------|---------|
| `.github/scripts/doc-review.py` | Diff-aware LLM analysis script |
| `.github/workflows/doc-review.yml` | CI workflow: runs on PR, posts review |

**How it works:**
1. On every PR, determines which C++ files changed
2. For each changed file, extracts the git diff hunks and existing doc
   comments
3. Sends both to the Anthropic API with a prompt tuned for xrpld: "Given
   this diff, are existing docs still accurate?"
4. Posts results as **inline review comments** on specific lines AND a
   **summary comment** on the PR
5. Starts in **warning-only mode** (does not block merge)

**Cost control:** Only processes changed files and changed hunks within
those files. A typical PR touches 3-10 files. Estimated cost: $0.05-0.15
per PR.

**Status: Complete.** All files created.

### 3.5 Claude Code Agent Commands

Four developer-facing commands in `.claude/commands/`:

| Command | Purpose |
|---------|---------|
| `doc-review` | Review doc accuracy for files changed on current branch |
| `explain-module` | Explain a module's architecture, classes, control flow, and entry points |
| `how-does-x-work` | Trace a feature through the codebase with file/line references |
| `find-bug-patterns` | Scan code for common xrpld bug patterns (unchecked TER, integer overflow, missing amendment gates, etc.) |

**Status: Complete.** All files created.

### 3.6 Full Codebase Documentation

The initial documentation pass covers 1,183 C++ files organized into 21
module-level PRs. Each PR is scoped to a single subsystem so one domain
expert can review it.

**Status: Not started.** This is the primary execution phase (see Section 5).

## 4. Resources Required

### 4.1 People

| Role | Responsibility | Estimated Time |
|------|---------------|----------------|
| **Documentation lead** (1 person) | Runs Claude Code for each module, reviews output quality, submits PRs, iterates on prompt quality | 50-60% for 15 weeks |
| **Domain reviewers** (3-5 people, rotating) | Review doc PRs for semantic accuracy in their area of expertise. Each reviewer handles 3-5 PRs. | 2-4 hours per PR |
| **CI/infrastructure** (1 person) | Deploys workflows, monitors costs, tunes false positive rate on doc-review action | 10-15% for 15 weeks |

**Total estimated effort:** ~1 FTE for 15 weeks + ~80-120 hours of
reviewer time spread across 3-5 engineers.

### 4.2 Infrastructure & Tools

| Resource | Purpose | Cost |
|----------|---------|------|
| **Anthropic API access** | Powers doc-review GitHub Action | ~$50-100/month (20-30 PRs/week, ~2K tokens per file analysis) |
| **Claude Code license** | Initial documentation pass + developer agent commands | Existing license |
| **GitHub Actions minutes** | Doc-coverage workflow (Doxygen XML build + coverxygen) | ~5-10 min per PR on existing `ubuntu-latest` runners |
| **Coverxygen** | Python package, open source (MIT) | Free |
| **Doxygen** | Already configured and used — existing `ghcr.io/xrplf/ci/tools-rippled-documentation` container | Free (already in CI) |
| **GitHub Actions secret** | `ANTHROPIC_API_KEY` — needed for doc-review workflow | N/A |

**Estimated ongoing cost after initial pass:** $50-150/month for API
usage, negligible CI compute on existing runners.

### 4.3 Access & Permissions

- Write access to the `rippled` repository (or a fork for initial PRs)
- Ability to add GitHub Actions secrets (`ANTHROPIC_API_KEY`)
- Ability to modify required status checks (when promoting doc-review
  from warning to required)

## 5. Execution Plan

### Phase 0: Infrastructure — Week 1

Ship the tooling as a single foundational PR:

- [x] `docs/DOCUMENTATION_STANDARDS.md`
- [x] `docs/Doxyfile` modifications
- [x] `.github/doc-coverage-thresholds.json`
- [x] `.github/scripts/doc-coverage-check.py`
- [x] `.github/workflows/doc-coverage.yml`
- [x] `cmake/XrplDocs.cmake` modifications
- [x] `.github/workflows/doc-review.yml`
- [x] `.github/scripts/doc-review.py`
- [x] `.claude/commands/` (4 agent commands)

**Exit criteria:** All workflows pass on a test PR. Coverage report
renders correctly. Doc-review action posts comments without false positives
on a sample PR.

### Phase 1: Foundation Modules — Weeks 2-4

Document the lowest-level modules first (everything else depends on these):

| PR | Module | ~Files | ~Lines |
|----|--------|--------|--------|
| 1 | `include/xrpl/basics/` + `src/libxrpl/basics/` | 63 | ~15K |
| 2 | `include/xrpl/crypto/` + `src/libxrpl/crypto/` | 6 | ~1.5K |
| 3 | `include/xrpl/json/` + `src/libxrpl/json/` | 18 | ~4K |
| 4 | `include/xrpl/beast/` + `src/libxrpl/beast/` | 88 | ~20K |

**Process per PR:**
1. Create branch `docs/module-<name>` from `develop`
2. Run Claude Code against each file with full context: the file itself,
   its includes, corresponding test files, and the module README
3. Generate `/** */` doc comments following DOCUMENTATION_STANDARDS.md
4. Domain expert reviews for semantic accuracy
5. Run Doxygen build to validate no doc errors
6. Merge, ratchet that module's threshold up to actual coverage level

**Exit criteria:** 4 PRs merged. Coverage for these modules at 60%+.
Doc-review action running in warning mode on all subsequent PRs.

### Phase 2: Protocol & Transaction Engine — Weeks 4-8

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
false positive rate tracked and < 10%.

### Phase 3: Server & Application Layer — Weeks 8-13

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

### Phase 4: Tests & Polish — Weeks 13-15

- Document test files (brief docs only — test name + what it validates)
- Global threshold at 70%
- Full coverage trend reporting on GitHub Pages
- Retrospective: review false positive rate, API costs, contributor
  feedback

**Exit criteria:** 70% global doc coverage. Doc-review required check
with < 5% false positive rate. Coverage trend visible on GitHub Pages.

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
| LLM generates plausible but wrong docs | Medium | High | Every doc PR requires human domain expert review. Model output is a draft, not final product. |
| Doc-review action false positives annoy contributors | Medium | Medium | Warning-only mode for 3 months. Promote to required only when FP rate < 5%. |
| Coverage enforcement blocks unrelated PRs | Low | Medium | Start at 0% threshold with `no_decrease` only. Quarterly increases announced in advance. |
| Reviewer bandwidth bottleneck | Medium | Medium | PRs scoped to single modules. Reviewers rotate. 2-4 hours per PR is manageable. |
| API costs exceed budget | Low | Low | Only processes diff hunks, not full files. ~$0.05-0.15/PR. Monthly budget cap of $200 with alerting. |
| Doxygen XML build adds CI time | Low | Low | Runs in parallel with existing checks. Uses existing documentation container. ~5 min. |
| Doc comments add code noise | Low | Low | Terse style enforced by standards. 2-5 lines per class, 1-3 per function. |
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
| "What does this function do?" | Read the code, guess | Read the inline doc |
| "How does the payment engine work?" | Read Common Prefix spec (maybe stale) | Run `/explain-module` or `/how-does-x-work` |
| "Did this PR break any documented behavior?" | Manual review, hope someone notices | Doc-review action flags it automatically |
| "What's our documentation coverage?" | Unknown | Measured per-module in every PR |
| "Is the spec up to date?" | Check manually, probably not | Docs are in-repo, enforced by CI |

## 10. Out of Scope

- **Formal verification.** This project documents code behavior; it does
  not prove correctness. Formal verification is a separate discipline.
- **External-facing API documentation.** This covers the C++ source code,
  not the JSON-RPC API documentation on xrpl.org.
- **Test coverage.** Test file documentation (Phase 4) is brief and
  optional. Test coverage measurement is handled by existing Codecov
  integration.
- **Architectural decision records.** Module-level READMEs already exist
  for key subsystems. This project adds function/class-level docs, not
  system-level design documents.

## 11. Timeline Summary

```
Week 1        Phase 0: Infrastructure PR (tooling, workflows, standards)
Weeks 2-4     Phase 1: Foundation modules (basics, crypto, json, beast)
Weeks 4-8     Phase 2: Protocol & TX engine (protocol, ledger, tx, paths)
Weeks 8-13    Phase 3: Server & application (overlay, consensus, rpc, app)
Weeks 13-15   Phase 4: Tests & polish, promote to required check
```

**Total duration:** 15 weeks
**Total effort:** ~1 FTE + 80-120 hours reviewer time
**Ongoing cost:** ~$50-150/month API + negligible CI compute
