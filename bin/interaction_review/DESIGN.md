# Feature-Interaction Dependency Graph — Phase 1 Design

## Purpose

This tool is Phase 1 of a PR-review assistant that catches **untested feature-interaction
bugs** in rippled — the class of bug where two features share a code path and the boundary
between them is untested (e.g. a wrapper transaction × account-authorization consent
bypass). The full tool has three components:

- **A. Graph/registry** — a machine-readable model of which features interact, through
  which shared resources (this phase).
- **B. PR mapping + test locator** — map a PR diff onto graph nodes and find the tests
  covering each affected interaction (later phase).
- **C. LLM test-sufficiency grader** — judge whether those tests actually exercise the
  boundary (later phase).

**Phase 1 scope:** construct the interaction graph and enumerate the pairwise interaction
set. No PR diffing, no test location, no LLM. The output is a validated JSON artifact
(`graph.json` + `interactions.json`) describing feature nodes, resource nodes, edges, and
interactions — the substrate every later phase consumes.

## Graph model

### Feature nodes

- **Transactors** — one node per `TRANSACTION(...)` entry in
  `include/xrpl/protocol/detail/transactions.macro` (currently 82), carrying
  `{delegable, amendment, privileges_bitfield, fields[]}` from the macro columns.
- **Amendments** — one node per active `XRPL_FEATURE`/`XRPL_FIX` entry in
  `include/xrpl/protocol/detail/features.macro` (currently 49: 23 `FEATURE` + 26 `FIX`).
  `XRPL_RETIRE_*` entries and commented-out lines are skipped. Note the `XRPL_FIX`
  invocations are space-aligned (`XRPL_FIX    (Name, ...)`), so any counting regex must
  allow whitespace before the paren.

Counts drift as the codebase evolves; tests derive expected counts from the macro files
rather than hardcoding them.

### Resource nodes

Shared components that multiple features touch. Three kinds:

1. **Base-pipeline forks** (high signal) — shared `Transactor` functions that branch on a
   cross-cutting input. Extracted from the AST of `src/libxrpl/tx/Transactor.cpp` (via
   libclang), not hand-declared. Keyed by function name (overloads such as the two
   `checkSign`s merge into one resource; "family" labels like _auth_ or _sequencing_ are
   display-only metadata, never keys). Each fork resource carries:
   - **Lever fields** — SFields from the common-field set referenced anywhere in the
     function body.
   - **Lever flags** — transaction flag constants (`tf*`) tested in the body.
   - **Amendment gates** — amendments the body branches on directly.
   - **State space** — the enum of runtime outcomes where one exists
     (`FeePayerType` for the fee-payer fork, `SeqProxy::Type` for sequencing).
2. **Invariants** (medium signal) — one node per privilege bit in
   `enum Privilege` (`include/xrpl/tx/invariants/InvariantCheckPrivilege.h`, currently 14
   bits, excluding `NoPriv`).
3. **Shared per-tx SFields** (low signal) — non-common fields declared by ≥2 transactors.

### Edges

| Edge kind  | From → To                                | Source                                                                                                                                                                                                                                                                                           |
| ---------- | ---------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `consumer` | transactor → fork resource               | every transactor flows through each base fork                                                                                                                                                                                                                                                    |
| `consumer` | transactor → invariant                   | per-tx privilege bits from the macro                                                                                                                                                                                                                                                             |
| `consumer` | transactor → shared SField               | field lists from the macro                                                                                                                                                                                                                                                                       |
| `mediator` | feature → fork resource                  | (a) a lever field's governing feature, via `config/field_to_amendment.yml`; (b) a lever flag's governing feature, via `config/flag_to_amendment.yml`; (c) **harvested amendment gates** — a `rules().enabled(featureX)` branch inside the fork body directly names its mediator, no table needed |
| `wrapper`  | wrapper transactor → every fork resource | a transactor whose fields include `sfRawTransactions` (Batch) wraps inner transactions and therefore mediates every base fork; treated as a mediator for interaction purposes                                                                                                                    |

Some governing features are retired amendments or pre-amendment core behavior
(`MultiSign`, `TicketBatch`, `NetworkID`). The config tables accept a `core` sentinel for
these; a `core`-mediated lever still produces a resource and consumer edges, but no
active-amendment mediator edge.

### Interactions

An **interaction** is a pair of features that are common neighbors of a resource node,
annotated with `{resource, kind, boundary_states}`.

- Only `mediator×mediator` and `mediator×consumer` pairs are emitted.
  **`consumer×consumer` pairs on fork resources are excluded by construction** — every
  transactor consumes every fork, so those pairs are vacuous (~3.3k pairs per resource).
- Shared-SField resources have only consumer edges; their pairs are emitted but tagged
  `signal: low` so downstream phases can filter.
- Invariant resources emit `consumer×consumer` pairs tagged `signal: medium` (sharing a
  privilege bit is a meaningful, bounded relation — typically a handful of transactors
  per bit).

The acceptance example: the wrapper transactor (Batch) and the delegation amendment are
both mediators of the fee-payer/auth forks, so their pair falls out as
`mediator×mediator` on `getFeePayer` — the exact pair whose missing boundary test caused
the motivating bug.

## Extraction rules (fork extractor)

The load-bearing detail. For each discovered fork function body (see _Fork discovery_
below):

- **Lever fields:** collect every `DeclRefExpr` to an `sf*` protocol field, intersect
  with the common-field set (parsed from `TxFormats::getCommonFields()` in
  `src/libxrpl/protocol/TxFormats.cpp`, currently 20 fields). This deliberately does
  **not** enumerate access idioms — `isFieldPresent(sfX)`, `tx[sfX]`, `getFieldVL(sfX)`,
  `getAccountID(sfX)`, `getFieldArray(sfX)`, `getFieldObject(sfX)` all reduce to a
  `DeclRefExpr` on the field constant, so the reference-based rule is both simpler and
  more complete than pattern-matching call shapes.
- **Lever flags:** collect `DeclRefExpr`s to `tf*` flag constants (e.g. the
  inner-batch skip in `preflight2` branches on `isFlag(tfInnerBatchTxn)`, which no
  SField-based rule can see).
- **Amendment gates:** collect `DeclRefExpr`s to `feature*`/`fix*` amendment constants
  (e.g. `checkSign` branches on `featureLendingProtocol`, `featureBatchV1_1`, and
  `fixCleanup3_3_0` for the pseudo-account signing check). Each becomes an automatic
  mediator edge — these surface interactions (like LendingProtocol × signing) that no
  field table could, and they cross-validate the manual tables.
- A function with at least one lever field, lever flag, or amendment gate becomes a
  fork resource node.

### Fork discovery

Fork functions are **auto-discovered, not hand-listed** — honoring the design's
"derive, don't declare" principle and closing the recall gap where a new fork would be
silently missed. A function definition is in scope if it lives in the
`src/libxrpl/tx/Transactor.cpp` translation unit itself, or in a repo `/helpers/` header
it pulls in (e.g. `include/xrpl/ledger/helpers/SponsorHelpers.h`; bounded to the repo
root so a same-named third-party directory can't leak in). Any in-scope function whose
body references a common field, flag, or amendment gate becomes a fork; overloads
sharing a name (e.g. the two `checkSign`s) merge into one resource.

This currently discovers ~24 forks — including ones an earlier hand-maintained list
missed, such as `calculateBaseFee` (branches on sponsorship) and `preflightUniversal`
(gates on `fixCleanup3_2_0`). Core-only pipeline functions (`apply`, `operator()`,
`reset`) are included too; they carry no mediator edge (their fields are all `core`) and
only contribute Batch-wrapper interaction surface, which is legitimate.

Three loud guards replace what the hand-list used to protect: (1) a small
`_REQUIRED_FORKS` **anchor set** — the forks this design names by identity (`getFeePayer`,
`checkSeqProxy`, `consumeSeqProxy`, `checkSign`, `preflight2`) must all be discovered, so
a high-value fork vanishing via rename/relocation fails loudly rather than slipping
through as one absent entry among many; (2) a minimum-fork floor (a broken parse or wrong
scope yields almost nothing); (3) `FORK_STATE_ENUM` validation (a key must be a discovered
fork and its enum must resolve). The anchor set is a tripwire, not the discovery source —
it stays small and does not reintroduce the manual-scan burden.

### Known blind spots

- **Context-parameter forks:** a fork conditioned purely on a context parameter (e.g.
  `parentBatchId` in `checkSign`) has no SField/flag/amendment token to collect. The
  wrapper edge covers the known instances (all batch-inner-transaction paths) and the
  flag/amendment harvesting covers adjacent branches, but a new context-parameter fork
  with no token of any kind would be missed.
- **Out-of-scope forks:** `STObject::getInitiator` forks on `sfDelegate` but lives in a
  different TU (`STObject.cpp`); it is not scanned, and its `sfDelegate` fork is already
  represented via its callers (`getFeePayer` references `sfDelegate` directly). A shared
  fork that migrates outside Transactor.cpp / `/helpers/` would need its file added to
  the scan scope.

## Architecture

New Python CLI tool at `bin/interaction_review/` (dev-utility convention, like
`bin/pre-commit/`), with hash-locked requirements following the codegen convention
(`requirements.in` → `uv pip compile`).

```
bin/interaction_review/
  README.md
  DESIGN.md                     # this document
  requirements.in / .txt        # pcpp, pyparsing, libclang (hash-locked)
  build_graph.py                # CLI entrypoint
  macro_extractor.py            # .macro files -> feature nodes, macro edges, shared-SField resources
  common_fields.py              # TxFormats::getCommonFields() -> cross-cutting field set
  fork_extractor.py             # libclang AST over Transactor.cpp -> fork resources
  privilege_extractor.py        # Privilege enum + per-tx bits -> invariant resources + edges
  graph.py                      # dataclasses (FeatureNode/ResourceNode/Edge), GraphBuilder
  graph.schema.json             # JSON Schema for output validation
  interactions.py               # common-neighbor enumeration -> interaction set
  config/
    field_to_amendment.yml      # common field -> governing feature | core
    flag_to_amendment.yml       # lever flag   -> governing feature
    gate_allowlist.yml          # tolerated unresolved amendment-gate globals
  tests/                        # unit tests + acceptance assertions
```

### Reused code

- **TRANSACTION macro parsing:** `cmake/scripts/codegen/generate_tx_classes.py` provides
  `create_transaction_parser()`, `parse_transaction_args()` (expects ≥7 args), and
  `parse_macro_file()`. Import these rather than re-implementing. Because
  `parse_macro_file` runs the file through pcpp (`CppCleaner` in
  `macro_parser_common.py`), `#if TRANSACTION_INCLUDE` blocks, `//`-commented-out macro
  calls, and multi-line `({...})` field blocks are handled upstream — do not hand-roll
  parsers for those cases.
- **Field-list utilities:** `cmake/scripts/codegen/macro_parser_common.py`
  (`parse_field_list`, `parse_sfields_macro`).
- **Build-dir discovery** (`.build` → `build`) and the compile-DB path-canonicalization
  caveat (per-module header symlinks): follow `bin/pre-commit/clang_tidy_check.py:49-54`.
- `features.macro` has no existing parser; a small regex (skip `XRPL_RETIRE_*` and
  comment lines) suffices.

### Dependencies

`pcpp`, `pyparsing`, `PyYAML`, `jsonschema`, and the `libclang` Python bindings. The
bindings talk to a native dylib via ctypes and are forward-compatible with a newer dylib
for the AST nodes used here (`DECL_REF_EXPR`, `ENUM_DECL`, function decls); the PyPI
`libclang` package currently tops out at 18.1.1 while the toolchain dylib is LLVM 22, and
that combination parses cleanly. `fork_extractor.py` calls
`clang.cindex.Config.set_library_file()` (default
`/opt/homebrew/opt/llvm/lib/libclang.dylib`, `--libclang` to override) and passes
`-resource-dir <llvm>/lib/clang/<major>` derived from the dylib path. The resource dir is
load-bearing: without it the parse cannot find clang builtin headers (`stddef.h`, etc.),
the AST silently degrades to recovery types, and the `rules().enabled(featureX)` gate
references vanish. The extractor treats any error-level diagnostic as fatal for exactly
this reason.

## Pipeline

`python bin/interaction_review/build_graph.py --build-dir .build --out out/`

1. **Macro extraction** (`macro_extractor.py`):
   - `transactions.macro` → transactor feature nodes with delegability, gating
     amendment, privilege bitfield, and field list. The reused pcpp-based parser handles
     the macro edge cases (empty `({})` field blocks such as `DIDDelete`, `#if
TRANSACTION_INCLUDE` blocks, commented-out calls); an unrecognized delegability
     column fails loudly.
   - `features.macro` → active amendment nodes.
   - Wrapper detection: any transactor whose fields contain `sfRawTransactions` →
     `wrapper: true`.
   - Shared per-tx SField resources: non-common fields on ≥2 transactors → resource node
     - consumer edges, `signal: low`.
2. **Common-field set** (`common_fields.py`): parse the `kCommonFields` vector in
   `TxFormats::getCommonFields()`.
3. **Fork extraction** (`fork_extractor.py`): load Transactor.cpp's compile args from
   `compile_commands.json`, parse the TU with libclang, and auto-discover fork functions
   (see _Fork discovery_), applying the extraction rules above. Capture state-space
   enums: `FeePayerType` (`include/xrpl/tx/Transactor.h:135`) and `SeqProxy::Type`
   (`include/xrpl/protocol/SeqProxy.h`).
4. **Privilege extraction** (`privilege_extractor.py`): parse `enum Privilege` →
   invariant resource nodes; per-transactor privilege bitfields → consumer edges,
   `signal: medium`.
5. **Merge + edge synthesis** (`graph.py`):
   - Load both config tables; emit mediator edges for lever fields and lever flags.
   - Emit harvested-amendment-gate mediator edges directly from fork-extractor output.
   - Emit consumer edges (every transactor → every fork resource) and wrapper edges.
   - Union everything; validate against `graph.schema.json`. **Fail loudly** on: unknown
     node kind, dangling edge (including a config table naming a feature that is neither
     an active amendment nor `core`), or a discovered lever field/flag with no config
     entry — the last is the recall guardrail that turns silent gaps into build failures.
6. **Interaction enumeration** (`interactions.py`): per resource, enumerate common
   neighbors under the kind rules above; attach boundary states; write
   `interactions.json`.
7. Write `graph.json` + `interactions.json` to `--out` (ephemeral; never committed).

## Manual configuration (the only human-maintained inputs)

`config/field_to_amendment.yml` — cross-cutting field → governing feature. It covers the
**entire** common-field set (20 fields), because the fork scan discovers every
common-field lever, not only the governed ones, and the recall guardrail requires an
entry for each discovered lever. Governed fields map to an amendment; the rest map to the
`core` sentinel (base protocol behavior, no mediator edge). Amendment names must match
`features.macro` exactly:

```yaml
sfSponsor: Sponsor
sfSponsorFlags: Sponsor
sfSponsorSignature: Sponsor
sfDelegate: PermissionDelegationV1_1
sfTicketSequence: core # Tickets (TicketBatch retired)
sfSigners: core # MultiSign retired
sfNetworkID: core
# ... plus the remaining common fields (sfAccount, sfFee, sfFlags, ...), all core
```

Only common fields appear as levers (the scan intersects references with the common
set), so `sfSignerListID`/`sfSignerQuorum` — which are not common fields — are not in the
table.

`config/flag_to_amendment.yml` — lever flag → governing feature:

```yaml
tfInnerBatchTxn: BatchV1_1
```

`config/gate_allowlist.yml` — a (currently empty) list of amendment-gate globals
(`feature*`/`fix*`) that a fork body may reference but that resolve to no active amendment,
e.g. an amendment retired while its transition check still lives in the C++. An
unresolved gate not listed here fails the build.

The build fails if the AST scan discovers a lever with no table entry, if a table names a
non-active amendment, or if a fork references an unresolved, non-allowlisted gate — so
these inputs can only grow loudly, never rot silently.

## Verification

Run `build_graph.py` against the current checkout, then assert:

1. **Counts** (derived, not hardcoded): transactor nodes == `TRANSACTION(` entries in
   transactions.macro; amendment nodes == active `XRPL_FEATURE`/`XRPL_FIX` entries in
   features.macro; invariant nodes == non-`NoPriv` bits in `enum Privilege`.
   (At time of writing: 82 / 49 / 14.)
2. **Fork resource sanity** (superset assertions — lever sets may grow):
   - `getFeePayer` exists with lever fields ⊇ `{sfSponsor, sfSponsorSignature,
sfDelegate}` and state space `{Account, Delegate, SponsorCoSigned,
SponsorPreFunded}`.
   - `checkSeqProxy` exists with lever fields ⊇ `{sfTicketSequence}` and states
     `{Seq, Ticket}`.
   - `checkSign` exists with lever fields ⊇ `{sfSigners, sfDelegate}` and amendment
     gates ⊇ `{BatchV1_1, LendingProtocol}`.
   - `preflight2` exists with lever flags ⊇ `{tfInnerBatchTxn}` (flag harvesting works
     where field harvesting can't).
   - Auto-discovery finds forks beyond any hand-list — e.g. `calculateBaseFee` (sponsor
     lever) — and the discovered count is within a plausible band (currently 24).
3. **Batch:** node has `delegable: NotDelegable`, `wrapper: true`, amendment
   `BatchV1_1`, and wrapper edges to every fork resource.
4. **Acceptance interaction:** `interactions.json` contains the
   Batch × PermissionDelegationV1_1 pair on the `getFeePayer` resource as
   `mediator×mediator` (Batch via the wrapper edge, PermissionDelegationV1_1 via the
   `sfDelegate` lever) — the Phase-1 proxy for the recall requirement.
5. **Guardrails:** `graph.json` validates against the schema; deleting the `sfDelegate`
   row from `field_to_amendment.yml` makes the run fail; adding a bogus feature name to
   a table makes the run fail (dangling edge).
6. **Unit tests** (`tests/`): macro edge cases (empty `({})`, space-aligned `XRPL_FIX`,
   comment/RETIRE rejection); fork extractor against a small fixture TU covering each
   access idiom (`isFieldPresent`, subscript, `getFieldVL`, `getAccountID`, `isFlag`,
   `rules().enabled`); interaction enumeration on a toy graph including the
   consumer×consumer exclusion; the recall guardrails (missing lever, dangling amendment,
   unresolved gate, missing fork). Count assertions derive the expected number
   independently from the source files rather than hardcoding it.
7. **End-to-end:** `pip install -r requirements.txt && python
bin/interaction_review/build_graph.py --build-dir .build --out /tmp/out` completes
   and produces non-empty `graph.json` + `interactions.json`.

## Out of scope (later phases)

- Base-vs-head graph diffing and touched-node resolution (Component B).
- Test location and test-sufficiency grading (Components B/C).
- The GitHub Actions `issue_comment` slash-command workflow.
- Owner-directory resource family.
- Multi-TU fork scanning (only needed if a fork migrates out of Transactor.cpp's TU).
