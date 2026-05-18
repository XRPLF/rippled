# `LoanBrokerInvariant.cpp`

## Role in the System

This file implements `ValidLoanBroker`, one of the per-transaction invariant checkers that run after every transaction is applied to the XRPL ledger. It belongs to the invariant check framework defined alongside `InvariantCheck.cpp` and is specific to the Lending Protocol amendment (XLS-66). Its purpose is to verify that `LoanBroker` ledger objects and their associated state remain internally consistent after each transaction commits — catching bugs in transaction processing before they corrupt live ledger state.

The Lending Protocol introduces a `LoanBroker` as a pseudo-account-backed object that coordinates collateralized loans from a `Vault`. A broker holds cover collateral in a pseudo-account and tracks its `sfDebtTotal` (outstanding loans) and `sfCoverAvailable` (collateral available for liquidation). The invariant exists to ensure these accounting fields can never drift out of sync with on-ledger balances.

## Two-Phase Design

Like every invariant in the framework, `ValidLoanBroker` operates in two phases. `visitEntry()` is called once per modified ledger entry (SLE) during transaction application, accumulating relevant objects into member collections. After all entries are visited, `finalize()` performs the actual validation across the accumulated state. This split is necessary because a single transaction can touch multiple objects that together reveal a constraint violation: no single SLE change is inherently wrong in isolation.

## Entry Collection (`visitEntry`)

The collector is deliberately broad. It classifies each post-transaction `after` SLE into one of four buckets:

- **`ltLOAN_BROKER`** — added to `brokers_` with both before/after snapshots stored in a `BrokerInfo` struct.
- **`ltACCOUNT_ROOT` with `sfLoanBrokerID` present** — indicates a broker pseudo-account was touched. Its `sfLoanBrokerID` field is used as a key in `brokers_`, creating a placeholder `BrokerInfo{}` entry if none exists. This ensures the broker is checked even if the `ltLOAN_BROKER` object itself was not directly modified.
- **`ltRIPPLE_STATE`** — collected into `lines_` for deferred issuer lookup.
- **`ltMPTOKEN`** — collected into `mpts_` for deferred account lookup.

The `isDelete` flag is deliberately not used here. The post-state `after` is what matters for all invariant checks except the sequence monotonicity check, which compares `before` against `after`.

## Indirect Broker Discovery in `finalize`

The most architecturally interesting aspect of this invariant is its approach to incomplete visibility. A transaction may modify trust lines or MPTokens held by a broker's pseudo-account without ever directly touching the `ltLOAN_BROKER` SLE itself. To catch such cases, `finalize` iterates through all collected `lines_` and `mpts_`, reads the account root for each issuer/holder, and if that account carries `sfLoanBrokerID`, adds the broker to the tracking map.

This lazy discovery strategy avoids the `visitEntry` phase needing to speculatively read the full ledger for every modified trust line or MPToken. The `emplace` call uses the insert-if-absent semantics of `std::map::emplace`, so brokers already discovered directly are not overwritten.

At the end of this discovery pass, `brokers_` may contain entries where `brokerBefore` and `brokerAfter` are both null — just the ID is known. In that case, `finalize` reads the broker SLE from the current view via `keylet::loanbroker(brokerID)`. If the object is missing from the ledger entirely, the invariant fails immediately with `"Loan Broker missing"`.

## `goodZeroDirectory`

This private static helper enforces a property stated in the XLS-66 spec: when a broker's `sfOwnerCount` is zero, its owner directory must be a single-page root containing at most one entry, and that entry may only be an `ltRIPPLE_STATE` or `ltMPTOKEN` object. The reasoning is that a broker with no outstanding loans or other obligations should hold essentially no owned objects — the only exception being the trust line or MPToken through which the cover collateral is held.

The check examines `sfIndexNext` and `sfIndexPrevious` on the directory root using optional-field access (`dir->at(~sfIndexNext)`). If either is nonzero the directory has multiple pages, which is disallowed. It then inspects `sfIndexes` directly: more than one entry is a failure; exactly one entry must resolve to a valid SLE of the permitted types.

## Core Invariants Checked in `finalize`

For each tracked broker the following conditions must hold:

**Sequence monotonicity.** `sfLoanSequence` must never decrease. Since this sequence is used to derive unique loan keys, a decrement would allow replay of previously-issued loan IDs.

**Non-negative accounting.** Both `sfDebtTotal` and `sfCoverAvailable` must be ≥ 0. Because these are `STNumber` fields, negative values are representable and would indicate a bookkeeping bug.

**Vault linkage.** The broker's `sfVaultID` must reference a `Vault` object that still exists in the ledger. A dangling reference would prevent later loan operations from functioning.

**Cover/balance relationship.** `sfCoverAvailable` tracks how much of the pseudo-account's asset balance is committed as collateral cover. It must never exceed the pseudo-account's actual on-ledger balance (checked via `accountHolds` with freeze and auth handling both set to ignore, since pseudo-accounts are exempt from those controls). If `sfCoverAvailable > pseudoBalance` the broker is claiming more coverage than it actually holds.

**Tighter equality under `fixSecurity3_1_3`.** A subsequent security fix amendment further tightens the upper bound: `sfCoverAvailable` must equal `pseudoBalance`, not merely be ≤ it. The single exception is `ttLOAN_BROKER_DELETE`, where the broker object is being removed and `sfCoverAvailable` is deliberately not zeroed before deletion — checking the field at that point would produce a false positive.

## Amendment Gating

Unlike some invariants that conditionally enforce based on whether a relevant amendment is active, `ValidLoanBroker::finalize` has no amendment gate. The comment explains the rationale: `ltLOAN_BROKER` objects cannot exist in the ledger unless the Lending Protocol amendment has already been enabled. If the code reaches the broker validation loop there must be live broker state, so the amendment is implicitly confirmed by the data's presence.