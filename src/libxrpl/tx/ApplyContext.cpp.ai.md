## `ApplyContext.cpp` — Transaction Application Context and Invariant Enforcement

### Role in the System

`ApplyContext` sits at the heart of XRPL's transaction processing pipeline. It owns the sandboxed ledger view that a `Transactor` writes into during transaction execution, and it is the last line of defense before any corrupted state can reach a finalized ledger. When a transaction's own logic fails to prevent an illegal ledger mutation — whether due to a bug or an attempted exploit — `ApplyContext` catches it through a battery of invariant checks applied after every apply attempt.

The class is constructed once per transaction application attempt by the apply layer (`apply.cpp`/`applySteps.cpp`), after preflight and preclaim have already passed. By the time `ApplyContext` exists, the transaction has been declared structurally sound and account-eligible; the remaining question is whether executing it produces a valid ledger state.

### Construction and the Sandboxed View

The constructor takes the live `OpenView` (`base_`), the pre-validated `STTx`, the preclaim result, the base fee, and apply flags. Its first meaningful act is to `emplace` an `ApplyViewImpl` (`view_`) on top of `base_`. This indirection is fundamental to the transaction safety model: the transactor reads and writes exclusively through `view_`, never directly against `base_`. No ledger mutation escapes the sandbox until the explicit `apply()` call merges the deltas.

The constructor asserts that `parentBatchId` is populated if and only if `tapBATCH` is set in flags:

```cpp
XRPL_ASSERT(
    parentBatchId.has_value() == ((flags_ & tapBATCH) == tapBATCH),
    "Parent Batch ID should be set if batch apply flag is set");
```

This enforces a hard invariant around batch transaction processing — both pieces of batch context must either be present or absent together. The header also offers a convenience constructor for non-batch callers that omits `parentBatchId` entirely and asserts the flag is clear, removing any possibility of accidentally constructing a half-batch context.

### `discard()` — Cheap State Rollback

`discard()` simply re-emplaces a fresh `ApplyViewImpl` on top of `base_`, discarding all accumulated changes. Because the sandbox has never touched `base_`, this rollback costs only object construction. `Transactor` uses this mechanism when it needs to retry after an invariant failure: it attempts a full apply, detects a broken invariant, calls `discard()` to wipe the sandbox, then re-applies with fee-only logic before checking invariants a second time.

### `apply()` — Committing the Sandbox

When the transactor is satisfied with the transaction result, `apply()` delegates to `view_->apply()`, which merges the sandbox's ledger entry deltas into the live `base_`. The call forwards both `parentBatchId_` and the dry-run flag (`tapDRY_RUN`), so the commit step remains aware of batch context and simulation mode — keeping those concerns cleanly separated from the transactor itself.

### Invariant Checking Architecture

The invariant checking machinery in `checkInvariantsHelper` is the most architecturally significant part of this file. It uses compile-time polymorphism — a `std::tuple` of checker objects and an `std::index_sequence` — to iterate all registered invariant checkers without virtual dispatch or runtime polymorphism overhead.

`getInvariantChecks()` (defined in `InvariantCheck.h`) returns a fresh `InvariantChecks` tuple containing 25 distinct checker types, ranging from `XRPNotCreated` and `AccountRootsNotDeleted` to `ValidVault` and `ValidLoan`. Each checker implements `visitEntry()` to accumulate per-entry state and `finalize()` to render a pass/fail verdict. `checkInvariantsHelper` drives both phases:

First, it uses a fold expression to call `visitEntry` on every checker for every modified ledger entry via the `visit()` delegation:

```cpp
visit([&checkers](...) {
    (..., std::get<Is>(checkers).visitEntry(isDelete, before, after));
});
```

Then it finalizes all checkers into an array of booleans:

```cpp
std::array<bool, sizeof...(Is)> const finalizers{
    {std::get<Is>(checkers).finalize(tx, result, fee, *view_, journal)...}};
```

The critical design decision here is that the finalize step is **not** a `...&&` fold expression. A fold would short-circuit after the first failure, silencing subsequent ones. The array-then-`all_of` pattern ensures every failing invariant logs its own diagnostic message before a verdict is returned. In a production incident where multiple invariants fail simultaneously — which indicates a serious regression or exploit — having all failures visible is essential for diagnosis.

The invariant checkers run even on failed (`tec*`) transactions. As the `InvariantChecker_PROTOTYPE` documentation explains, bugs or exploits could cause a failed transaction to mutate ledger state in unexpected ways; invariants must defend against that possibility regardless of the transaction result code.

### The `failInvariantCheck()` Escalation Logic

When invariants fail, the severity of the returned code reflects how far through the retry sequence the caller is:

- A first-time failure from a normal result produces `tecINVARIANT_FAILED`, which **is** included in the ledger — the sender is still charged a fee for the invalid transaction attempt.
- If invariants fail again during a fee-only retry (recognized because the incoming result is already `tecINVARIANT_FAILED` or `tefINVARIANT_FAILED`), the function escalates to `tefINVARIANT_FAILED`, which does **not** get included in a ledger.

The rationale is explicit in the code comments: if even the minimal fee-charge path breaks invariants, something is wrong enough that no ledger entry of any kind should be created. This two-tier approach balances the normal goal of fee recovery against the impossibility of safely applying anything when the transaction's behavior is deeply unpredictable.

### Relationship to Sibling Files

`ApplyContext` is consumed primarily by `Transactor.cpp`, which holds a reference to it throughout the apply phase and calls `checkInvariants()` after `doApply()` returns. The `apply.cpp` and `applySteps.cpp` files orchestrate the higher-level flow that constructs an `ApplyContext` and dispatches it to the correct transactor subclass. The invariant definitions themselves live in `src/libxrpl/tx/invariants/`, with `InvariantCheck.h` providing the `InvariantChecks` tuple type and the `getInvariantChecks()` factory used here at runtime.