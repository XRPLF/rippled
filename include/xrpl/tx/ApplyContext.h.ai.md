# `ApplyContext` — Transaction Application State Container

`ApplyContext` is the central context object threading through the entire transaction-application pipeline in the XRPL ledger engine. Every time a signed transaction is applied to an `OpenView`, an `ApplyContext` bundles together the transaction, its pre-validated result, the sandboxed mutable view, logging infrastructure, and the invariant checking machinery needed to safely commit or discard ledger state changes.

## Role in the System

The XRPL transaction engine processes each transaction in two phases: a *preclaim* phase (authorization and fee validation, read-only) and an *apply* phase (actual state mutation). `ApplyContext` is created at the boundary between those phases and lives for the duration of the apply phase. It is passed by reference to every `Transactor` implementation, giving each transaction handler a uniform handle to the sandboxed ledger view, the fee information, and the ability to commit or roll back.

## Sandboxed View Lifecycle

The most important design choice in `ApplyContext` is how it manages the mutable ledger view. The working view is stored as `std::optional<ApplyViewImpl> view_`, sitting on top of a reference to the underlying `OpenView& base_`. This optional wrapping is not incidental — it is the mechanism behind `discard()`:

```cpp
void ApplyContext::discard() {
    view_.emplace(&base_, flags_);
}
```

Rather than implementing rollback semantics (walking backwards through recorded mutations), `discard()` simply destroys the current `ApplyViewImpl` in-place and constructs a fresh one. The base view is never touched, so the sandboxed changes evaporate without any undo log. This is a deliberate performance and simplicity tradeoff: rollback in a complex ledger object model would require either copy-on-write snapshots or a redo log; instead the system just discards and restarts from the unmodified base.

When a transaction handler is satisfied with its changes, it calls `apply(TER)`, which delegates to `ApplyViewImpl::apply()`. That method writes the accumulated state changes from the sandbox into `base_`, generates and returns `TxMeta` (the transaction metadata), and marks itself consumed. After `apply()` returns, the `ApplyViewImpl` is no longer usable — it has transferred ownership of its mutations to the base view.

## Batch Transaction Support

`ApplyContext` has two constructors. The simpler one (without `parentBatchId`) delegates to the full constructor with `std::nullopt` and includes an assertion that `tapBATCH` is not set:

```cpp
XRPL_ASSERT((flags & tapBATCH) == 0, "Batch apply flag should not be set");
```

When a transaction executes inside a batch, the fuller constructor receives the parent batch transaction's `uint256` ID. This ID is stored in `parentBatchId_` and forwarded through to `ApplyViewImpl::apply()`, where it gets embedded in the generated `TxMeta`. This creates an auditable parent-child relationship in ledger metadata between batch envelope transactions and their inner transactions. The constructor correspondingly asserts the inverse invariant — that `parentBatchId` is set if and only if `tapBATCH` is active.

## Invariant Checking

After every successful or fee-claiming transaction, `checkInvariants()` is called as the final safety gate before the result is accepted. This method operates over a compile-time tuple of checker types — `InvariantChecks` — defined in `InvariantCheck.h`. The implementation uses `std::index_sequence` to iterate:

```cpp
template <std::size_t... Is>
TER ApplyContext::checkInvariantsHelper(TER result, XRPAmount fee, std::index_sequence<Is...>) {
    auto checkers = getInvariantChecks();
    visit([&checkers](...) {
        (..., std::get<Is>(checkers).visitEntry(isDelete, before, after));
    });
    std::array<bool, sizeof...(Is)> const finalizers{
        {std::get<Is>(checkers).finalize(tx, result, fee, *view_, journal)...}};
    if (!std::all_of(...)) { return failInvariantCheck(result); }
    return result;
}
```

A critical comment explains why a `&&` fold expression is explicitly avoided for the `finalize` calls: it would short-circuit on the first failure, suppressing log output from every subsequent failing invariant. The two-step approach — collect results into an array, then check the array — ensures every invariant that fails writes its fatal log message.

The `failInvariantCheck()` static method encodes a two-tier failure response. If the current result is already `tecINVARIANT_FAILED` or `tefINVARIANT_FAILED`, it returns `tefINVARIANT_FAILED` — a transaction error code that causes the transaction to be *excluded* from the ledger entirely. If this is the first invariant failure, it returns `tecINVARIANT_FAILED`, which still results in a fee-charging ledger entry (the transaction appears in the ledger but has a failed result). This distinction matters for network consensus: a `tec` result is reproducible across validators and thus consensus-safe, while a `tef` result signals something so wrong the transaction must not be included at all.

## The `rawView()` Accessor

The header contains an explicit code comment — `// VFALCO Unfortunately this is necessary` — on the `rawView()` accessor. `ApplyView` provides higher-level ledger manipulation that enforces certain constraints. Some internal code in the transaction engine requires the lower-level `RawView` interface to write ledger entries without those guards. The accessor exposes this escape hatch while making its non-ideal nature visible.

## Public State and Immutability

Several fields are `const`-qualified public members: `tx`, `preclaimResult`, `baseFee`, and `journal`. This reflects the reality that these values are fixed for the lifetime of a transaction apply cycle — the transaction cannot change, the pre-claim result is read-only context, and the fee is determined before applying. Mutable state is confined to the private `view_`, `flags_`, and `parentBatchId_` members, with controlled access through the public interface.