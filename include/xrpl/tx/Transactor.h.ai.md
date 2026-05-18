# `include/xrpl/tx/Transactor.h`

## Role in the System

This header is the architectural keystone of XRPL transaction processing. Every transaction type — payment, offer, AMM, NFT, escrow, and dozens more — is implemented as a class that publicly inherits `Transactor`. The header defines the three-phase validation-and-execution contract that all transactions must satisfy, along with the two context structures (`PreflightContext` and `PreclaimContext`) that carry state between phases.

The three phases exist for a specific reason: they differ in what ledger state they require and in the consequences of failure. Code that can run without any ledger access runs cheapest; code that needs a final ledger read runs in the middle; and code that mutates ledger state runs last only when the cheaper checks have already passed.

## The Three-Phase Pipeline

**Phase 1 — preflight**: No ledger access. Validates transaction format, flags, fee field sanity, network ID, and cryptographic signature validity. Runs against a `PreflightContext` that has only the raw `STTx`, the active `Rules`, and apply flags. Because it requires no I/O, preflight can be called in parallel or cached. The output is a `PreflightResult` bundling the `NotTEC` result with `TxConsequences`, which the transaction queue (`TxQ`) uses to reason about how queuing the transaction would affect the account.

**Phase 2 — preclaim**: Read-only ledger access via `ReadView`. Given a `PreclaimContext` that adds the ledger view and the preflight result, preclaim checks whether the transaction is likely to succeed against the current ledger state (correct sequence, sufficient fee balance, signature against account state). The key output is whether the transaction will claim a fee even on failure — ledger infrastructure uses this to decide whether to relay an unvalidated transaction.

**Phase 3 — doApply**: Mutable ledger access via `ApplyView`. The virtual method that each concrete transactor must override. Only runs when preclaim returned `tesSUCCESS`.

## Compile-Time Polymorphism via Name Hiding

The most architecturally notable design in this file is that the preflight pipeline does **not** use virtual dispatch. Instead, derived classes override behavior by defining static methods with the same names — `preflight`, `preclaim`, `getFlagsMask`, `checkExtraFeatures`, `preflightSigValidated` — and the central `invokePreflight<T>()` template resolves them at compile time:

```cpp
template <class T>
NotTEC Transactor::invokePreflight(PreflightContext const& ctx) {
    auto const feature = Permission::getInstance().getTxFeature(ctx.tx.getTxnType());
    if (feature && !ctx.rules.enabled(*feature))  return temDISABLED;
    if (!T::checkExtraFeatures(ctx))               return temDISABLED;
    if (auto const ret = preflight1(ctx, T::getFlagsMask(ctx))) return ret;
    if (auto const ret = T::preflight(ctx))         return ret;
    if (auto const ret = preflight2(ctx))           return ret;
    return T::preflightSigValidated(ctx);
}
```

The pattern is deliberate: it gives each transaction type the ability to add or replace validation steps without virtual-function overhead or the accidental base-class call problem. The header comment explicitly warns not to define `invokePreflight` in derived classes, and not to call `preflight1` or `preflight2` directly — those are private plumbing called in the correct order by the template.

`preflight1` checks the account field, fee field, signing key format, and ticket/AccountTxnID compatibility. `preflight2` validates the cryptographic signature via the hash router cache. The reason they are split is that the transaction-specific `T::preflight` runs between them, allowing type-level validation to happen before the expensive signature check.

The one explicit template specialization, `invokePreflight<Change>`, is defined in `Change.cpp`. `Change` is a pseudo-transaction (validator-generated, no real sender) that requires entirely different preflight logic.

## Context Structures and Batch Support

Both `PreflightContext` and `PreclaimContext` have dual constructors: one for ordinary transactions and one for batch inner transactions. The batch variants accept a `parentBatchId` (the hash of the outer batch transaction) and assert that `tapBATCH` is set in `flags`. The non-batch constructors assert the opposite. This constructor-level assertion enforces the invariant at construction time — if you pass a batch ID without the flag, or the flag without an ID, you get a debug assertion rather than silent misbehavior.

When `tapBATCH` is active, `preflight2` skips the cryptographic signature check entirely, since batch inner transactions are authorized through the outer transaction's signature.

## `operator()` and the Apply Mechanics

`Transactor::operator()()` is the final dispatch point called by `doApply()` (the free function in `applySteps.h`). It:

1. Sets up per-transaction numeric rule guards (`fixUniversalNumber`, `CurrentTransactionRulesGuard`) as RAII objects.
2. In debug builds, roundtrips the transaction through serialization to detect corruption.
3. Checks if the transaction ID matches a debug trap, useful for stopping on a specific transaction during testing.
4. Passes the preclaimResult directly if it isn't `tesSUCCESS`; otherwise calls `apply()` → `preCompute()` → `doApply()`.
5. Handles `tecOVERSIZE` (metadata grew too large) by rolling back and rerunning to only collect removable offers, trust lines, and NFT offers.
6. Forces `applied = false` when `tapDRY_RUN` (simulation mode) is set, ensuring no state changes persist.

The private `reset(fee)` method rolls back `ApplyContext` and deducts only the fee — the mechanism for charging a fee on a failed-but-fee-claiming (`tec`) result.

## Fee Calculation

`calculateBaseFee()` returns the base fee in drops: the ledger's configured base fee plus one additional base fee per multisigner. This is not scaled for server load. `minimumFee()` scales it using `LoadFeeTrack` from the `ServiceRegistry`. Separating the two allows the unscaled base fee to be computed in any phase, while load scaling only happens where current node load is accessible.

`calculateOwnerReserveFee()` adds the owner reserve increment for transactions that will create a ledger object, preventing spam by requiring the sender to prove they can cover the reserve.

## `ConsequencesFactoryType` and the Transaction Queue

Each concrete transactor declares a `static constexpr ConsequencesFactoryType ConsequencesFactory` member with one of three values. `Normal` produces standard `TxConsequences` (fee only). `Blocker` signals that applying this transaction could prevent subsequent queued transactions from claiming fees (e.g., `SetRegularKey` changing the signing key). `Custom` requires the transactor to implement `makeTxConsequences(PreflightContext const&)` for transaction-specific cost modeling. The `with_txn_type` function in `applySteps.cpp` uses C++20 `requires` constraints to dispatch the correct `consequences_helper` at compile time.

## Validation Utilities

The protected template helpers `validNumericRange` and `validNumericMinimum` follow a deliberate convention: an absent optional (`std::nullopt`) is treated as valid, reflecting the rule that optional fields are legal to omit. Both are overloaded for `unit::ValueUnit<Unit, T>` strong-unit types to maintain type safety across unit systems.

`checkPermission()` integrates the Delegate feature: if the transaction carries a `sfDelegate` field, it verifies that a `Delegate` ledger object exists and that the delegated permissions cover this transaction type. This is called as a static method during preclaim so the read-only ledger check happens before any mutation.