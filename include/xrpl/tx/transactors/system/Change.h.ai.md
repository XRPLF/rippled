# `Change` — System-Level Protocol Transactor

## Role and Purpose

`Change` is the single transactor responsible for applying all system-level, consensus-driven protocol mutations to a closed ledger. It handles three conceptually distinct operations — enabling an amendment, adjusting network fees, and modifying the Negative UNL validator set — under one class. The three type aliases declared at the bottom of the header make this explicit:

```cpp
using EnableAmendment = Change;
using SetFee         = Change;
using UNLModify      = Change;
```

These aliases exist so that higher-level dispatch code can refer to each pseudo-transaction type by its semantic name without duplicating implementation. All three resolve to the same `Change` class, and `doApply()` switches on `getTxnType()` to route to `applyAmendment()`, `applyFee()`, or `applyUNLModify()`.

## Pseudo-Transaction Identity

Unlike ordinary user-submitted transactions, `Change` transactions are synthesized by the server during the ledger close process and are never sent by real accounts. The preflight specialization enforces this identity explicitly: the account field must be the zero account (`beast::zero`), the fee must be zero XRP, signatures must be absent, and the sequence number must be zero. Any deviation returns a `tem` error code immediately.

This is why `calculateBaseFee()` is overridden to return `XRPAmount{0}`. Pseudo-transactions are not subject to fee markets, load scaling, or reserve checks — they represent network-internal protocol maintenance.

The normal `invokePreflight<T>` template checks amendment gating, flag masks, fee/signature validity, and signature validation in a fixed order. Because `Change` transactions violate almost all of those assumptions, the specialization `Transactor::invokePreflight<Change>` (defined in `Change.cpp`) replaces the entire default sequence with a minimal check: only `preflight0` (for txid and flag sanity) is called, followed by the account/fee/signature/sequence zero-checks described above.

## Closed-Ledger Enforcement

`preclaim()` is the first place where the open/closed ledger distinction is enforced. If `ctx.view.open()` is true, the transaction returns `temINVALID`. This guard exists because the mechanism that determines whether `tapOPEN_LEDGER` is set is not available during preflight, making `preclaim()` the appropriate checkpoint. Protocol-level changes are meaningless against an open ledger snapshot.

`preclaim()` also validates fee format compatibility. The `ttFEE` transaction has two incompatible field sets depending on whether the `featureXRPFees` amendment is active: the old format uses fee-unit integers (`sfBaseFee`, `sfReferenceFeeUnits`, `sfReserveBase`, `sfReserveIncrement`), while the new format uses drop amounts (`sfBaseFeeDrops`, `sfReserveBaseDrops`, `sfReserveIncrementDrops`). The presence or absence of these fields is strictly enforced in both directions at `preclaim()` time, since `preclaim()` has access to the current rule set.

## Amendment Lifecycle via `applyAmendment()`

The amendment object in the ledger (accessed via `keylet::amendments()`) maintains two collections: `sfAmendments`, the list of already-active amendment hashes, and `sfMajorities`, the list of amendments that currently hold validator supermajority but have not yet been activated.

`applyAmendment()` recognizes three mutually exclusive states signaled by the transaction's flags:

- **`tfGotMajority`** — adds the amendment to the `sfMajorities` array with the current ledger's close time, recording when the supermajority was first achieved. If the amendment is already in that list, `tefALREADY` is returned.
- **`tfLostMajority`** — removes the amendment from `sfMajorities`, resetting its clock. If it was never in the list, `tefALREADY` is returned.
- **No flag set** — the amendment is being activated. It is added to `sfAmendments` and `AmendmentTable::enable()` is called on the service registry. If the server does not support this amendment, `NetworkOPs::setAmendmentBlocked()` is invoked, which prevents the node from participating further in consensus — the correct behavior when the network has advanced beyond the node's capabilities.

## Fee Schedule Updates via `applyFee()`

`applyFee()` reads from the transaction and writes directly to the ledger's fee schedule SLE (`keylet::fees()`). The dual-format handling mirrors what `preclaim()` validated: under `featureXRPFees`, it sets the drop-denominated fields and explicitly removes the old fee-unit fields using `makeFieldAbsent`, ensuring no stale data persists in the ledger. Without the feature, the reverse applies.

## Negative UNL Modification via `applyUNLModify()`

The Negative UNL (N-UNL) mechanism allows the network to continue making forward progress even when a subset of validators becomes unreliable. `applyUNLModify()` writes into the `keylet::negativeUNL()` ledger object to nominate a validator for disabling or re-enabling.

Critically, this operation is only permitted on "flag ledgers" — specific ledger sequence numbers where the UNL adjustment protocol operates. Attempting it on any other ledger returns `tefFAILURE`. The method validates the transaction's `sfUNLModifyValidator` as a valid public key before proceeding.

The object tracks at most one pending `sfValidatorToDisable` and one pending `sfValidatorToReEnable` at any time. Several consistency invariants are enforced: you cannot disable a validator already in the negative UNL; you cannot re-enable a validator not in it; and the same validator cannot simultaneously appear in both pending slots. Any violation returns `tefFAILURE` with a diagnostic log at warning level.

## Design Rationale

Consolidating three distinct pseudo-transaction types into a single `Change` class avoids three separate classes that would each need the same unusual preflight bypass, zero-fee override, and closed-ledger guard. The type aliases provide semantic clarity at the call site without code duplication. The dispatch in `doApply()` is exhaustive by design — the `default` branch is annotated `UNREACHABLE` and covered by `preclaim()`'s rejection of unknown transaction types, so it functions as a defensive assertion rather than live error handling.