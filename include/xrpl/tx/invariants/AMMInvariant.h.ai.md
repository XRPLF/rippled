# `AMMInvariant.h` — Post-Transaction Validity Guard for AMM Ledger State

## Role in the System

`AMMInvariant.h` declares `ValidAMM`, one of many invariant-checker classes that the XRPL transaction engine runs as a last line of defense after applying every transaction. It lives in the `InvariantChecks` tuple defined in `InvariantCheck.h`, which is iterated uniformly after each transaction commits. Its sole job is to detect impossible or corrupt AMM state that should never arise from correct code — and, once the `fixAMMv1_3` amendment is active, to reject the transaction if that state is observed.

## The Two-Phase Interface

Like every invariant checker in the system, `ValidAMM` exposes exactly two public methods: `visitEntry` and `finalize`. The framework calls `visitEntry` once per modified ledger entry, then calls `finalize` to render a verdict.

`visitEntry` collects the diff between ledger state before and after the transaction by inspecting the `before` and `after` SLE snapshots. It ignores deletions entirely and only records three pieces of state:

- `ammAccount_` — the AMM's pseudo-account ID, populated when an `ltAMM` object is modified
- `lptAMMBalanceAfter_` / `lptAMMBalanceBefore_` — the LP token supply from the AMM object's `sfLPTokenBalance` field, snapped from both versions
- `ammPoolChanged_` — a flag set when any `ltRIPPLE_STATE` entry carrying `lsfAMMNode`, or any `ltACCOUNT_ROOT` with an `sfAMMID` field, is touched

This compact snapshot is all that's needed to verify consistency across all AMM transaction types.

## The Mathematical Core: `generalInvariant`

The most important logic sits in the private `generalInvariant` method, used by both deposit and withdrawal paths. It re-reads the actual pool balances from the ledger via `ammPoolHolds`, then checks:

```
sqrt(amount × amount2) ≥ lptAMMBalanceAfter_
```

This is the constant-product invariant: the geometric mean of pool reserves must never fall below the LP token supply. The check uses a strong comparison first, then falls back to `withinRelativeDistance` with a tolerance of `1e-11` to absorb floating-point rounding that can arise in fixed-point arithmetic. Crucially, logging for a failed `generalInvariant` is unconditional — it always emits a detailed error line including the transaction hash, individual pool amounts, geometric mean, LP token balance, and relative deviation — before the `enforce` flag gates whether to return `false`.

## Per-Transaction Dispatch in `finalize`

`finalize` first short-circuits on any failure result that isn't `tesSUCCESS` or `tecINCOMPLETE`. The `tecINCOMPLETE` carve-out exists because `AMMDelete` is allowed to return that code when there are too many trustlines to clean up in a single transaction, yet the partial deletion still needs validation.

It then reads the `enforce` flag from `view.rules().enabled(fixAMMv1_3)`. Before this amendment, invariant failures are logged but the method still returns `true`, preserving backward compatibility. After the amendment, violations fail the transaction. The dispatch is a switch over the transaction type:

- **`ttAMM_CREATE`** (`finalizeCreate`): Verifies that an AMM object was actually created, then re-derives the expected LP token balance as `sqrt(amount × amount2)` using `ammLPTokens` and asserts it matches the recorded balance. All three values (both pool amounts and LP supply) must be strictly positive. This is the only invariant using exact equality rather than a `≥` bound.

- **`ttAMM_DEPOSIT`** (`finalizeDeposit`): Confirms the AMM object still exists, then delegates to `generalInvariant` with `ZeroAllowed::No`. A deposit can never produce a zero pool.

- **`ttAMM_WITHDRAW` and `ttAMM_CLAWBACK`** (`finalizeWithdraw`): If `ammAccount_` is absent, the AMM was deleted by the last withdrawal — this is legitimate and the method returns `true` immediately. Otherwise it calls `generalInvariant` with `ZeroAllowed::Yes`, which additionally permits the all-zeros case (both pool amounts and LP supply simultaneously zero) as a valid terminal state during a full drain.

- **`ttAMM_BID`** (`finalizeBid`): The pool itself must not change — bidding for the auction slot only burns LP tokens. If it did change, that's a violation. If the pool is untouched, it then verifies that `lptAMMBalanceAfter_ ≤ lptAMMBalanceBefore_` and that the post-bid balance is positive. LP tokens are consumed as the bid price, so they must decrease.

- **`ttAMM_VOTE`** (`finalizeVote`): Both LP token balance and pool state must be unchanged. Voting only updates fee parameters on the AMM object; it must not touch reserves.

- **`ttAMM_DELETE`** (`finalizeDelete`): On `tesSUCCESS`, `ammAccount_` must be absent (the object was deleted). On `tecINCOMPLETE`, the object must similarly not have been modified during the partial attempt.

- **`ttCHECK_CASH`, `ttOFFER_CREATE`, `ttPAYMENT`** (`finalizeDEX`): DEX operations route through AMM pools but should never modify the `ltAMM` object itself. If `ammAccount_` is set, an object that should only be read was written — a serious bug.

## Design Choices Worth Noting

The `ZeroAllowed` scoped enum (`No` / `Yes`) instead of a bare `bool` makes the intent self-documenting at call sites, which matters because the two values represent meaningfully different economic scenarios.

The `LCOV_EXCL_START/STOP` brackets around most failure branches signal that those code paths are considered unreachable under correct operation — they exist to catch implementation bugs, not expected error conditions, so test coverage tools rightly exclude them. The one exception is `finalizeCreate`'s error log, which lacks these guards and is therefore expected to be reachable in testing.

The invariant's relationship to the broader `InvariantChecks` tuple is compositional and stateless at the framework level: each checker is constructed fresh per transaction, `visitEntry` accumulates deltas, and `finalize` renders its verdict. `ValidAMM` fits this pattern perfectly — its three `std::optional` members and one `bool` represent exactly the minimum diff state needed, with no heap allocation and no shared mutable state.