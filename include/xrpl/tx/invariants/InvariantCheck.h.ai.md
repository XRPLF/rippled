# `include/xrpl/tx/invariants/InvariantCheck.h`

## Role and Purpose

This header is the central registry for the XRPL transaction invariant checking system. It exists to give the ledger a last line of defense: after every transaction has been applied (whether it succeeded or failed), a suite of invariant checkers scans the modified ledger entries and verifies that the result is internally consistent. If any check fails, the transaction is rolled back and replaced with a fee-only charge (`tecINVARIANT_FAILED`) or, if even that fails, excluded from the ledger entirely (`tefINVARIANT_FAILED`).

The file declares the checker classes that live in this translation unit, then aggregates every checker — including those from sibling headers like `FreezeInvariant.h`, `NFTInvariant.h`, `AMMInvariant.h`, `VaultInvariant.h`, and others — into a single `std::tuple` alias called `InvariantChecks`. This tuple is the single source of truth for which invariants exist.

## The Interface Contract — Duck-Typed, Not Virtual

The system deliberately avoids inheritance. No base class, no vtable. Instead it relies on a duck-typed interface: every checker class must implement `visitEntry()` and `finalize()`. The `InvariantChecker_PROTOTYPE` class documents this interface but is guarded by `#if GENERATING_DOCS` — it is entirely absent at compile time.

The two-phase execution model mirrors a streaming aggregation:

- **Phase 1** — `visitEntry(isDelete, before, after)`: called once per modified ledger entry. Checkers use this to accumulate state (e.g., `XRPNotCreated` tracks a running `drops_` delta; `AccountRootsDeletedClean` records every deleted account root along with its before/after snapshots).
- **Phase 2** — `finalize(tx, tec, fee, view, j)`: called once per transaction after all entries have been visited. Returns `true` if the check passes, `false` if it fails.

The actual dispatch happens in `ApplyContext::checkInvariantsHelper()`, which unpacks the tuple via `std::index_sequence` and fold expressions:

```cpp
(..., std::get<Is>(checkers).visitEntry(isDelete, before, after));
```

And for `finalize`, the results are collected into a `std::array<bool, N>` rather than short-circuiting with `&&`. This is intentional: every failed invariant should produce its own log entry at `fatal` level. Short-circuiting with a fold expression would silence all failures after the first.

## Invariants Defined Locally

The checkers declared directly in this file cover the core ledger properties:

**`TransactionFeeCheck`** verifies that the fee charged is non-negative, less than the total XRP supply, and not greater than the fee the transaction itself authorized. Its `visitEntry()` is a no-op; all logic is in `finalize()`.

**`XRPNotCreated`** tracks the net change in XRP across account roots, payment channels, and escrows. In `finalize()`, it verifies that the net change is exactly `-fee` — drops can only be destroyed as fees, never created. Payment channels and escrows have subtleties around deletion (the amount field isn't adjusted on delete), handled by skipping the `after` side when `isDelete` is true.

**`XRPBalanceChecks`** sets a boolean `bad_` flag if any account root's XRP balance is outside `[0, INITIAL_XRP]`. The flag is sticky — once set it can't be cleared.

**`AccountRootsNotDeleted`** counts deleted account roots. In `finalize()`, it uses `hasPrivilege()` (from `InvariantCheckPrivilege.h`) to distinguish: transactions with `mustDeleteAcct` (like `AccountDelete`) must delete exactly one; transactions with `mayDeleteAcct` (like `AMMWithdraw`) may delete at most one; all others must delete zero.

**`AccountRootsDeletedClean`** stores before/after pairs for every deleted account root. In `finalize()`, it verifies the account had zero balance, zero owner count, and left behind no orphaned objects — including trust lines, escrows, offers, NFT pages, payment channels, and for pseudo-accounts, any linked protocol object (AMM, Vault, etc.). This checker is amendment-gated: it logs at fatal level regardless, but only returns `false` if one of `featureInvariantsV1_1`, `featureSingleAssetVault`, or `featureLendingProtocol` is enabled.

**`LedgerEntryTypesMatch`** catches two categories of corruption: a modified entry whose type differs between `before` and `after`, and a newly created entry with an unrecognized type. The valid type list is generated via `ledger_entries.macro`.

**`NoXRPTrustLines`** and **`NoDeepFreezeTrustLinesWithoutFreeze`** validate trust line properties. The former ensures no trust line references XRP as an asset (which would be nonsensical). The latter enforces that the `lsfLowDeepFreeze`/`lsfHighDeepFreeze` flags can only be set when the corresponding regular freeze flag is also set.

**`NoBadOffers`** rejects offers with negative amounts or XRP-to-XRP exchange, both before and after modification.

**`NoZeroEscrow`** (somewhat misnamed) validates that escrow amounts are strictly positive and within bounds. It also validates `ltMPTOKEN_ISSUANCE` and `ltMPTOKEN` entries for amount range and locked-amount consistency.

**`ValidNewAccountRoot`** ensures that at most one account root is created per transaction, that it originates from a transaction with the correct privilege, starts with the right sequence number (current ledger sequence for normal accounts, zero for pseudo-accounts), and has the expected flags if it is a pseudo-account.

**`ValidClawback`** is scoped to `ttCLAWBACK` transactions. On success, it confirms at most one trust line or MPToken was modified and that the holder's resulting balance is non-negative. On failure, it confirms nothing was modified at all.

**`ValidPseudoAccounts`** enforces structural rules for pseudo-accounts (used by AMM and Vault): exactly one pseudo-account field must be set, the sequence number must not change, the correct flags must be present, and no regular key may exist.

**`NoModifiedUnmodifiableFields`** checks that certain fields are never altered during modification. For `ltLOAN_BROKER` and `ltLOAN` entries, this covers a large set of creation-time fields like `sfInterestRate`, `sfBorrower`, etc. For all other entry types, `sfLedgerEntryType` and `sfLedgerIndex` are universally immutable.

## The Privilege System

`InvariantCheckPrivilege.h` defines a `Privilege` bitmask enum and `hasPrivilege(STTx const& tx, Privilege priv)`, which is implemented via `transactions.macro` — the same macro file used to enumerate all transaction types. Each transaction type carries a bitmask of its allowed privileges at the macro invocation site, so `hasPrivilege()` is essentially a compile-time-declared table of which operations each transaction type may legitimately perform. Invariant checkers call this function to distinguish legitimate operations from violations (e.g., distinguishing an `AccountDelete` from a bug that accidentally deletes an account).

## Amendment Gating and the `assert(enforce)` Pattern

Several checkers implement a soft-enforcement pattern: they log a `fatal` message and fire a debug-build `XRPL_ASSERT` when an invariant is violated, but only return `false` from `finalize()` when a relevant amendment is enabled. This allows early detection during development and testing without breaking network consensus on nodes that haven't enabled the amendment yet. The `assert(enforce)` is explicitly documented in `InvariantCheckPrivilege.h` as a developer-facing trap — it fires if an invariant is violated in a build that has the relevant amendments disabled, which is the typical state during development.

## Extending the System

Adding a new invariant requires two steps: declare the class (either in this file or a new sibling header) and add it to the `InvariantChecks` tuple. The tuple-based dispatch in `ApplyContext` is fully generic — it requires no other changes. The comment above the tuple definition makes this extensibility explicit.