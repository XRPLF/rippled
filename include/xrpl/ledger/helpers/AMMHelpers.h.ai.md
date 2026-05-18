# `include/xrpl/ledger/helpers/AMMHelpers.h`

This header is the mathematical and operational backbone of XRPL's Automated Market Maker (AMM) implementation. It provides every computation needed to run a constant-product AMM pool — from LP token minting and burning, to spot-price alignment against the central limit order book (CLOB), to swap execution with rigorous rounding guarantees. The functions here are consumed by `AMMLiquidity`, `AMMDeposit`, `AMMWithdraw`, and `AMMBid` transactors, as well as by the payment-path engine.

## Pool Invariant and the Rounding Contract

Every function in this file is written around one non-negotiable invariant:

```
sqrt(poolAsset1 × poolAsset2) >= LPTokensBalance
```

XRPL's `STAmount` type stores only 16 significant decimal digits, which means every multiply-and-convert step introduces a small ULP-level error. Violating the invariant — even by one drop — is a ledger inconsistency. The entire rounding strategy flows from this constraint:

- **Swap-in** (`swapAssetIn`): rounds the output received by the trader **downward**, so the pool retains a tiny excess and the product stays large.
- **Swap-out** (`swapAssetOut`): rounds the input required from the trader **upward**, so again the pool collects slightly more than strictly necessary.
- **LP token issuance**: rounds tokens **downward** on deposit (the pool is worth slightly more per token) and the corresponding asset amounts **upward** (the depositor puts in slightly more).
- **LP token burning**: rounds tokens **upward** on withdrawal (the pool gives up as little as possible) and asset amounts **downward**.

The `fixAMMv1_1` amendment made this rounding more granular. Before the amendment, `swapAssetIn` computed `pool.out - (pool.in * pool.out) / (pool.in + assetIn * feeMult(tfee))` in a single expression with a single round-down at the end. After the amendment, the code explicitly sets `Number::upward` for numerator products and intermediate ratios that should be maximized (to minimize what is given out), and `Number::downward` for the denominator (to maximize the ratio) — each step individually guided. The pre-amendment code path is preserved for historic ledger replay.

The `fixAMMv1_3` amendment extends this discipline to LP token and deposit/withdrawal formulas, replacing `toSTAmount(…, raw_value)` calls with `multiply(balance, frac, rounding_mode)`, ensuring the final multiplication — the step with the most numerical influence — is explicitly directed.

## LP Token Deposit and Withdrawal Formulas

`lpTokensOut`, `ammAssetIn`, `lpTokensIn`, and `ammAssetOut` implement the four quadrant operations: given an asset deposit amount find the tokens earned, given a token amount find the asset deposit required, given an asset withdrawal find the tokens to burn, and given a token burn find the asset returned. These are the XLS-30d AMM standard equations (labelled 3, 4, 7, 8 in the implementation comments). All four involve fees because a single-sided deposit is economically equivalent to a proportional deposit plus a fee-bearing swap; the fee is embedded in the formula through `feeMult(tfee)` and `feeMultHalf(tfee)` from `AMMCore.h`.

`ammLPTokens` is the initial pool seeding formula: `sqrt(asset1 × asset2)`, which sets the geometric mean of pool reserves as the starting LP token supply. This is the standard Uniswap v2 approach and maintains the invariant at equality at creation.

## LP Token Precision Adjustment

`adjustLPTokens` addresses a subtle issue: adding newly-minted tokens to an already-large `LPTokensBalance` field loses significance in the least-significant digit. The workaround is to compute the difference as `(balance + tokens) - balance` rather than just `tokens`. This round-trips through the 16-digit representation and returns the value that will actually be committed to the ledger, which may be slightly less than the calculated tokens. The `adjustAmountsByLPTokens` layer then adjusts the corresponding asset amounts downward to match, preventing the ledger from granting assets that exceed what the LP token math supports.

`getRoundedAsset` and `getRoundedLPTokens` are the amendment-gated wrappers for equal (two-sided) deposit/withdrawal rounding, each available in two overloads: a simple `(balance, frac, isDeposit)` form for direct use, and a callback-based `(noRoundCb, balance, productCb, isDeposit)` form that delays evaluation until inside the function, avoiding recomputing expensive intermediate values. The callbacks exist because the old path (`!fixAMMv1_3`) needs the same formula without controlled rounding, and factoring this out cleanly required deferring the computation.

## Spot Price Quality Alignment

The central challenge in AMM/CLOB co-execution is `changeSpotPriceQuality`. When the payment engine encounters both AMM pools and order book offers for the same currency pair, it asks the AMM to generate a synthetic offer whose quality exactly matches the best CLOB quality. This forces both pools to compete at the same marginal price rather than letting one undercut the other.

The problem is: given current pool reserves `(I, O)` and a target quality `Qt`, find `(i, o)` — the taker-pays and taker-gets — such that the AMM's spot price after the swap equals `Qt` **or** the swap's effective price equals `Qt`, whichever is smaller. These are two different binding constraints:

- **Scenario A** (post-swap spot price = Qt): substituting the swap equation into the spot price condition yields a quadratic in either `i` or `o`.
- **Scenario B** (effective offer price = Qt): the swap equation gives a closed-form linear constraint.

Both constraints are solved and the smaller result is taken to maximize offer quality. `getAMMOfferStartWithTakerGets` is used when the pool pays XRP (IOU-in / XRP-out), while `getAMMOfferStartWithTakerPays` is used for XRP-in/IOU-out and IOU/IOU pools. The `fixAMMv1_1` amendment switched from always starting with `takerPays` to always starting with the XRP side. The reason is that XRP amounts are rounded to integer drops, so rounding the XRP side down has the largest discrete effect on quality. Computing XRP first and then deriving the IOU amount from `swapAssetIn`/`swapAssetOut` ensures the resulting offer quality stays at or above the target rather than falling one drop below it.

The `detail::reduceOffer` helper applies a 99.99% multiplier (rounding toward zero) as a last-resort quality rescue: if the rounded offer still comes out below `targetQuality` due to XRP discretization, reducing the offer by 0.01% brings it back above the target without generating an implausibly small trade.

## Proximity Checks and Tolerance

`withinRelativeDistance` has two overloads: one for `Quality` objects and one for generic numeric types. The `Quality` version cannot use subtraction directly because `Quality` has no arithmetic operators — instead it uses `Quality::rate()`, which is the *inverse* of quality (output/input), converting the "is quality within X% of target?" question into a comparison of rates. The formula compensates for the inversion: `(min.rate - max.rate) / min.rate < dist`. The generic version works straightforwardly. These are used in `changeSpotPriceQuality` to emit a trace-level error only when the quality mismatch exceeds one part in ten million, preventing excessive log noise from harmless floating-point residuals.

## Ledger State Queries

`ammPoolHolds` and `ammHolds` read the AMM's actual trust line balances from a `ReadView`, supporting both frozen-account and authorization checks via `FreezeHandling` and `AuthHandling` flags. `ammHolds` returns an `Expected<tuple, TER>` — a success-or-error type from `xrpl/basics/Expected.h` — because reading the pool state can fail if the AMM's SLE is malformed. `getTradingFee` applies the auction-slot discount: the slot owner and up to four authorized accounts trade at `TRADING_FEE_THRESHOLD / AUCTION_SLOT_DISCOUNTED_FEE_FRACTION` of the normal fee.

## AMM Lifecycle

`deleteAMMAccount` removes all trust lines held by the AMM account and, once the last trust line is gone, deletes the AMM SLE and its on-ledger account. Because each ledger transaction has a bounded work budget, not all trust lines may fit in one transaction; in that case `tecINCOMPLETE` is returned and the caller must submit additional transactions to finish deletion.

`isOnlyLiquidityProvider` detects the single-LP edge case: when a sole LP withdraws, there should be exactly one LP token trust line and no other LPs. `verifyAndAdjustLPTokenBalance` handles the resulting rounding drift — the AMM's recorded `LPTokenBalance` may not equal the last LP's trust line balance after accumulated rounding, so the function corrects the ledger field within a tolerance, enabling the final withdrawal to succeed cleanly.