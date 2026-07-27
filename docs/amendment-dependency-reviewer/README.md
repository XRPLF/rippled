# Design: Global-Feature Invariant Review

**Status:** Draft for review
**Audience:** rippled maintainers and reviewers
**Scope:** This document defines the problem model, data model, and detection
architecture for catching the most severe class of protocol bugs: a _global
feature_ that violates a _cross-cutting invariant_.

_Note: Prototype configuration—model/provider parameters, CI wiring, file
layouts, and the exact property-test catalog—lives in the companion
[IMPLEMENTATION.md](./IMPLEMENTATION.md)._

---

## 1. Context and Motivation

Most XRPL transactions are _local_: they read and write a bounded set of ledger
objects and are authorized, paid for, and applied under assumptions that every
transactor shares:

- The submitting account signed it.
- The submitting account pays the fee.
- A failed transaction either applies-and-claims (`tec`) or does not apply at all.

These assumptions are not formally written down; they are baked into the base
`Transactor` and silently relied upon by all ~40 transaction types.

A **global feature** is an amendment that fundamentally breaks one of these
baked-in assumptions for every transactor at once:

| Global feature            | Assumption it breaks                                                                                                                                    |
| ------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Batch** (XLS-56)        | Inner transactions run unsigned, with `Fee=0`, under a shared outer account context. "The account signed it" and "the account paid" are no longer true. |
| **Delegation**            | The account that _signs_ is no longer the account the transaction _acts on_ (`sfDelegate`).                                                             |
| **Sponsor**               | The account that _pays_ the fee/reserve is no longer the sending account.                                                                               |
| **Pseudo-accounts** (AMM) | An account may have no signing keys at all and must never be authorized or deleted by normal paths.                                                     |

The danger isn't that these features are individually complex. The danger is that
they silently invalidate assumptions that dozens of unrelated code paths still
make.

### The Evidence: The Batch Fix Log

We can prove this by looking at the history of the Batch amendment. Every shipped
Batch fix—including the disclosed authorization-bypass CVE—maps directly to a
cross-cutting invariant being violated.

| Shipped Batch fix                                                                                                | Invariant violated                     |
| ---------------------------------------------------------------------------------------------------------------- | -------------------------------------- |
| `bind batch signer signatures to outer account and sequence`                                                     | **Sign** (replay / binding)            |
| CVE: premature success in the signer loop ([`Batch.cpp:452`](../../src/libxrpl/tx/transactors/system/Batch.cpp)) | **Sign** (authorization bypass)        |
| `bind required signers to sfDelegate for delegated inner txns`                                                   | **Sign × Delegation** (combinatorial)  |
| `Ensure delegate tests do not silently fail with batch`                                                          | **Batch × Delegation** (combinatorial) |
| `Sponsor fee not allowed on inner batch` (rollback)                                                              | **Fee × Sponsor** (combinatorial)      |
| `harden batch inner transactions (TxQ blocker)`                                                                  | **Fee** / queueing                     |
| `Reorder Batch Preflight Errors`                                                                                 | **Result semantics** (`tec` ordering)  |
| `skip inner batch txns during ledger replay`                                                                     | **Consensus** / replay                 |

**The design goal:** Make this implicit invariant matrix explicit and enforced.
When a PR touches a global feature, the system must force a decision and a test
for every cross-cutting invariant it could break—including how it composes with
existing global features—and surface gaps before merge.

---

## 2. The Model: The Invariant Matrix

The organizing artifact for this system is a two-dimensional matrix.

- **Rows (Global Features):** The set of features that break a baked-in
  assumption. Membership here is _declared, not inferred_ (§6). Making a feature
  "global" is a deliberate design decision.
- **Columns (Cross-Cutting Invariants):** The assumptions the base transactor
  enforces on behalf of all transaction types. The column set is derived
  _top-down from the base `Transactor`_—by enumerating every invariant its
  preflight → preclaim → doApply path enforces—**not** reverse-engineered from
  any one feature's failures. This is deliberate: a matrix whose columns are cut
  to fit Batch would silently pass the next feature that fails on an axis Batch
  never touched. Batch _populates_ the matrix; it does not _define_ its
  dimensions.
- **Cells (Audit Obligations):** A cell `(Feature, Invariant)` records how that
  feature preserves that invariant, and—crucially—links to the test(s) that
  prove it.
- **The completeness cell.** Every feature also carries one open obligation:
  _does this break an assumption not yet in the column set?_ It is the only
  defense against an unknown-column failure, and answering it can add a new
  column for every feature.

### Combinatorial Cells (Where Severity Concentrates)

The off-diagonal cells are where the most dangerous bugs live. For two global
features `F₁` and `F₂`, the cell `(F₁ × F₂, Invariant)` is a distinct obligation
because assumption-breaks stack.

For example, `Batch × Delegation` on the **Sign** axis (who-signs × who-acts) and
`Batch × Sponsor` on the **Fee** axis (who-pays × zero-fee-inner) both produced
shipped bugs. The matrix turns these "untested interactions" into first-class
requirements. A bug is simply a matrix cell that is **unfilled** (no code
decision/test) or **filled incorrectly**.

---

## 3. Detection Anchors: Guard Sites and Probes

To make the matrix machine-checkable, we anchor it to the codebase using two
mechanisms.

### 1. Guard Sites (Static)

Each invariant is enforced at a small, stable set of functions in the base
transactor (e.g., `Transactor::checkSign`, `Transactor::checkFee`).

- If a PR introduces a new global feature but does **not** touch the relevant
  guard site, it is highly suspicious—the invariant is being changed outside of
  where it is enforced.
- If a guard site grows a new branch keyed on a feature context (e.g.,
  `if (ctx.parentBatchId)`), a cell is being filled and must have an associated
  test.

### 2. Property Probes (Dynamic)

Each invariant has a machine-checkable property that must hold for any global
feature. These are the enforcement backbone. Two rules keep them from degrading
into Batch-shaped regression tests:

- **Probes encode the _contract_, not the _symptom_.** The Sign probe asserts
  "every consumed authority is authorized and bound to this transaction"—not "the
  signer loop does not return early." A probe cut to one bug's silhouette passes
  the next bug that violates the same contract a different way.
- **Each probe is validated against ≥2 distinct historical bugs** before it counts
  as covering its invariant—e.g., the Sign probe must reject the premature-return
  loop _and_ a signature-binding/replay bug _and_ a missing-signer-entry case.

The v1 column set, derived from the transactor contract (§2):

| Invariant             | Property probe (must pass for all global features & combinations)                                                                          |
| --------------------- | ------------------------------------------------------------------------------------------------------------------------------------------ |
| **Sign**              | Every consumed authority is authorized and bound to this transaction; forging or omitting _any single_ required party ⇒ rejection.         |
| **Fee**               | Total XRP debited across all payers equals the declared fee exactly; no payer is charged twice; no zero-fee path escapes justification.    |
| **Reserve**           | Owner-reserve is charged to and released from exactly the account that owns the object; no path creates an object without a reserve payer. |
| **Result semantics**  | For every reachable `tec`, the fee-charged / sequence-consumed / ledger-applied triple matches the base-transactor contract.               |
| **Sequence / Ticket** | Every applied transaction consumes exactly one sequence or ticket from the acting account; none is consumed twice or skipped.              |
| **Owner directory**   | Objects created or deleted are correctly linked/unlinked from their owner directories; no dangling or orphaned entries.                    |
| **Consensus safety**  | With the feature's amendment disabled, transaction results are byte-identical to the prior release.                                        |

---

## 4. Architecture: Three-Layer Verification

The system operates in three tiers. The bottom two are deterministic and can
block PRs; the top tier is AI-driven, advisory, and never blocks.

```
  LAYER 1: COVERAGE (Deterministic, Blocking)
  ┌───────────────────────────────────────────────────────────────┐
  │ Reads the declared matrix and the PR diff to ensure:          │
  │  • Does every implicated cell have a REQUIRED, PRESENT test?  │
  │  • Is every new behavioral branch amendment-gated?            │
  │  • Are there any Supported::Yes → No lifecycle reversions?    │
  │  [!] Missing cell test or ungated branch = BLOCK              │
  └───────────────────────────────┬───────────────────────────────┘
                                  │
  LAYER 2: PROPERTIES (Deterministic, Blocking)                    ▼
  ┌───────────────────────────────────────────────────────────────┐
  │ Runs the Property Probes (§3) for each implicated cell.       │
  │  [!] Any Sign/Fee/Result/Consensus property fails = BLOCK     │
  └───────────────────────────────┬───────────────────────────────┘
                                  │ inert findings (data only)
                                  ▼
  LAYER 3: AI REVIEW (Advisory, Never Blocks)
  ┌───────────────────────────────────────────────────────────────┐
  │ Scaffolded by the matrix, the LLM reasons cell-by-cell:       │
  │ "Does this code preserve <Invariant> given <Feature>?"        │
  │ Output: Sticky PR comments proposing missed edge cases.       │
  └───────────────────────────────────────────────────────────────┘
```

**Why the AI is scaffolded, not free-roaming:** A generic "find bugs in this
diff" prompt is noisy and prone to hallucination. Here, the AI is handed a
_specific cell_ and a _specific invariant_. Asking an LLM, "The Sign invariant
requires every required signer to be validated; does this loop return success
before all signers are matched?" yields highly accurate, falsifiable results.

---

## 5. Alternatives Considered

**Alternative 1: Structural Dependency Graph (`enabled(A) && enabled(B)`).** A
prior design attempted to catch bugs by mapping explicit boolean gates and code
lineage into a dependency graph.
_Why we rejected it as the primary model:_ Of the ~13 severe Batch fixes, none
were boolean-coupling edges—severe bugs cluster in semantic invariant violations,
not structural code proximity. The coupling graph addresses a different class
(e.g., `fixAMMv1_1` rounding reuse) whose severity has been lower _recently_, not
inherently. We therefore retain its one sound, cheap component—the deterministic
amendment-gating and `Supported::Yes → No` lifecycle checks—inside Layer 1 (§4),
and drop only the graph as the organizing model.

**Alternative 2: Implicit Global-Feature Inference.** We considered having the
system automatically infer when a feature becomes "global" by scanning for
widespread scope changes.
_Why we rejected it:_ What makes a feature "global" is a design fact, not a
syntactic one. No AST pass can decide that Batch "breaks the signing assumption."
This must be explicitly declared by a maintainer.

---

## 6. The Declared Registry

The matrix's rows and cells are declared in a maintained sidecar file (the
Registry), not inferred from source.

While manual registries can rot, this one is **enforced, not trusted.** The
registry is continuously cross-checked against the diff and the property probes.
If a feature is declared to touch **Sign** but the PR adds no test at the Sign
guard site, Layer 1 blocks the merge. The registry cannot silently drift out of
sync because the deterministic layers fail closed.

Crucially, adding a global feature **auto-expands the matrix**. Declaring `F_new`
forces the author to fill `(F_new × F_existing)` cells for every existing global
feature. It transforms tribal knowledge into a hard CI requirement.

---

## 7. Security and Scaling Limits

**Trust boundaries & prompt injection.** Layers 1 and 2 run in the untrusted zone
(processing attacker-controllable fork code) but execute no AI models. They output
schema-validated JSON. Layer 3 (the AI) runs in the trusted zone on base-branch
code and consumes the JSON as inert data. The worst case for a malicious PR is a
misleading AI comment—never code execution, exfiltration, or a spurious block.

**Cost bounding.** Layer 2 (executing property tests) is bounded by only running
the cells the diff explicitly implicates, rather than running the full
combinatorial matrix on every PR push.

**Context truncation.** If a massive refactor exceeds the AI's token limit, Layer
3 degrades gracefully: it processes cells in batches, and any cell that cannot fit
falls back to a deterministic warning flagged with "AI analysis unavailable for
this cell." We never silently truncate.

---

## 8. Validation and Launch

The global-feature model has a real-world corpus spanning multiple features: the
Batch fix log, Delegation fixes, Sponsor fixes, and AMM / pseudo-account
authorization history. The validation is explicitly structured to avoid _fitting
to Batch_, which is both the richest example and the easiest trap.

**Launch gates:**

- **Recall on Batch / Delegation:** Reconstruct historical fixes to their pre-fix
  states; the **Sign** probe must catch the CVE's premature-return loop and the
  combinatorial cells must catch the `Batch × Delegation` regressions.
- **Held-out recall (the decisive gate):** Author and tune the probes against
  Batch and Delegation _only_, then test cold against a feature never used in
  authoring—AMM/pseudo-account or AccountDelete history. Recall that survives on
  the held-out feature is the real measure of generalization; recall that holds
  only on the tuned features is overfitting, and does not ship.
- **Precision:** Zero blocking findings on clean, merged PRs that touched
  global-feature code safely.
- **Soft launch:** Run silently on merged PRs to audit LLM comment quality before
  enabling reviewer-facing visibility.

---

## 9. Open Questions for Reviewers

1. **Invariant set for v1.** The columns are derived top-down from the transactor
   (§2–3): {Sign, Fee, Reserve, Result-semantics, Sequence/Ticket, Owner-directory,
   Consensus}. Is that enumeration complete, and which columns are mature enough to
   _block_ on in v1 versus advise only?
2. **Registry ownership.** Who declares a feature "global" and owns its cells—the
   amendment author at design time, or a standing protocol-security reviewer?
3. **Blocking timing.** Should Layer 1 (coverage) and Layer 2 (properties) block
   merges on day one, or be advisory for one release cycle while the registry is
   backfilled?
4. **Property-probe authorship.** Are the per-invariant properties (§3)
   maintainer-authored once and instantiated per feature, or must each feature
   author write their own? (And how do we prevent weak self-authored tests from
   passing vacuously?)
5. **Combination scope.** Is full pairwise `F₁ × F₂` coverage tractable as the
   feature set grows, or do we need a declared "these two cannot co-occur" escape
   hatch to bound the matrix?
