# Implementation Notes: AI Cross-Amendment Dependency Reviewer

Companion to [README.md](./README.md), which is the design/review document.
Read the README first — it carries the motivation, the architecture, and the
key decisions. This file holds the build-level detail that a reviewer does not
need in order to approve the direction: the extraction algorithm, schemas,
sample artifacts, per-PR mechanics, the full rule catalog, and the file layout.

---

## 1. Graph extraction (the scanner)

**For v1, a name-anchored text scanner written in pure-standard-library
Python** — no third-party dependencies, matching how the levelization check
already runs.

Text scanning works here because the gate API is unusually regular. Every gate
looks like `x.enabled(id)` or `x->enabled(id)`, and `id` always comes from a
fixed, known set: the identifiers generated from `features.macro` (`feature` +
name, or `fix` + name). The scanner:

1. **Reads `features.macro` first** (a strict, sorted, line-oriented format) to
   get the exact list of amendment identifiers.
2. **Matches only those literal names**, e.g.
   `\benabled\(\s*(featureBatchV1_1|fixCleanup3_3_0|…)\s*\)`. Anything that is
   not a declared amendment can't match, so precision is effectively 100%.
3. **Strips comments and string literals first** with a small state machine.
   This is essential — amendment names appear constantly in comments (for
   example the `Rules.cpp:42` comment block).
4. **Attributes each match to its enclosing function** by tracking brace depth
   and remembering the most recent function signature. This is reliable here
   because clang-format is enforced.
5. **Reads the boolean context.** It walks out to the surrounding condition,
   treats each `enabled(NAME)` as a single term and everything else as opaque,
   and parses the `!`, `&&`, `||`, and parentheses. For any pair of amendments
   in one expression, the relationship is the operator that joins them, and
   negation is tracked per term (so `!fixCleanup3_2_0 && !featureMPTokensV1`
   becomes an `and` edge with both terms negated).
6. **Records weaker "same function" links.** When two amendments are checked in
   the same function but not in the same expression, that becomes a weak edge.
   This partially covers a known gap — data flowing through boolean local
   variables, exactly the `Rules.cpp:44-49` pattern — until v3.

**Considered, but deferred to v3: libclang AST extraction.** It is more accurate
(it can follow data flow and gates hidden inside helper functions), but it needs
`compile_commands.json`, which means a conan + cmake configure that takes
minutes and is cache-sensitive — too heavy for a check meant to run in seconds.
CodeQL needs a full build database and is heavier still. In v3, a libclang
extractor can drop in behind the same schema; the text scanner stays as the fast
local path, with a CI check that the AST results always include everything the
regex found. The text approach has one more advantage: because it keys on tokens
rather than file paths, it is unaffected by directory reorganizations — the
recent move of transactors from `src/xrpld/app/tx/detail/` to
`src/libxrpl/tx/transactors/` would have been transparent to it.

---

## 2. Graph schema

Nodes are keyed by the full generated identifier (unambiguous, easy to grep, and
matches the source):

```json
{
  "schema": 1,
  "nodes": {
    "fixAMMv1_3": {
      "kind": "fix",
      "status": "supported",
      "vote": "defaultNo",
      "mainnet": "enabled",
      "lineage": ["fixAMMv1_2"],
      "hub": false
    }
  },
  "edges": [
    {
      "type": "and",
      "amendments": ["fixAMMv1_3", "fixCleanup3_3_0"],
      "negated": [],
      "scope": "prod",
      "sites": [
        "src/libxrpl/tx/transactors/dex/AMMWithdraw.cpp::AMMWithdraw::doApply"
      ]
    }
  ]
}
```

- `status`: `supported | unsupported | obsolete | retired`, taken from
  `features.macro`.
- `mainnet`: `enabled | majority | voting | unknown`, merged in at read time
  from `network-status.json` and never stored in `graph.json`.
- `lineage`: derived from naming rules — drop the version suffix
  (`fixAMMv1_3 → fixAMMv1_2 → fixAMMv1_1`) and match a fix's base name to its
  feature (`fixAMMv1_1 → featureAMM`).

**Edge types**, strongest first:

| type              | meaning                                                                                        | source                         |
| ----------------- | ---------------------------------------------------------------------------------------------- | ------------------------------ |
| `declared`        | an authoritative relationship from `amendment-deps.yml`                                        | the sidecar file               |
| `and`             | both required in one boolean expression (negation tracked per term)                            | expression parse               |
| `or`              | "earliest activation wins" alternatives                                                        | expression parse               |
| `lineage`         | a succession chain derived from names                                                          | naming rules                   |
| `test-co-toggled` | toggled together in `FeatureBitset` algebra or `amendmentCombinations({...})` under `src/test` | v2 test scanner, `scope: test` |
| `co-function`     | checked in the same function but not the same expression (weak)                                | function tracking              |

**Two stability rules (learned from levelization).** Edge sites are stored as
sorted `path::Class::function` strings, with **no line numbers** (they'd change
on every unrelated edit) and **no counts** (`fixCleanup3_2_0` appears at ~52
sites; adding a 53rd isn't meaningful and shouldn't change the graph).
Function-level granularity stays stable across edits while remaining precise
enough for the diff to notice when a function's gating changes. Line numbers
appear only in the short-lived evidence bundle built at PR time.

---

## 3. Per-run artifacts

The graph is a build artifact, rebuilt on every run for both the PR head and the
base branch and never committed (see README §3 for the rationale). Following
levelization's two-file shape:

- `graph.json` — the canonical form: sorted keys, sorted arrays, LF line
  endings, and no timestamps or tool-version strings, so the head-vs-base
  comparison is stable. Built for both branches, uploaded as a workflow
  artifact, and read by the diff classifier and the AI layer.
- `edges.txt` — one line per edge
  (`and  fixAMMv1_3 + fixCleanup3_3_0  [dex/AMMWithdraw.cpp::AMMWithdraw::doApply, …]`),
  so the change is easy to read in a CI log or artifact.

`results/network-status.json` is the one committed artifact (external mainnet
data, updated only by the weekly cron PR — see §5 below).

---

## 4. Declared intent: `amendment-deps.yml`

Scanning the code recovers what the code _does_, but not what the author
_intended_. A separate YAML file captures that intent. We keep it out of
`features.macro` on purpose: that file is a protocol-critical X-macro expanded
into compiled headers, so adding a field touches every consumer for data the
compiler never uses, and a comment convention would have no schema and quietly
rot.

```yaml
amendments:
  fixAMMv1_3:
    supersedes: fixAMMv1_2
    requires: [featureAMM]
    notes: "Rounding fixes; interacts with AMMClawback withdrawal paths."

gate-groups:
  # The Rules.cpp hand-maintained invariant, made machine-checked:
  number-rules-guard:
    superset: [fixCleanup3_2_0, featureSingleAssetVault, featureLendingProtocol]
    subset-functions:
      - "src/libxrpl/protocol/Rules.cpp::setCurrentTransactionRules"
    rule: "amendments extracted from subset-functions must be a subset of superset"

expected-couplings: # reviewed, known-good pairs (suppression)
  - [fixCleanup3_3_0, fixAMMv1_3]
```

The `gate-groups` block is the most valuable part. It turns the
comment-enforced invariant at `Rules.cpp:42-79` — which the comments themselves
flag as a live hazard — into a **hard CI check**: a simple set-inclusion test
over the extracted identifier lists, run at PR time instead of as a runtime
assert.

Cross-checks, advisory first:

- an extracted edge with no matching declaration → an `info` finding
  ("undeclared coupling") for the AI layer to consider;
- a declaration with no matching evidence in code → a `warning` ("possibly
  stale declaration");
- a gate-group violation → a **hard fail**.

The file starts almost empty (just the Rules.cpp gate-group) and grows over
time. Missing declarations never block a merge.

---

## 5. Mainnet snapshot mechanics

A **weekly scheduled workflow** calls the public `feature` RPC (served from the
`ltAMENDMENTS` ledger object; see
[`src/xrpld/rpc/handlers/server_info/Feature.cpp`](../../src/xrpld/rpc/handlers/server_info/Feature.cpp)
and `ledger_entries.macro:185`) against `s1.ripple.com`, falling back to
`xrplcluster.com`, normalizes the response, and **opens an automated PR** to
update `results/network-status.json` whenever it changes.

Alternatives we rejected:

- **Calling the RPC live at PR time.** This makes CI non-deterministic
  (endpoints flap, rate-limit, or report transient majority states) and makes
  re-running an old PR irreproducible.
- **Using the develop branch alone** (treating `Supported::Yes` and not
  `Obsolete` as a proxy for "active"). This needs no external data but is wrong
  both ways: it lags real activations and also overstates them, since several
  `Supported::Yes` amendments aren't enabled on mainnet. We keep it only as a
  documented fallback when a snapshot entry is missing.

Amendments activate roughly monthly, so a weekly snapshot is fresh enough, and
its `asOf` and `source` fields make staleness easy to audit.

---

## 6. Per-PR check mechanics

`reusable-check-amendment-graph.yml` runs via `workflow_call`, wired into
[`on-pr.yml`](../../.github/workflows/on-pr.yml) behind the existing
`should-run` changed-files gate (trigger paths `src/**`, `include/**`,
`.github/scripts/amendment-graph/**`):

1. **Build the head graph.** Run `generate.py` over the PR head to produce
   `graph.json` and `edges.txt` in the runner's workspace (not committed).
2. **Build the base graph.** Check out the base branch at `$BASE_SHA` into a
   scratch worktree and run the _same pinned_ `generate.py` over it. Both graphs
   are temporary — nothing is committed, so there is no drift check and no
   "stale graph" failure. Runs stay reproducible because the scanner is pinned.
3. **Diff the two** and classify each change:

   | class                         | example                                                        | what happens                                                                                |
   | ----------------------------- | -------------------------------------------------------------- | ------------------------------------------------------------------------------------------- |
   | `node-added`                  | a new amendment is declared                                    | → finding                                                                                   |
   | `node-status-changed`         | `Supported::Yes → No`                                          | **hard fail** (lifecycle violation); obsoletion or retirement → finding                     |
   | `edge-added` / `edge-removed` | a new AND-gate pairing                                         | → finding                                                                                   |
   | `gating-changed`              | the amendment set at an existing `file::function` site changed | → finding (the most reviewer-relevant class)                                                |
   | `invariant-violation`         | a gate-group subset rule is broken                             | **hard fail**                                                                               |
   | `retired-reference`           | a retired identifier appears in source                         | **hard fail** (the compiler already warns via `[[deprecated]]`; this surfaces it at review) |
   | `relocation-only`             | structure is identical, only site paths moved (a tree reorg)   | one `info` finding, not hundreds of edge changes                                            |

4. **Write `findings.json`** as a workflow artifact for the AI layer:

```json
{
  "schema": 1,
  "base": "<sha>",
  "head": "<sha>",
  "findings": [
    {
      "id": "edge-added:and:fixAMMv1_3+fixCleanup3_3_0:prod",
      "kind": "edge-added",
      "severity": "info",
      "amendments": ["fixAMMv1_3", "fixCleanup3_3_0"],
      "sites": [
        "src/libxrpl/tx/transactors/dex/AMMWithdraw.cpp::AMMWithdraw::doApply"
      ],
      "graph_context": {
        "mainnet": { "fixAMMv1_3": "enabled", "fixCleanup3_3_0": "voting" },
        "neighbors": { "fixAMMv1_3": ["fixAMMv1_2", "featureAMM"] },
        "declared": false
      },
      "summary": "New AND-gate couples fixAMMv1_3 (mainnet-enabled) with fixCleanup3_3_0 (voting).",
      "reviewer_questions": [
        "Is behavior correct on ledgers where fixAMMv1_3 is enabled but fixCleanup3_3_0 is not?"
      ]
    }
  ]
}
```

---

## 7. Noise controls (detailed)

- **Test code vs. production code.** `src/test` is scanned in a separate pass
  that only produces `scope: test` edges (anchored on
  `Env(*this, features - featureX)` and `amendmentCombinations({...})` from
  [`src/test/jtx/Env.h:112-130`](../../src/test/jtx/Env.h)). Test edges answer
  "were these ever exercised together?" — which rule R1 inverts: a new
  production AND-edge with **no** matching test edge is a strong prompt for the
  reviewer.
- **Generated and vendored code.** Scan only files tracked by `git ls-files`
  under `src/` and `include/`, so untracked build or codegen output is excluded
  automatically.
- **Hub suppression.** Umbrella cleanup fixes and very large functions would
  otherwise produce a flood of meaningless weak edges. If a function checks more
  than ~5 distinct amendments, its `co-function` edges are dropped (recorded as
  a `hubs` entry instead). If an amendment appears in more than ~25 functions,
  it is marked `hub: true` and needs expression-level (`and`/`or`) evidence for
  any new edge. Expression-level edges are never suppressed.
- **Retired amendments.** These stay in the graph as `status: retired` nodes so
  lineage chains stay intact, but any reference to a retired name in source is a
  hard-fail finding.

---

## 8. AI layer specifics

### 8.1 Stage 1 output schema

One Sonnet-tier API call at temperature 0, with a cached stable prompt prefix
and strict JSON-schema output:

```json
{
  "findings": [
    {
      "id": "…static finding id or AI-NEW-n…",
      "verdict": "confirmed | likely | false_positive",
      "severity": "blocker | warning | info",
      "confidence": 0.82,
      "amendments": ["featureX", "fixY"],
      "evidence": [{ "file": "…", "line": 123, "why": "…" }],
      "explanation": "2-4 reviewer-facing sentences",
      "suggested_tests": ["amendmentCombinations({featureX, fixY}, all)"],
      "rule": "R2"
    }
  ],
  "summary": "one paragraph"
}
```

The model may **add** findings of only one kind: semantic reuse (rule R2),
anchored to the function-reuse index below, tagged `AI-NEW-n` and capped at
`warning`. Findings the AI raises on its own can never block a merge.

Stage 2 (per-finding verification) is added in v2 only if the evaluation harness
shows Stage 1 isn't precise enough for findings in the 0.4–0.8 confidence range.

### 8.2 Context bundle (assembled deterministically, ~60k token target)

In priority order (lowest priority is trimmed first if we run long):

1. **`findings.json`** — included verbatim, always.
2. **Relevant diff hunks** — only the files the static layer flagged as
   amendment-related, with ±30 lines of context so the enclosing function and
   any governing gate are visible. Never the whole diff.
3. **Graph neighborhood** — the touched amendments and their direct neighbors,
   with edge types and statuses; two hops out only along lineage.
4. **Network snapshot** — so the model can tell replay-dangerous changes from
   ones on not-yet-active amendments.
5. **Function-reuse index (relevant slice)** — for each function the new or
   changed code calls that contains an amendment gate (in its body or in shallow
   callees): its name, location, and which amendments its behavior depends on.
   This is what catches the `fixAMMClawbackRounding` case.
6. **A few curated examples** — a committed `priors.md` with 4–6 historical
   interaction bugs, each about 10 lines: what the code looked like, what was
   missed, and what a good warning would have said.
7. **Two CONTRIBUTING rules** — every amendment needs an XLS spec, and
   transaction-processing changes must be amendment-gated.

Cost: roughly 40–80k tokens in and 2–4k out, about **$0.15–0.40 and 30–90
seconds per PR that has findings, and $0 otherwise**. On a new push we re-run
only if amendment-related files changed since the last analyzed commit.

### 8.3 Invocation and fork-PR safety

**A plain, version-pinned script using the Anthropic SDK — not an agentic CLI.**
A tool with shell and file access is exactly what shouldn't run against
untrusted fork content, and there is no need for it: the context bundle already
contains everything.

Fork secrets are handled by splitting the work across two jobs:

1. The **unprivileged job** (triggered by `pull_request`) checks out the PR
   code, runs the static layer, and uploads `findings.json` and the context
   bundle as an artifact. It needs no secrets.
2. The **privileged job** (triggered by `workflow_run`, running only base-branch
   scripts) downloads that artifact, treats it as **pure data**, makes the API
   call, and posts the comment. It never runs anything from the PR head. So a
   prompt-injection attempt in the diff can, at worst, produce a misleading
   comment — never code execution or leaked secrets, because the model's output
   is schema-validated JSON that only ever becomes comment text.
3. For fork PRs, the privileged job additionally requires a maintainer-applied
   label (like the existing `DraftRunCI` convention) or a `MEMBER`/`COLLABORATOR`
   author.
4. If org policy forbids API keys in this repo at all, GitHub-hosted models
   (`models: read` on `GITHUB_TOKEN`, which works for forks) are a lower-quality
   fallback.

### 8.4 Reviewer-facing output (full mockup)

**A single sticky comment** (marked with a hidden `<!-- amendment-reviewer:v1
-->` and edited in place on each push), **posted only when there are findings**.
If a later push resolves everything, the comment is edited down to a one-line
"resolved as of `<sha>`" rather than deleted, to keep an audit trail. The same
content also appears as a job summary and a **neutral** check run.

> ## Amendment interaction review (2 findings · analyzed `abc1234`)
>
> ### 🔴 Blocker — R3: `Supported::Yes → No` reversion
>
> `fixFooBar` changed from `Supported::Yes` to `Supported::No` in
> features.macro. A released `Supported::Yes` amendment can never revert
> (older servers would amendment-block); use `VoteBehavior::Obsolete` instead.
> Evidence: `include/xrpl/protocol/detail/features.macro:31`
>
> ### 🟡 Warning — R2: new gated code reuses amendment-dependent logic (confidence 0.82)
>
> Interacting: `featureNewThing` × `fixAMMv1_1`
> `NewThingCreate::doApply` calls `ammWithdrawImpl()`, whose rounding differs
> under `fixAMMv1_1` (`…/AMMWithdraw.cpp:412`). The new code is gated only on
> `featureNewThing`, which activates independently. Precedent:
> `fixAMMClawbackRounding` (#5513) shipped for exactly this pattern.
> Suggested test: `amendmentCombinations({featureNewThing, fixAMMv1_1}, all)`
> Suppress: add `featureNewThing:fixAMMv1_1` to
> `.github/amendment-reviewer/reviewed.yml`
>
> ```mermaid
> graph LR
>   featureNewThing -->|calls, ungated| ammWithdrawImpl
>   fixAMMv1_1 -->|changes rounding of| ammWithdrawImpl
>   fixAMMv1_1 -.lineage.-> featureAMM
> ```
>
> <sub>graph: 1 edge added · rules fired: R2, R3 · model pinned · full report in artifact</sub>

Noise controls: confidence thresholds (AI findings < 0.5 dropped, < 0.7 shown as
`info`; deterministic findings bypass them); the suppression allowlist
(`reviewed.yml`, `{a, b, reason, pr}` entries); report only changed findings on
later pushes; and a hard cap of ~7 rendered findings with AI-raised findings
capped at `warning`.

---

## 9. Full rule catalog

**D** — fully deterministic: the static layer decides, and the AI only explains.
**D+AI** — the static layer proposes candidates and the AI confirms or rejects them.
**AI** — only detectable by reading meaning.

| ID  | Rule                                                                                                                                                                                                               | Kind | Severity     | Ships |
| --- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ---- | ------------ | ----- |
| R1  | New amendment-gated code has no test for the amendment **combination** (no `amendmentCombinations({new, neighbor})` and no `features - x` pairing) for any neighbor in the graph                                   | D+AI | warning      | v1    |
| R2  | New or changed code calls a function whose behavior differs under an amendment the caller never checks, and no gate covers the call site — **the fixAMMClawbackRounding case, and the reason the AI layer exists** | D+AI | warning      | v1    |
| R3  | A `Supported::Yes → Supported::No` change, or removal of a non-retired entry, in `features.macro`                                                                                                                  | D    | blocker      | v1    |
| R4  | Transaction-processing logic changed with no amendment gate covering the changed lines (a CONTRIBUTING violation). Produces too many false positives without AI filtering out refactors and moves                  | D+AI | warning      | v1    |
| R5  | Coupled-set violation: an amendment added to one member of a declared `gate-group` but not its counterpart (seeded by the `Rules.cpp:42-79` invariant)                                                             | D    | blocker      | v1    |
| R6  | The diff changes code inside a gate for a **mainnet-enabled** amendment (a replay hazard). AI decides whether the change is semantic or cosmetic; gates on not-yet-active amendments are `info`                    | D+AI | blocker/info | v1    |
| R7  | A new `fix*` amendment whose base feature isn't live on mainnet yet ("consider folding it into the base" — the base may already be voting, so this is only `info`)                                                 | D    | info         | v2    |
| R8  | A new amendment with no XLS spec referenced in the PR description                                                                                                                                                  | D+AI | info         | v2    |
| R9  | An OR-gate's membership changed (the `Transactor.cpp:722` pattern) — does "earliest activation wins" still hold, and were the comments and tests updated?                                                          | D+AI | warning      | v2    |
| R10 | A negated guard flipped: `!enabled(x)` added or removed around changed logic                                                                                                                                       | D+AI | warning      | v2    |
| R11 | An identifier gated in code but missing from `features.macro`, or registered but never gated anywhere (a dead amendment or typo)                                                                                   | D    | warning      | v1    |
| R12 | A test toggles `features - featureX`, but the code under test also branches on a lineage-linked fix the test never toggles                                                                                         | D+AI | info         | v2    |
| R13 | A retired (`[[deprecated]]`) amendment referenced in new code, surfaced with graph context beyond the compiler warning                                                                                             | D    | warning      | v2    |

---

## 10. File layout and phased build

```
.github/scripts/amendment-graph/
  README.md                       # what/why, regeneration instructions, finding taxonomy
  generate.py                     # parse features.macro → scan → build → canonical write (to workspace, not committed)
  check.py                        # regen head+base → classify delta → findings.json; gate-group invariants
  refresh_network_status.py       # feature-RPC snapshot normalizer (cron only)
  amendment_graph/                # shared lib: macro_parse, scanner, exprparse, model
  schema/graph.schema.json
  schema/findings.schema.json
  amendment-deps.yml
  eval/cases/<case>/{pr.diff, graph.json, network-status.json, expected.yml}
  results/
    network-status.json           # committed (external ground truth), refreshed by cron PR
  # graph.json / edges.txt are NOT committed — regenerated per run for head and
  # base into the job workspace/artifact (canonical, human-readable respectively)
.github/amendment-reviewer/
  reviewed.yml                    # suppression allowlist / KB seed
  priors.md                       # curated few-shot historical bugs
.github/workflows/
  reusable-check-amendment-graph.yml       # wired into on-pr.yml behind should-run
  amendment-ai-review.yml                  # privileged workflow_run job
  refresh-amendment-network-status.yml     # weekly cron → automated PR
```

**Phase v1 (weeks):** parse the macro; scan with comments and strings stripped
and names anchored; classify AND/OR/negation expressions; attribute matches to
functions; add weak co-function edges; derive lineage from names; build the
canonical graph for both head and base; diff and emit `findings.json`; set up
the network-snapshot cron; implement rules R1–R6 and R11; build the context
packer and single-call synthesis; split the workflows; and post the sticky
comment with the mermaid diagram — plus the golden-set harness and its recall
gate. _Exit criterion:_ a head-vs-base run on a no-op PR produces an empty diff,
and the example sites (`AMMWithdraw.cpp:439` AND-pair, `Transactor.cpp:722-724`
OR-triple, `EscrowCreate.cpp:116` negated pair) show up with the right edge
types.

**Phase v2 (signal quality):** the test-scope scanner; `amendment-deps.yml`
validation with hard gate-group checks; hub suppression; relocation
classification; rules R7–R10 and R12–R13; the knowledge base; the blast-radius
report; and Stage 2 verification if the evaluation calls for it.

**Phase v3 (accuracy):** a libclang extractor behind the same schema —
following data flow through boolean locals (the `Rules.cpp:44-49` pattern the
text scanner can't see) and one hop through gate-wrapping helpers — while the
text scanner stays as the fast local path, with a CI check that the AST results
include everything the regex found.

---

## 11. Evaluation harness (golden-set backtest)

Each case in `eval/cases/<case>/` contains: `pr.diff` (the PR that
**introduced** the bug, not the one that fixed it); `graph.json` and
`network-status.json` reconstructed at that PR's merge-base (the extractor
rebuilds them from the historical `features.macro`, which doubles as a
regression test for the static layer); and `expected.yml` (`must_flag` /
`must_not_flag`).

At least 6 seed positives:

1. The AMMClawback-introducing PR → must flag R2 against `fixAMMv1_1` (ground
   truth: `fixAMMClawbackRounding` #5513 later existed).
2. The pre-#5032 offer-path change (#4937) → must flag R2/R12 against
   `fixReducedOffersV1` (ground truth: `fixReducedOffersV2`).
3. NFT × trustline (pre-`fixEnforceNFTokenTrustline`, plus the V2/#5297
   deep-freeze follow-up) → cross-domain edge test.
4. Freeze × AMM (pre-`fixFrozenLPTokenTransfer`).
5. Synthetic: edit the `Rules.cpp` mantissa list without `useRulesGuards` → must
   flag R5.
6. Synthetic: flip a `Supported::Yes` to `No` → must flag R3.

At least 10 negatives — real merged PRs that touched amendment-gated code and
shipped cleanly (refactors near transactors, test-only PRs, a clean new-feature
PR). `must_not_flag: severity >= warning`.
