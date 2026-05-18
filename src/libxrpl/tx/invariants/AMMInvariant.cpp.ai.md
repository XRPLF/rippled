# `AMMInvariant.cpp` — Post-transaction Invariant Checker for AMM State

## Role in the System

This file implements `ValidAMM`, one of the specialized invariant checkers that XRPL runs as a final safety net after every transaction is applied. It participates in the tuple-based `InvariantChecks` system defined in `InvariantCheck.h`, where every checker exposes two methods: `visitEntry`, called once per modified ledger entry, and `finalize`, called when the transaction is complete.

The purpose of `ValidAMM` is to assert that AMM-related ledger state is mathematically consistent after any transaction that touches an AMM — whether that transaction is purpose-built (`ttAMM_CREATE`, `ttAMM_DEPOSIT`, etc.) or incidental (a `ttPAYMENT` or `ttOFFER_CREATE` that routed through AMM liquidity). Invariants in XRPL are the last line of defense: if a bug in the transaction processor allows illegal state to form, the invariant checker can reject the transaction entirely before it commits to the ledger.

## State Accumulation in `visitEntry`

`ValidAMM` uses a two-phase design that is common across all XRPL invariant checkers. During the first phase, `visitEntry` scans every SLE (serialized ledger entry) touched by the transaction — both before and after mutation — and records three pieces of state:

- `ammAccount_`: the account ID of the AMM pseudo-account, populated from any `ltAMM` entry seen in `after`.
- `lptAMMBalanceAfter_` and `lptAMMBalanceBefore_`: the LP token supply recorded from the `after` and `before` snapshots of the `ltAMM` object, respectively.
- `ammPoolChanged_`: a boolean set to `true` if any trust-line entry bearing `lsfAMMNode`, or any account root carrying `sfAMMID`, was touched — meaning the on-pool reserves themselves changed.

Deletion events are ignored entirely; the `isDelete` flag causes an early return, since object removal is handled through the presence or absence of `ammAccount_` and the LP balance fields after the fact.

The pool-change detection deliberately unifies two SLE types: `ltRIPPLE_STATE` entries tagged `lsfAMMNode` hold the fungible-token reserves, while `ltACCOUNT_ROOT` entries tagged `sfAMMID` hold the XRP reserve. Either kind of mutation sets `ammPoolChanged_`.

## The `finalize` Dispatch

After all entries are visited, `finalize` is called with the full transaction and a `ReadView`. It immediately discards failed transactions — with the deliberate exception of `tecINCOMPLETE`, which arises during `ttAMM_DELETE` when there are too many trust lines to delete in a single pass. All other TER codes that are not `tesSUCCESS` skip invariant checks, since failed transactions must not modify AMM-relevant state in the first place (a separate invariant elsewhere would catch that).

A single `bool enforce` flag is derived from `view.rules().enabled(fixAMMv1_3)`. This is the XRPL amendment-activation pattern: when the amendment is not yet active, violations are logged but do not cause the transaction to fail. Once the amendment activates on-network, violations become hard failures that reject the transaction. This lets the invariant ship before activation without disrupting the network.

The dispatch then branches by transaction type, delegating to one of seven `finalize*` methods.

## Per-transaction Invariants

**`finalizeVote`**: An `AMMVote` transaction adjusts fee parameters through a weighted-vote mechanism. It must not change the LP token supply or the pool reserves at all. The check simply compares `lptAMMBalanceBefore_` and `lptAMMBalanceAfter_` for equality and asserts `ammPoolChanged_` is false.

**`finalizeBid`**: An `AMMBid` transaction burns LP tokens to win the auction slot. The pool reserves are untouched (`ammPoolChanged_` must be false), and the LP token supply must strictly decrease and remain above zero.

**`finalizeCreate`**: This is the most mathematically demanding case. After creation, `finalizeCreate` reads the live pool balances using `ammPoolHolds`, then calls `ammLPTokens` (which computes `sqrt(asset1 * asset2)`) and checks for exact equality with the recorded LP token balance. All three balances — both pool assets and the LP supply — must be strictly positive. There is no tolerance here: at creation the geometric-mean formula must hold exactly.

**`finalizeDelete`**: On a successful `AMMDelete`, the AMM object must no longer exist, so `ammAccount_` must be empty. On `tecINCOMPLETE` (partial deletion), the object must also be unmodified from the invariant's perspective — `ammAccount_` must still be absent because the check relies on it not having been touched in a way that would be visible here.

**`finalizeDEX`**: DEX transactions — `ttPAYMENT`, `ttOFFER_CREATE`, and `ttCHECK_CASH` — route through AMM pool swaps but must never modify the `ltAMM` ledger object itself (only the pool trust-lines change). If `ammAccount_` is populated it means the AMM object was incorrectly written, and the invariant fails.

**`finalizeDeposit` / `finalizeWithdraw`**: Both delegate to `generalInvariant` after confirming the AMM object still exists for deposit (and handling the case of final withdrawal deleting it).

## The General Pool Invariant

`generalInvariant` encodes the core AMM mathematical property for deposits and withdrawals:

```
sqrt(poolAsset1 × poolAsset2) ≥ LPTokenBalance
```

This is the constant-product invariant expressed in geometric-mean form. The strong check is a direct `>=` comparison. Because floating-point rounding in `STAmount` arithmetic can produce a `poolProductMean` that is very slightly *less* than the LP balance even when the operation was mathematically valid, a weak fallback is also provided: `withinRelativeDistance` accepts a relative error up to `1e-11`.

The `ZeroAllowed` enum handles the edge case cleanly: on withdrawal, if the last LP redeems all tokens, the pool drains to zero and the AMM object is deleted. `ZeroAllowed::Yes` allows the condition where all three quantities are simultaneously zero, which is the correct post-state for a final withdrawal or clawback. Deposits, by contrast, always require strict positivity (`ZeroAllowed::No`).

## Enforcement and Defensive Patterns

The `LCOV_EXCL_START/STOP` markers on most error branches reflect the expectation that these paths should be unreachable if transaction processing is correct. They exist as a belt-and-suspenders guard against hypothetical bugs, not as regular error paths. The `enforce` flag separates the diagnostic role (always log) from the enforcement role (only reject when the amendment is active), allowing safe deployment before full network activation of `fixAMMv1_3`.