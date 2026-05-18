# `include/xrpl/tx/invariants/LoanBrokerInvariant.h`

## Role in the System

`LoanBrokerInvariant.h` declares `ValidLoanBroker`, one of the invariant checker classes registered in `InvariantChecks` (the master tuple in `InvariantCheck.h`). It is part of the XRPL Lending Protocol (XLS-0066) and runs unconditionally after every transaction, serving as a final correctness gate: if any `LoanBroker` ledger object is found to be internally inconsistent, the transaction is rolled back regardless of what the transaction logic returned.

Like every invariant checker in the codebase, `ValidLoanBroker` exposes exactly two public methods — `visitEntry()` and `finalize()` — matching the `InvariantChecker_PROTOTYPE` interface. The infrastructure calls `visitEntry` once per modified ledger entry and then calls `finalize` exactly once to deliver a pass/fail verdict.

## Key Design: Three-Channel Entry Collection

The private state reveals how `ValidLoanBroker` handles the unusual topology of the Lending Protocol. During `visitEntry`, modified entries are sorted into three separate collections:

- `brokers_` — a `std::map<uint256, BrokerInfo>` keyed on the broker ledger index. Entries are added directly when a `ltLOAN_BROKER` SLE is touched, or indirectly when an `ltACCOUNT_ROOT` carrying `sfLoanBrokerID` is touched (pseudo-accounts representing the broker).
- `lines_` — modified `ltRIPPLE_STATE` (trust line) entries.
- `mpts_` — modified `ltMPTOKEN` entries.

The trust lines and MPToken collections exist because a transaction may affect a broker's state without directly touching the `LoanBroker` SLE itself. In `finalize()`, both endpoints (high/low issuer) of each trust line are resolved via `view.read(keylet::account(...))`, and the MPToken's owner account is similarly resolved. If either endpoint carries `sfLoanBrokerID`, that broker is added to the `brokers_` map. This deferred discovery is necessary because the invariant must validate any broker implicated by a transaction, even if the broker object was only touched indirectly through associated trust lines or token balances.

The `BrokerInfo` struct is minimal by design — it stores both the pre-transaction (`brokerBefore`) and post-transaction (`brokerAfter`) SLE snapshots. Only `brokerAfter` is used for absolute-value checks; `brokerBefore` is used for monotonicity checks that require a before/after comparison.

## Invariants Enforced in `finalize()`

Once all implicated brokers are gathered, `finalize()` performs several checks on each:

**Zero-OwnerCount directory structure** — the most structurally unusual check. If `sfOwnerCount == 0`, the broker's owner directory must have at most a single root page with at most one entry, and that entry (if present) must be either a `ltRIPPLE_STATE` or `ltMPTOKEN`. This is enforced by the static helper `goodZeroDirectory()`, which reads `sfIndexNext` and `sfIndexPrevious` to confirm there is no chained page, then reads `sfIndexes` to verify the single-entry constraint, and finally resolves the referenced object to check its type. A broker with zero owner-count holding anything besides a trust line or token balance represents a structural corruption.

**Sequence monotonicity** — `sfLoanSequence` must not decrease between transactions. Because `before` may be `nullptr` for newly-created brokers, this check is guarded.

**Non-negative financial fields** — `sfDebtTotal` and `sfCoverAvailable` must each be ≥ 0. These represent the broker's outstanding debt and available coverage capacity; negative values would indicate arithmetic corruption.

**Vault linkage** — every broker must reference a valid `ltVAULT` object via `sfVaultID`. A missing vault means the broker's backing store has been destroyed while the broker still exists, which is an illegal ledger state.

**Cover–balance relationship** — `sfCoverAvailable` must not be less than the broker pseudo-account's actual asset balance (computed via `accountHolds`). When the `fixSecurity3_1_3` amendment is active, an additional upper-bound check is enforced: `sfCoverAvailable` must not exceed the pseudo-account balance, except during a `ttLOAN_BROKER_DELETE` transaction (where `sfCoverAvailable` is intentionally left un-zeroed at deletion). This two-sided bound is a security hardening: the available cover must precisely reflect what the pseudo-account actually holds.

## Relationship to Sibling Invariants

`ValidLoanBroker` is more structurally complex than its peers `ValidLoan` and `ValidVault`. `ValidLoan` uses a flat vector of before/after pairs and checks a single numeric invariant. `ValidVault` uses separate before/after vectors and cross-checks share and asset quantities. `ValidLoanBroker` uses a map keyed on broker ID because the same broker may be reached through multiple paths (direct SLE touch, pseudo-account modification, and trust line / MPToken resolution), and deduplication via `std::map::emplace` prevents double-checking. The `goodZeroDirectory` helper exists as a static method rather than inlined into `finalize` because the directory-page-chain traversal logic is non-trivial enough to warrant isolation and separate testing.

The file itself is a thin declaration; all logic lives in `src/libxrpl/tx/invariants/LoanBrokerInvariant.cpp`. The header exists so that `InvariantCheck.h` can include it alongside all other invariant checkers and add `ValidLoanBroker` to the `InvariantChecks` tuple without exposing implementation details to callers.