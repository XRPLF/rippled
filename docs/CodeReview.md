# Code review guide

This guide collects the most critical review rules that come up on pull requests in this repository. It is written for both human reviewers and AI reviewers, and for authors who want to self-review before asking for a review. The rules were distilled from maintainer review comments on public `rippled` pull requests.

It complements, and does not replace, [CONTRIBUTING.md](../CONTRIBUTING.md) (process, contracts, tests, style guide) and [docs/CodingStyle.md](./CodingStyle.md) (formatting). Where a rule overlaps those documents, it links to the relevant section.

## How to use

- Apply the **Correctness** rules first. Ignoring one risks wrong ledger state, a wrong result, a security issue, or a bug reaching a release.
- Then apply the **Style and conventions** rules.
- Cite the rule ID in a review comment, e.g. "CT-2", so the author can find the rule.
- A rule ID is `C` (correctness) or `S` (style), then the theme letter shown in its heading, then a number. Add a new rule at the end of its theme with the next number, and never renumber existing rules.
- A rule is a default, not a law: if a change deliberately departs from one, the PR should say why.

## Correctness

### Amendments and feature gating (CG)

- **CG-1.** Respect amendment state until formal retirement: always check it as if disabled, preserve pre-amendment behaviour even when wrong, honour pre-amendment objects, and when retiring remove only the gating path.

### Transactor structure and result codes (CT)

- **CT-1.** Put checks needing only transaction fields and Rules in preflight (tem codes); ledger-dependent or mutating checks go in preclaim or doApply; run universal preflight checks before transactor-specific ones.
- **CT-2.** Never throw on transaction data; return tecINTERNAL or tefINTERNAL for conditions earlier steps made impossible, keep logicError in destructors and consensus paths, and catch every throwing call.
- **CT-3.** Return the result code that describes the actual failure, propagate a helper's TER unchanged, tolerate only the specific expected error, and never let cleanup failures override the primary result.
- **CT-4.** Keep every transactor check and result code in sync with the XLS spec; when the spec omits a protocol invariant or is wrong, fix the spec rather than silently diverging.

### Ledger objects, fields and flags (CL)

- **CL-1.** Null-check every SLE from view.read or peek before dereferencing. When absence is a valid outcome, return its protocol result (e.g. `tecNO_ENTRY`); when earlier checks make a null impossible, return tefBAD_LEDGER or tefINTERNAL with LCOV_EXCL_LINE.
- **CL-2.** Call view.update(sle) once per modified SLE per transaction; its position among the modifications is irrelevant, but it must happen.

### Invariants, asserts and defensive checks (CI)

- **CI-1.** Use `XRPL_ASSERT` for a condition that must hold and `UNREACHABLE` for a branch that must never run; never use both for the same check. Release builds continue past both, so write an impossible branch as `if (cond) { UNREACHABLE(...); return error; }` with LCOV exclusion, not as a silent tefINTERNAL. See [instrumentation.h](../include/xrpl/beast/utility/instrumentation.h) and [CONTRIBUTING.md](../CONTRIBUTING.md#contracts-and-instrumentation).
- **CI-2.** Invariants must hold when the transaction fails: never return true early on a non-tesSUCCESS result; instead verify that only the changes permitted for that result were made, such as the fee and the cleanup kept by `Transactor::processPersistentChanges`.

### Tests (CZ)

- **CZ-1.** After each transaction in a test, assert the resulting ledger state and balances in every branch, including absent objects, flags and transfer-fee effects, not just the result code.
- **CZ-2.** Validate the tests themselves: confirm a regression test fails without the fix, question unexplained expectation changes, and check nothing silently disables a test or fails for the wrong reason.

### Concurrency and locking (CC)

- **CC-1.** Never allocate or copy large containers while holding a lock. Instead, reserve capacity before taking the lock and re-check the size once it is held, or keep the data behind a `shared_ptr` and copy only the pointer under the lock so readers use that snapshot unlocked.

### Peer protocol and networking (CP)

- **CP-1.** Gate peer-protocol behaviour changes on a new ProtocolVersion, keeping old behaviour for older peers. This includes relaying: convert a message received from a peer on one version before forwarding it to a peer on another.

## Style and conventions

### Code style and idioms (SC)

- **SC-1.** Declare locals const (east style `auto const`), initialised in one statement from the producing expression, value_or or a conditional, and bind aliases and structured bindings with auto const&.
- **SC-2.** Prefer early returns over else branches and nested ifs; hoist disqualifying conditions into one early return and remove checks made redundant upstream.

### Structure and reuse (SR)

- **SR-1.** Put guards, degenerate cases and amendment gates inside the helper or virtual hook so callers cannot forget them, and never re-check logic the helper already performs.

### Ledger and transactor idioms (SL)

- **SL-1.** Prefer an existing specific result code (temINVALID_FLAG, tecPATH_DRY, tecNO_PERMISSION) over redundant temMALFORMED or a new tec; the code space is limited.

### Test conventions (SZ)

- **SZ-1.** In tests and jtx helpers, check outcomes with the framework's expectations, not contracts: `if (!BEAST_EXPECT(x)) return;` or `Throw` in Beast suites and jtx (prefer `BEAST_EXPECT` over `expect`), `EXPECT_*`/`ASSERT_*` in gtest. Never use XRPL_ASSERT there; plain `assert` is only for contracts, as [CONTRIBUTING.md](../CONTRIBUTING.md#contracts-and-instrumentation) allows. Give switches a failing default.
- **SZ-2.** Test `libxrpl` code with gtest under `src/tests/`. Write a Beast suite under `src/test/` only when the test cannot be written with gtest.
