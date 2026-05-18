# `src/libxrpl/tx/invariants/InvariantCheck.cpp`

## Role in the System

This file is the primary implementation of XRPL's post-transaction invariant checking machinery. Every time a transaction is applied to the ledger, a suite of structural and financial invariants is run against the resulting state changes. If any invariant fails, the transaction's result is overridden with `tecINVARIANT_FAILED` (or `tefINVARIANT_FAILED` if a re-application also fails), ensuring that a corrupt or exploited transaction can never be committed to a validated ledger.

The invariants here are the last line of defense. They are not pre-validation checks — they run *after* the transaction engine has already applied its logic, operating on the diff between the pre- and post-transaction ledger state. They exist to catch bugs in transaction processors, novel exploits, or edge cases that pre-validation missed.

## The Two-Phase Visitor Framework

Every invariant checker in this file follows the same interface defined in `InvariantCheck.h`:

- `visitEntry(isDelete, before, after)` — called once per modified `SLE` (Serialized Ledger Entry) in the transaction's sandbox. This is where checkers accumulate state: counters, flags, or lists of entries to examine.
- `finalize(tx, result, fee, view, journal)` — called once after all entries have been visited. This is where the checker renders a verdict, returning `true` to pass or `false` to fail.

The dispatch loop lives in `ApplyContext::checkInvariantsHelper()`, which holds an `InvariantChecks` tuple (a `std::tuple` of every checker type, enumerated in `InvariantCheck.h`). A variadic fold expression fires every checker's `visitEntry` for each changed SLE, then evaluates all `finalize()` calls. One critical design note in that code: it deliberately avoids a short-circuit `&&` fold for the finalizers — every invariant is evaluated and logs independently, so the full set of failures appears in the journal in a single transaction failure.

## `hasPrivilege()` and the X-Macro Dispatch

The free function `hasPrivilege(STTx const& tx, Privilege priv)` maps transaction types to `Privilege` bitmasks via the `transactions.macro` X-macro expansion. Each entry in that macro carries a privilege bitfield; `hasPrivilege` performs a bitwise AND test against it. This avoids a hand-maintained switch statement and keeps privilege assignments co-located with transaction type definitions.

The `Privilege` enum in `InvariantCheckPrivilege.h` captures semantically distinct capabilities: `createAcct`, `mustDeleteAcct`, `mayDeleteAcct`, `createPseudoAcct`, `overrideFreeze`, `changeNFTCounts`, `createMPTIssuance`, and more. Having separate `must` and `may` variants allows invariants to distinguish transactions that are required to produce side effects (AccountDelete *must* delete exactly one account root) from those that can optionally do so (AMMWithdraw *may* delete an account root when LP token supply hits zero).

## Core Invariants

**`TransactionFeeCheck`** validates three fee constraints: the fee must not be negative, must not equal or exceed the total XRP supply (`INITIAL_XRP`), and must not exceed the fee declared in the transaction's `sfFee` field. The third constraint permits fee discounts but not fee surcharges — an important economic invariant.

**`XRPNotCreated`** tracks the net XRP drop change across all `ltACCOUNT_ROOT`, `ltPAYCHAN`, and `ltESCROW` entries touched by the transaction. Payment channels track uncommitted XRP as `sfAmount - sfBalance` (the unclaimed portion), so deletions of pay channels and escrows are excluded from the tally since those fields are not adjusted at deletion time. The finalize check requires that `drops_` be non-positive (no XRP creation) and that `-drops_` exactly equals the fee charged. This ties fee accounting to XRP conservation.

**`XRPBalanceChecks`** verifies that every modified account root carries a balance denominated in native XRP between 0 and `INITIAL_XRP` inclusive. This is a belt-and-suspenders check against type confusion — the `STAmount::native()` call verifies the currency code, not just the magnitude.

**`NoBadOffers`** checks that DEX order entries contain non-negative amounts and that neither side of the offer is XRP-for-XRP. The XRP-to-XRP check uses `pays.native() && gets.native()` rather than comparing currency codes, matching what the XRP native type represents internally.

**`NoZeroEscrow`** has grown into a multi-asset invariant. It checks escrow amounts for XRP (must be strictly positive and below `INITIAL_XRP`), IOU (must be positive, must not use the `badCurrency()` sentinel), and MPT (must be positive and below `maxMPTokenAmount`). The same invariant also validates `ltMPTOKEN_ISSUANCE` entries: outstanding amount must be within range, and locked amount must not exceed outstanding. Similarly `ltMPTOKEN` entries are checked. This reuse of a semantically-named escrow invariant for MPT accounting is a pragmatic bundling choice rather than a conceptual one.

## Account Deletion Integrity

Two checkers cooperate to ensure account deletions are clean:

**`AccountRootsNotDeleted`** uses the privilege system to enforce cardinality rules: a transaction with `mustDeleteAcct` (AccountDelete, AMMDelete) must delete exactly one account root on success. A transaction with `mayDeleteAcct` (AMMWithdraw, AMMClawback) may delete one. All other transactions must not delete any. The `accountsDeleted_` counter is incremented in `visitEntry` and checked in `finalize` against the transaction result and its privilege set.

**`AccountRootsDeletedClean`** stores the `(before, after)` SLE pair for each deleted account root and performs a thorough post-deletion audit in `finalize`. It checks that the deleted account's balance is zero and its `sfOwnerCount` is zero. It then probes the ledger for objects that should have been cleaned up: the owner directory, signer lists, check entries, deposit pre-authorizations, and the full range of NFT pages (using `view.succ()` to scan between `nftpage_min` and `nftpage_max` for any intermediate pages). Pseudo-account linked objects (AMM, Vault, etc.) are also checked by reading the pseudo-account designator fields from the `before` SLE — `before` is used specifically because fields might be cleared during deletion, and the pre-deletion snapshot still has the object keys.

This invariant uses a feature-gated enforcement pattern: `enforce = view.rules().enabled(featureInvariantsV1_1) || view.rules().enabled(featureSingleAssetVault) || ...`. Log messages and `XRPL_ASSERT` fire unconditionally; hard failures only occur when an enabling amendment is active. This allows invariant checks to be shipped and observed in logs on non-upgraded nodes before they become consensus-breaking.

## Pseudo-Account Invariants

**`ValidNewAccountRoot`** ensures that new accounts are created only by privileged transactions (those with `createAcct | createPseudoAcct`), that at most one account is created per transaction, and that the starting sequence is set correctly. For regular accounts, the sequence must equal the current ledger sequence. For pseudo-accounts (AMM, Vault, etc.), the sequence must be 0, and the flags must be exactly `lsfDisableMaster | lsfDefaultRipple | lsfDepositAuth` — encoding the security constraints that prevent pseudo-accounts from acting as regular signers or initiating payments.

**`ValidPseudoAccounts`** extends this for all modifications to pseudo-accounts during their lifetime. It verifies that exactly one pseudo-account designator field is present (`getPseudoAccountFields()` returns this list from the SField metadata), that the sequence never changes, that the required flags remain set, and that no `sfRegularKey` is ever assigned. Errors are accumulated in a `std::vector<std::string>` and all are emitted to the journal before returning.

## `NoModifiedUnmodifiableFields`

This invariant enforces that structurally immutable fields are never altered on existing objects. For all ledger entry types, `sfLedgerEntryType` and `sfLedgerIndex` are immutable. For `ltLOAN_BROKER` and `ltLOAN` objects (lending protocol), an extended set of origination-time fields — rates, fees, schedules, parties, and configuration — are also immutable after creation. The checker collects all modified (not created or deleted) SLE pairs in a `std::set` during `visitEntry`, then compares fields using a local lambda `fieldChanged` that handles both presence and value differences.

The invariant uses `enforce = view.rules().enabled(featureLendingProtocol)` as its gate. The comment in the code notes that this gate applies even to general `sfLedgerEntryType`/`sfLedgerIndex` checks, because that's when the invariant was introduced — a deliberate coupling of a universal check to a specific feature's amendment lifecycle.

## Assertion Pattern for Developers

The `InvariantCheckPrivilege.h` header documents the recurring `assert(enforce)` pattern explicitly. In debug builds, violating an invariant in a code path where the protecting amendment is not yet enabled is a fatal assertion. In release builds, the same violation logs at `fatal` level but does not fail the transaction. This creates a staged deployment contract: developers see hard failures early, but production nodes on pre-amendment ledgers get observability without breaking consensus.