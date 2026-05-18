# `AMMHelpers.cpp` — AMM Mathematical Engine and Ledger Operations

This file is the implementation core of XRPL's Automated Market Maker. It spans two distinct responsibilities: the closed-form mathematical formulas that determine how assets and LP tokens are exchanged during deposits and withdrawals, and the ledger-state utilities that read, validate, and clean up AMM accounts and trust lines. All code lives in the `xrpl` namespace; the companion header `AMMHelpers.h` additionally defines inline templates (`swapAssetIn`, `swapAssetOut`, `changeSpotPriceQuality`, `getAMMOfferStartWithTakerGets/Pays`) that depend on these primitives.

## AMM Pool Invariant and Fee Encoding

The central invariant enforced throughout this file is `sqrt(asset1 * asset2) >= LPTokenBalance`. `ammLPTokens()` computes the initial LP token supply as the geometric mean of both pool assets. Every subsequent deposit or withdrawal formula is derived from this same invariant, with trading fees baked in to make single-sided operations more expensive than proportional ones.

Trading fees are stored as `uint16_t` in basis points scaled by `AUCTION_SLOT_FEE_SCALE_FACTOR` (100,000), so a value of 1000 represents 1%. The helpers `feeMult(tfee) = 1 - tfee/100000`, `feeMultHalf(tfee) = 1 - tfee/200000`, and `getFee(tfee) = tfee/100000` from `AMMCore.h` are used pervasively and are not re-derived here.

## Single-Sided Deposit and Withdrawal Formulas

The four paired formulas represent the heart of this file:

**Equations 3 and 4** handle single-asset deposits. `lpTokensOut()` implements Equation 3, computing LP tokens minted for a given deposit amount `b` relative to pool balance `B`:

```
t = T * [(b/B - (sqrt(f2²-b/(B·f1))-f2)) / (1 + sqrt(f2²-b/(B·f1))-f2)]
```

`ammAssetIn()` solves the inverse (Eq. 4): given desired LP tokens, what asset deposit is required? The derivation reduces to a quadratic `(R/t2)² + R*(2d/t2 - 1/f1) + d² - f2² = 0`, solved by `solveQuadraticEq()`.

**Equations 7 and 8** handle single-asset withdrawals. `lpTokensIn()` implements Equation 7, computing LP tokens to burn for a given withdrawal. `ammAssetOut()` solves the inverse (Eq. 8), which simplifies to a direct rational expression: `R = (t1² + t1*(f-2)) / (t1*f-1)`.

The quadratic solver `solveQuadraticEq()` returns the positive root `(-b + sqrt(b²-4ac)) / 2a`. Its companion `solveQuadraticEqSmallest()`, used in offer generation (header templates), implements the numerically stable "citardauq" formula from Blinn's paper: when `b > 0` it uses `2c / (-b - sqrt(d))` instead of the standard form, avoiding catastrophic cancellation when both terms under subtraction are nearly equal.

## The `fixAMMv1_3` Rounding Overhaul

Every deposit/withdrawal formula has a pre-amendment and post-amendment code path, making this file a detailed record of how rounding semantics evolved.

**Pre-amendment**: Arithmetic flows through `Number`'s default rounding mode and `toSTAmount()` converts the result at the end.

**Post-amendment**: Directional rounding is applied at the final multiplication step using the `multiply()` function, which wraps `NumberRoundModeGuard` to set the mode for the duration of the call. The invariant dictates opposite directions: LP tokens on deposit round *downward* (we issue fewer tokens), assets on deposit round *upward* (the user pays more), assets on withdrawal round *downward* (the user gets less). The `detail::getLPTokenRounding()` and `detail::getAssetRounding()` inlines in the header encode this logic.

`getRoundedAsset()` and `getRoundedLPTokens()` are the abstraction layer exposing this dispatch. Each has two overloads: one accepting a raw fraction (`Number`), one accepting a `std::function<Number()>` callback. The lambda-based overload exists to avoid computing the formula twice in callers that don't know which code path will be taken — the callback is only evaluated inside the function once the rounding mode is set.

`adjustAssetInByTokens()` and `adjustAssetOutByTokens()` address a secondary problem: after token adjustment (see below), the recalculated asset amount may exceed the original requested amount due to rounding working in an unexpected direction. The fix is to reduce the requested amount by the excess, recalculate tokens and then the asset, and return the minimum.

## LP Token Precision Loss (`adjustLPTokens`)

`STAmount` maintains 16 significant digits. When LP tokens are added to the AMM's total balance (e.g. `balance + tokens`), the sum may be rounded to fit 16 digits, meaning `(balance + tokens) - balance < tokens`. Naively recording this as the new token count would undercount the minted tokens.

`adjustLPTokens()` fixes this by computing the amount the *other way around*: for a deposit it returns `(lptAMMBalance + lpTokens) - lptAMMBalance`, which applies the same precision loss to both sides and therefore cancels out the truncation. For withdrawal it uses `(lpTokens - lptAMMBalance) + lptAMMBalance`. The function forces `Number::downward` rounding so the adjusted token count is never more than the requested count. `adjustAmountsByLPTokens()` wraps this: under `fixAMMv1_3` it returns immediately (the new rounding strategy makes this adjustment unnecessary), but under older amendments it propagates the adjusted token count back into the asset amounts.

## Ledger State Queries

`ammHolds()` is the principal read function. It returns a three-tuple `(asset1, asset2, LPTokenBalance)` via `Expected<tuple, TER>`. The optional asset parameters let callers specify which pool side they care about — if only one is given, the function identifies the matching pool asset and returns the pair with the requested asset first. An invalid pair triggers `tecAMM_INVALID_TOKENS` and the unreachable error paths are annotated `LCOV_EXCL_START`.

`ammLPHolds()` deliberately avoids reusing `accountHolds()`. The comment explains the distinction: `accountHolds()` checks whether the underlying pool assets are frozen (gated by `fixFrozenLPTokenTransfer`), but LP token *balance* queries should only check whether the LP token trustline itself is frozen, not the pool assets.

`ammAccountHolds()` reads raw balances without the balance hook, using `Asset::visit()` to dispatch over both IOU and MPT cases. The AMM account can hold either type as its pool assets.

`getTradingFee()` returns the effective fee for a specific account: if the account holds a valid (non-expired) auction slot, it returns `sfDiscountedFee`; otherwise the global `sfTradingFee`. The expiration check compares `parentCloseTime` in seconds against the slot's stored expiration, which is `parentCloseTime + TOTAL_TIME_SLOT_SECS` (24 hours) set at initialization time.

## AMM Account Lifecycle and Cleanup

`deleteAMMAccount()` orchestrates full account teardown in a specific order that matters for correctness:

1. `deleteAMMTrustLines()` sweeps the owner directory, removing zero-balance IOU trustlines (`ltRIPPLE_STATE`). Any non-zero balance returns `tecINTERNAL`, which is annotated as unreachable under correct business logic. MPToken and AMM entries are skipped.

2. Only if trustlines are fully deleted does `deleteAMMMPTokens()` run. The ordering is intentional: if the AMM cannot fully delete trustlines (e.g., `tecINCOMPLETE` is returned upstream), the AMM can be recreated via a new deposit, and any MPToken objects for the pool assets must remain for that path to work.

3. After both passes, the owner directory link and both the AMM SLE and the AMM root account SLE are erased from the `Sandbox`.

The `deleteAMMTrustLines` limit parameter (`maxDeletableAMMTrustLines`) allows partial deletion, returning `tecINCOMPLETE` if the directory isn't fully drained, enabling multi-transaction deletion. MPTokens allow at most three items (two pool-side MPTs plus the AMM object), so a fixed limit of 3 suffices there.

## Fee Auction Initialization

`initializeFeeAuctionVote()` is called both on `AMMCreate` and whenever a depleted AMM receives its first deposit. It writes a single vote entry with `VOTE_WEIGHT_SCALE_FACTOR` (100%) weight for the creator, then constructs the auction slot with a 24-hour expiration and a `sfPrice` of zero (the creator gets the slot for free). The discounted fee is `tfee / AUCTION_SLOT_DISCOUNTED_FEE_FRACTION` (one-tenth of the full fee), and both fee fields are conditionally omitted via `makeFieldAbsent()` if the value is zero, preserving the canonical encoding of absent-field serialization.

## Last-LP Balance Reconciliation

`isOnlyLiquidityProvider()` walks the AMM account's owner directory (up to 10 pages, sufficient for at most four ledger objects) and classifies each entry as the AMM SLE, an LPToken trustline, an IOU pool-asset trustline, or an MPToken. If any non-LP LPToken trustline appears, it returns `false` immediately — there are other LPs. Final validation checks that exactly one LPToken trustline exists and between one and two pool-asset entries (IOU or MPT) are present.

`verifyAndAdjustLPTokenBalance()` uses this to patch the AMM's `sfLPTokenBalance` during a final withdrawal. The stored balance and the LP's actual trustline balance may differ by a small fraction due to the 16-digit precision limit. If the discrepancy is within 0.1% (tolerance `Number{1, -3}`), the AMM balance is silently updated; if it exceeds this, `tecAMM_INVALID_TOKENS` is returned to reject the transaction.