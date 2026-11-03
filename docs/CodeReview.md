# Code review guide

This guide collects the 18 most critical review rules that come up on pull requests in this repository. It is written for both human reviewers and AI reviewers, and for authors who want to self-review before asking for a review. The rules were distilled from maintainer review comments on public `rippled` pull requests.

It complements, and does not replace, [CONTRIBUTING.md](../CONTRIBUTING.md) (process, contracts, tests, style guide) and [docs/CodingStyle.md](./CodingStyle.md) (formatting). Where a rule overlaps those documents, it links to the relevant section.

## How to use

- Apply the **Correctness** rules first. Ignoring one risks wrong ledger state, a wrong result, a security issue, or a bug reaching a release.
- Then apply the **Style and conventions** rules.
- Cite the rule number in a review comment, e.g. "CR-17", so the author can find the rule.
- Rules are numbered continuously through the document so that a number is stable and citable; the headings only group them by theme.
- A rule is a default, not a law: if a change deliberately departs from one, the PR should say why.

## Correctness

### Amendments and feature gating

- **CR-1.** Respect amendment state until formal retirement: always check it as if disabled, preserve pre-amendment behaviour even when wrong, honour pre-amendment objects, and when retiring remove only the gating path.

### Transactor structure and result codes

- **CR-2.** Put checks needing only transaction fields and Rules in preflight (tem codes); ledger-dependent or mutating checks go in preclaim or doApply; run universal preflight checks before transactor-specific ones.
- **CR-3.** Never throw on transaction data; return tecINTERNAL or tefINTERNAL for conditions earlier steps made impossible, keep logicError in destructors and consensus paths, and catch every throwing call.
- **CR-4.** Return the result code that describes the actual failure, propagate a helper's TER unchanged, tolerate only the specific expected error, and never let cleanup failures override the primary result.
- **CR-5.** Keep every transactor check and result code in sync with the XLS spec; when the spec omits a protocol invariant or is wrong, fix the spec rather than silently diverging.

### Ledger objects, fields and flags

- **CR-6.** Null-check every SLE from view.read or peek before dereferencing and return tefBAD_LEDGER or tefINTERNAL with LCOV_EXCL_LINE, even when a null is nearly impossible.
- **CR-7.** Call view.update(sle) once per modified SLE per transaction; its position among the modifications is irrelevant, but it must happen.

### Invariants, asserts and defensive checks

- **CR-8.** For impossible branches write `if (cond) { UNREACHABLE(...); return error; }` with LCOV exclusion; a bare XRPL_ASSERT or a silent tefINTERNAL alone is insufficient. See also [CONTRIBUTING.md](../CONTRIBUTING.md#contracts-and-instrumentation).
- **CR-9.** Invariants must hold when the transaction fails: never return true early on a non-tesSUCCESS result; instead verify no guarded objects were touched.

### Tests

- **CR-10.** After each transaction in a test, assert the resulting ledger state and balances in every branch, including absent objects, flags and transfer-fee effects, not just the result code.
- **CR-11.** Validate the tests themselves: confirm a regression test fails without the fix, question unexplained expectation changes, and check nothing silently disables a test or fails for the wrong reason.

### Concurrency and locking

- **CR-12.** Never allocate or copy large containers while holding a lock; reserve outside the lock, re-check under it, or hand out shared_ptr snapshots.

### Peer protocol and networking

- **CR-13.** Gate peer-protocol behaviour changes on a new ProtocolVersion, keeping old behaviour for older peers.

## Style and conventions

### Code style and idioms

- **CR-14.** Declare locals const (east style `auto const`), initialised in one statement from the producing expression, value_or or a conditional, and bind aliases and structured bindings with auto const&.
- **CR-15.** Prefer early returns over else branches and nested ifs; hoist disqualifying conditions into one early return and remove checks made redundant upstream.

### Structure and reuse

- **CR-16.** Put guards, degenerate cases and amendment gates inside the helper or virtual hook so callers cannot forget them, and never re-check logic the helper already performs.

### Ledger and transactor idioms

- **CR-17.** Prefer an existing specific result code (temINVALID_FLAG, tecPATH_DRY, tecNO_PERMISSION) over redundant temMALFORMED or a new tec; the code space is limited.

### Test conventions

- **CR-18.** In unit tests and jtx helpers never use assert or XRPL_ASSERT; write `if (!BEAST_EXPECT(x)) return;` or Throw, give switches a failing default, and prefer BEAST_EXPECT over expect. See also [CONTRIBUTING.md](../CONTRIBUTING.md#contracts-and-instrumentation) (where plain `assert` is still expected).
