# AMMLiquidity.cpp

`AMMLiquidity` is the adapter between an on-ledger Automated Market Maker pool and the XRPL payment engine's offer-book traversal layer (`BookStep`). It generates synthetic offers from live AMM pool state so that `BookStep` can treat AMM liquidity identically to limit-order-book (CLOB) offers during payment path execution.

## Role in the Payment Engine

Payment execution in the XRPL walks through book steps, consuming offers one at a time. CLOB offers are finite discrete records; an AMM pool is a continuous curve. `AMMLiquidity` solves this mismatch by producing `AMMOffer<TIn, TOut>` objects — value types that mirror the `TOffer` interface — sized according to the current interaction context. A single `AMMLiquidity` instance lives for the duration of one payment's book-step execution; it owns the `initialBalances_` snapshot taken at construction while calling `fetchBalances()` freshly each time `getOffer()` is invoked so it always prices against the pool's current state after prior swap activity.

## Two Distinct Offer Generation Modes

The class bifurcates its strategy entirely on whether `AMMContext::multiPath()` is true.

**Single-path mode** (one payment path, competing with CLOB): `getOffer()` calls `changeSpotPriceQuality()` to solve for swap amounts that, if consumed, would move the AMM's spot-price quality exactly to the CLOB offer quality passed in as `clobQuality`. This is the mathematically correct approach: it computes the exact input/output pair satisfying the constant-product invariant and the quality constraint, dispatching to `getAMMOfferStartWithTakerGets` or `getAMMOfferStartWithTakerPays` depending on which asset is XRP (to round XRP first and maximise quality). If `changeSpotPriceQuality` cannot produce a valid offer (e.g., pool too small or fee too high), the `fixAMMv1_2` amendment allows falling back to `maxOffer` provided that offer's quality still beats the CLOB's.

When `clobQuality` is entirely absent — meaning there is no competing CLOB and the path is unconstrained — `getOffer()` returns `maxOffer()`, which deliberately over-sizes the offer to the theoretical maximum. The actual consumed amount is trimmed later by `BookStep` according to send-max, deliver limits, or available funds.

**Multi-path mode** (multiple payment paths): calling `changeSpotPriceQuality` independently on each path would cause each path to consume the whole quality differential, leading to double-counting. Instead, `generateFibSeqOffer` produces a sequence of exponentially growing synthetic offers keyed to `AMMContext::curIters()`, which counts how many payment-engine iterations have already consumed an AMM offer. The starting offer is a tiny fraction of the pool (`InitialFibSeqPct = 5/20000 = 0.025%` of the in-asset balance) and the output is then scaled by the Fibonacci multiplier for the current iteration:

```
fib[] = {1, 2, 3, 5, 8, 13, 21, …, 1346269}  // 30 entries
cur.out = (base_out) × fib[curIters - 1]
```

At iteration 0 the base offer is returned directly; from iteration 1 onward the prior output is scaled by successive Fibonacci numbers. The sequence grows to cover the full pool asymptotically while guaranteeing each individual offer is physically valid (`cur.out < balances.out`; violation throws `std::overflow_error`). The iteration counter is capped at `AMMContext::MaxIterations = 30` — enforced by both an early exit in `getOffer()` and an `XRPL_ASSERT` inside `generateFibSeqOffer`. This cap prevents AMM offers from indefinitely dominating the offer-counter budget that `BookStep` does not otherwise apply to AMM.

## The maxOffer and Its Amendment Duality

`maxOffer()` exists in two behaviours controlled by the `fixAMMOverflowOffer` amendment. Pre-amendment, it constructs an offer with `takerPays = maxAmount<TIn>()` (the protocol ceiling for the type) and lets `swapAssetIn` compute the corresponding output. This is safe mathematically but can produce astronomically large offers that overflow intermediate calculations, hence it was the source of the overflow exception previously swallowed inside `getOffer`. Post-amendment, the function instead sizes output at 99% of the current pool balance (`out = 0.99 × balances.out`) via the file-local `maxOut()` helper, then back-calculates input via `swapAssetOut`. This is bounded and cannot overflow. If the capped output would be zero or would equal the full balance (corner case for tiny pools), `maxOffer()` returns `std::nullopt`.

## Quality Threshold Guard

Before any offer is generated, `getOffer()` computes the pool's current spot-price quality and bails out if it is no better than the CLOB quality, or if it is within a relative distance of 1×10⁻⁷ of it. This threshold is intentional: after a partial swap the new spot price may approach but never quite reach the target due to floating-point rounding, and without the threshold the loop could iterate fruitlessly for many rounds.

## Template Instantiation and Type System

The class is parameterised on `<TIn, TOut>` constrained by `StepAmount`. The translation unit closes with eight explicit instantiations covering all legal asset-type pairings: `IOUAmount`×`IOUAmount`, `XRPAmount`×`IOUAmount`, `IOUAmount`×`XRPAmount`, and the five `MPTAmount` combinations. Each pairing has distinct rounding semantics inside `swapAssetIn` / `swapAssetOut`, which always round in the AMM's favour — outputs are rounded down, inputs are rounded up — to preserve the constant-product invariant under integer representation constraints.

## Error Handling Architecture

`getOffer()` wraps all offer computation in a try/catch block. An `std::overflow_error` — thrown by `generateFibSeqOffer` when the Fibonacci-scaled output exceeds the pool — is caught here. Pre-`fixAMMOverflowOffer`, the fallback is to attempt `maxOffer`; post-amendment it returns `std::nullopt` instead, letting the payment engine continue with whatever CLOB offers remain. Any other `std::exception` is logged and suppressed, returning `std::nullopt`. This layered approach means a problematic AMM pool cannot abort an entire payment transaction; it simply ceases to contribute liquidity for that path.