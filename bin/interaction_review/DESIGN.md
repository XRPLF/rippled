# Interaction review design

## Goal

Cross-feature bugs are often missed by file-level review because the affected
features meet in shared signing, authorization, sequencing, fee, or invariant
logic. This tool narrows that problem in three stages:

1. Build a graph of features and shared protocol decisions.
2. Map a change to the graph and select a small set of feature boundaries.
3. Give all relevant locations for each boundary to one bounded model
   conversation, which reviews behavior and tests.

Static analysis determines scope. The model interprets code only within that
scope. This split keeps candidate selection reproducible while allowing the
final review to follow behavior across files.

See [README.md](README.md) for commands and
[PROTOTYPE_RESULTS.md](PROTOTYPE_RESULTS.md) for recorded results.

## Graph model

### Features

The graph has two feature types:

- **Transactors**, extracted from `transactions.macro`, including their fields,
  amendment, delegability, and invariant privileges.
- **Active amendments**, extracted from `features.macro`. Retired and commented
  entries are excluded.

Counts are derived from the source macros in tests rather than fixed in code.

### Shared resources

Resources represent code or state used by more than one feature:

- **Base-pipeline forks** are functions in the `Transactor.cpp` translation
  unit or repository helper headers that branch on common transaction fields,
  transaction flags, or amendment gates. These are the strongest signals.
- **Invariant privileges** are the individual bits in `enum Privilege` and the
  `hasPrivilege` calls that enforce them.
- **Shared transaction fields** are non-common fields declared by at least two
  transactors. These are useful but low-signal.

Forks are discovered from the libclang AST rather than maintained in a list.
Overloads with the same name become one resource. A small required-fork set,
minimum count, state-enum checks, and fatal parser diagnostics detect extraction
failures without making the check list the source of truth.

AST references are collected by symbol, so field access through subscripts,
`isFieldPresent`, or typed getters is handled uniformly. Amendment checks and
transaction flags are collected the same way. Known state enums, such as
`FeePayerType` and `SeqProxy::Type`, provide the boundary states a reviewer
should consider.

### Edges and interactions

| Edge       | Meaning                                                                             |
| ---------- | ----------------------------------------------------------------------------------- |
| `consumer` | A transactor passes through a fork, uses a privilege, or declares a shared field    |
| `mediator` | An amendment controls a field, flag, or gate used by a fork                         |
| `wrapper`  | A wrapper transaction, currently Batch, sends inner transactions through base forks |

An interaction is a pair of features connected through one resource. Fork
resources emit mediator-to-mediator and mediator-to-consumer pairs; generic
consumer-to-consumer fork pairs are excluded because every transactor uses the
base pipeline. Invariant consumer pairs are retained because privilege sets are
small and meaningful. Shared-field pairs remain available with low signal.

Mappings that cannot be inferred safely live in four checked configuration
files:

- `field_to_amendment.yml` maps common fields to an amendment or `core`;
- `flag_to_amendment.yml` maps transaction flags to amendments;
- `gate_allowlist.yml` permits known unresolved amendment globals;
- `transactor_impl_overrides.yml` supplies implementation paths only when path
  derivation fails.

Missing mappings, inactive features, unresolved gates, dangling edges, and
invalid source locations fail the build. This makes changes to protocol levers
explicit.

## Diff mapping and selection

Every graph node records where it came from: a macro row, C++ definition,
privilege reference, state enum, or attributed implementation file.
`pr_map.py` intersects a Git diff with these spans. Precise span hits are kept
separate from whole-file implementation hits. Changes in a known file but
outside every span are reported as context rather than assigned to an unrelated
node. Renames, deletions, and mode-only changes are reported as structural
changes.

`select_interactions.py` joins the touched nodes with the interaction set. A
candidate enters when the diff touches its shared resource or either endpoint.
Selection then:

- gives more weight to precise resource changes, mediator relationships, both
  endpoints changing, known state spaces, and newly introduced lever tokens;
- removes weak shared-field pairs unless the field or both endpoints changed;
- summarizes generic pass-through cohorts when a base decision changes; and
- applies global and per-resource limits while keeping the strongest resources
  intact.

Scores are ordinal routing signals, not probabilities. The complete candidate
set and filtering metrics remain in `selected.json` for audit.

### Why one cluster per boundary

The same feature pair can meet at several shared functions. Reviewing each
function independently produced duplicated and sometimes contradictory model
answers. `review_clusters.py` therefore groups every retained location for a
canonical feature pair into one investigation. The best eligible location sets
the cluster's budget rank, but all retained locations are available to the
model.

Invariant authorization is grouped as one rule and its authorized holders,
rather than fabricated pair rows. This preserves singleton privileges and lets
evaluation distinguish routing to the rule from discovery of broader semantic
context.

The PR comment intentionally omits static intersections, ranks, and per-location
roles. They explain why a cluster was chosen but are not useful as review
findings. `selected.json` and `judged.json` retain them for debugging and
evaluation.

## Model review

Each eligible cluster gets one conversation. The model can follow the diff,
shared functions, endpoint implementations, and relevant tests, then submits:

- behavior: `broken`, `correct`, or `unclear`;
- coverage: `covered`, `missing`, or `unclear`;
- one primary location for a conclusive behavior result;
- a role for every supplied location: `decisive`, `supporting`,
  `not_relevant`, or `unresolved`; and
- source citations and a concise explanation.

Behavior and coverage are independent. A missing test does not prove broken
behavior, and correct-looking behavior without a test is not reported as fully
handled. The displayed verdict is derived in code:

1. broken behavior becomes `gap`;
2. otherwise missing coverage becomes `coverage_gap`;
3. correct behavior with covered tests becomes `handled`;
4. every other result becomes `unclear`.

The prompt does not include static scores or expected results. The graph chooses
what to review; the model decides what the selected code means.

### Source and evidence boundary

The model receives three read-only tools: `read_file`, `grep`, and `git_diff`.
The boundary is enforced as follows:

1. **Revision binding.** A committed head is read from Git objects. A worktree
   run binds the checkout commit and tracked diff and checks for drift around
   model-visible operations.
2. **Source-only access.** Only tracked files under `src/`, `include/`, `test/`,
   and `tests/` are readable. Git metadata, `cfg/`, credentials, review code,
   outputs, corpus definitions, and expected results are unavailable.
3. **Observed citations.** The evidence trace records line spans shown by tool
   results. A cited line must resolve in the bound revision and must have been
   observed. Unsupported behavior and coverage claims are independently reduced
   to `unclear`.
4. **Bounded failure.** Cluster count, turns, and tool-result size are limited.
   Errors produce diagnostics and an unjudged cluster rather than a fabricated
   conclusion.

Repository text is treated as evidence, never as instructions. Git textconv and
diff drivers are disabled for model-visible reads. The judge writes structured
data only; rendering and PR posting are separate steps.

`judged.json` records the exact selection hash, source fingerprint, prompt hash,
verdict-schema hash, location roles, tool trail, token usage, and hashed evidence
trace. This prevents a verdict from being silently attached to a different
selection or revision.

There is a per-cluster turn ceiling, but no hard run-level token, dollar, or
wall-clock budget. Production use should add those controls.

## Evaluation and replay

`evaluate_run.py` measures deterministic routing and optional model results
against a separate expected-results file. The main routing unit is the
feature-pair cluster:

- cluster rank, Recall@K, and mean reciprocal rank;
- behavior and coverage agreement;
- primary-location accuracy and relevant-location precision/recall; and
- evidence validity, turns, and tokens.

Earlier resource-row metrics are retained under `legacy_resource_rows` only for
comparison. Canonical feature IDs avoid collisions between amendments and
transactors with the same display name. `match_mode=invariant_route` records
that the graph found an authorization rule without claiming it statically found
semantic context discovered later by the model.

The seven-case corpus uses source-only reverse mutations of five historical
fixes and two behavior-preserving controls. Each case runs in a detached,
disposable worktree. Mutations are count-checked, fixed regression tests remain
present, and expected results are generated outside the model-readable tree.
This tests routing and judging separately without exposing the answer in the
prompt.

The corpus demonstrates the workflow; it is not an accuracy benchmark. Cases
and controls are visible, one prompt was adjusted after a visible miss, and the
sample is small. A production evaluation needs held-out cases, more clean
changes, repeated trials, semantic citation grading, calibration of `unclear`,
latency and cost distributions, and a graph-context versus bare-diff baseline.

## CI trust boundary

The analysis workflow checks out and processes PR code with no repository write
permission. A separate `workflow_run` job downloads the generated artifact and
posts the comment without checking out PR code.

This separation protects the comment token, but it does not make model
credentials safe from PR-controlled execution. The analysis job configures,
builds, and installs the PR before assuming its AWS role. Forced runs on
untrusted fork heads must not receive credentials until the judge and its
dependencies execute from a base-pinned, privileged job that treats the PR as
data. Any current role should be restricted to the required model endpoint.

## Known gaps

- Fork discovery is limited to the `Transactor.cpp` translation unit and
  included repository helper headers. A branch based only on an untracked
  context parameter can be missed unless another lever or wrapper edge exposes
  it.
- Payment-path internals, broader ledger helpers, and invariants without mapped
  privilege bits are not yet modeled.
- Static mapping has no test nodes. Test sufficiency is assessed only during the
  model pass.
- High-fanout generic forks can retain the correct candidate below a practical
  review budget when the diff has no endpoint-specific signal.
- Forced-run credential isolation, hard cost limits, and broader held-out
  evaluation are required before production rollout.
