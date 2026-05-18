# `AMMLiquidity.h` — AMM Offer Generation for the Payment Engine

## Role in the System

`AMMLiquidity` is the adapter that makes an Automated Market Maker (AMM) pool appear as a sequence of synthetic offers to the XRPL payment engine's `BookStep`. The payment engine was originally designed around the Central Limit Order Book (CLOB), where discrete offers are popped off in price-sorted order. AMMs, by contrast, offer continuous liquidity with a price that slides as the pool is consumed. `AMMLiquidity` bridges these two paradigms by fabricating virtual offers on demand, sized according to a strategy that depends on whether the payment has one path or multiple paths.

## Template Structure and Instantiations

The class is templated on `TIn` and `TOut`, representing the amount types for the two pool assets. The implementation file explicitly instantiates all eight valid combinations (`IOUAmount`/`XRPAmount`/`MPTAmount` pairs), making this a classic extern-template pattern: the header declares the interface, the `.cpp` defines it once, and the linker provides the symbols. This avoids bloating every translation unit that includes the header with redundant instantiations.

## Construction and Snapshot of Initial Balances

The constructor takes a `ReadView`, the AMM account ID, the trading fee in basis points, the two assets, a reference to the shared `AMMContext`, and a journal for logging. It immediately calls `fetchBalances()` and stores the result in `initialBalances_` — a `const` member. This snapshot matters: the Fibonacci offer-sizing logic in `generateFibSeqOffer()` scales each iteration's offer as a multiplier of `initialBalances_.in`, not the current (depleted) balances. The rationale is that offer sizes should be deterministic across iterations given the same starting state; using live balances would create feedback loops where earlier iterations change the sizes of later ones unpredictably.

## Two Offer-Generation Strategies

### Multi-Path: Fibonacci Sequence

When the payment transaction specifies multiple paths (`ammContext_.multiPath()` is true), AMM offers must compete with CLOB offers across strands. In this regime, `getOffer()` calls `generateFibSeqOffer()`, which produces an offer whose output amount is:

```
out_i = initialOut × (fib[i−1])
```

where `fib` is the standard Fibonacci sequence and `i` is `ammContext_.curIters()` — the count of payment engine iterations that have already consumed an AMM offer. The Fibonacci growth means early iterations get small offers (preserving price quality), and later iterations can consume exponentially larger slices if needed. The sequence is hard-coded to 30 entries, matching `AMMContext::MaxIterations`. If a computed output equals or exceeds the current pool balance, the function throws `std::overflow_error`, which `getOffer()` catches and converts to `std::nullopt`.

The 30-iteration cap exists because AMM offers don't increment `BookStep`'s internal CLOB offer counter. Without an independent limit, the payment engine could loop indefinitely against an AMM pool. `AMMContext::maxItersReached()` gates every `getOffer()` call, returning `std::nullopt` before any work is done once the cap is hit.

### Single-Path: Spot-Price Quality Matching

With a single path, there is no cross-strand competition, so the goal shifts to maximising value extraction against the current CLOB. `getOffer()` uses `changeSpotPriceQuality()` (from `AMMHelpers.h`) to compute an offer sized so that, if fully consumed, the pool's new spot price equals the best competing CLOB offer's quality. This is the key insight: instead of taking a fixed slice, the AMM offer is sized to exactly meet the CLOB's price level, letting `BookStep` determine how much of it to actually use.

If `changeSpotPriceQuality()` returns nothing (e.g., the CLOB quality is already worse than the AMM's spot price) but the `fixAMMv1_2` amendment is active, `getOffer()` falls back to `maxOffer()`. If there is no competing CLOB offer at all (`!clobQuality`), `maxOffer()` is used directly.

## The `maxOffer()` Method and `fixAMMOverflowOffer`

`maxOffer()` generates the largest safe synthetic offer against the pool. Under the `fixAMMOverflowOffer` amendment, it caps `takerGets` at 99% of `balances.out` (computed by the local `maxOut()` helper) and derives `takerPays` via `swapAssetOut()`. It returns `std::nullopt` if that 99% cap rounds to zero or equals the full balance — a safety valve against degenerate pool states. Before the amendment was active, the function used `maxAmount<TIn>()` (the protocol-level ceiling) as `takerPays` and derived `takerGets` via `swapAssetIn()`, which could produce arithmetic overflow on large pools — the bug that motivated the amendment.

## Quality Gating in `getOffer()`

Before generating any offer, `getOffer()` computes the pool's current spot-price quality (`Quality{balances}`) and compares it to `clobQuality`. If the CLOB offer is at least as good — or within a relative threshold of 1e-7 — the AMM cannot profitably compete and `std::nullopt` is returned. The threshold prevents a degenerate oscillation where the spot price keeps approaching the CLOB quality without converging, burning iterations needlessly.

## `AMMContext` as Shared State

`AMMContext` is a single instance created in `flow()` and passed by reference throughout. It tracks whether the payment is multi-path, whether an AMM offer was consumed during the current engine iteration, and the count of such iterations. `AMMLiquidity` holds a non-owning reference to it (`ammContext_`) and consults it on every call. After each engine iteration `AMMContext::update()` increments `ammIters_` if `ammUsed_` is set, then clears the flag. `AMMLiquidity` itself does not update `ammContext_`; that responsibility belongs to `BookStep`.

## Non-Copyable Design

`AMMLiquidity` deletes its copy constructor and assignment operator. Because it holds a mutable reference to `AMMContext` (shared state across all strands) and an immutable snapshot of pool balances captured at construction, copying would produce objects with aliased state that do not reflect a coherent point in time. The `std::optional<AMMLiquidity<TIn, TOut>>` storage pattern in `BookStep` (using `emplace()`) avoids copies while still supporting deferred construction.