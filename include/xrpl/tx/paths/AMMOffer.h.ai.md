# `AMMOffer.h` — Synthetic AMM Offer Adapter for BookStep

`AMMOffer<TIn, TOut>` is a computational stand-in for an Automated Market Maker pool within the XRPL payment engine's offer-crossing logic. It does not correspond to any on-chain ledger entry; instead it presents an interface that mirrors `TOffer<TIn, TOut>` (the Central Limit Order Book wrapper) so that the generic `BookStep` class can treat AMM liquidity and CLOB offers polymorphically, without any runtime dispatch.

## The Design Problem

The XRPL payment engine processes offers through `BookStep`, a generic template that was originally built for CLOB orders. When AMM support (XLS-30) was added, the challenge was to plug AMM pools into this existing machinery without forking or duplicating `BookStep`. The solution is structural duck-typing: `AMMOffer` exposes the exact same named methods (`quality()`, `amount()`, `consume()`, `fully_consumed()`, `limitIn()`, `limitOut()`, `send()`, `isFunded()`, `adjustRates()`, `checkInvariant()`) that `TOffer` provides, making both types usable as template arguments to the same `BookStep` logic.

## Core State

The offer holds four pieces of immutable data set at construction time:

- `amounts_` — the synthetic offer size (TakerPays/TakerGets equivalent). For single-path transactions, this is sized so that if fully consumed, the AMM spot price after the swap equals the quality of the competing CLOB offer, or it is a "max offer" representing 99% of the output side of the pool. For multi-path transactions, it is generated from a Fibonacci-sequence progression so successive payment engine iterations probe progressively larger AMM liquidity slices.
- `balances_` — the current pool reserves at the time the offer was generated. Crucially, these are snapshotted separately from `amounts_` because the spot price quality (used as `quality_`) can diverge from the raw ratio of `amounts_` when the offer is sized relative to a competing CLOB.
- `quality_` — either the actual spot price quality derived from `balances_`, or the `amounts_` ratio when the two coincide.
- `consumed_` — a mutable boolean flag ensuring the offer is crossed at most once per payment engine iteration.

The `ammLiquidity_` reference back to `AMMLiquidity<TIn, TOut>` gives access to the trading fee, asset identifiers, the AMM account ID, and the `AMMContext` that tracks cross-iteration state.

## Single-Path vs. Multi-Path Divergence

The most architecturally significant behavior difference is in `limitOut()` and `limitIn()`. When `ammLiquidity_.multiPath()` is false (a single payment path), limiting is done using the actual AMM conservation function: `limitOut` calls `swapAssetOut(balances_, limit, tradingFee())` and `limitIn` calls `swapAssetIn(balances_, limit, tradingFee())`. This respects the true constant-product curve — as more of the pool is consumed, the price worsens nonlinearly.

When multiple paths are present, AMM offers behave like fixed-quality CLOB offers: `limitOut` calls `quality().ceil_out_strict(offerAmount, limit, roundUp)` and `limitIn` calls `quality().ceil_in_strict`. This proportional scaling is deliberate: changing the AMM offer size according to its initial quality preserves the ordering between strands, ensuring that the taker pays slightly more than necessary (yielding a higher pool product than the original), rather than computing a new price along the AMM curve which would complicate multi-strand optimization.

The same logic applies to `getQualityFunc()`: in single-path mode it constructs a `QualityFunction` with a nonzero slope (`QualityFunction::AMMTag`) derived from the current pool balances and trading fee, encoding the curve `q(out) = -cfee/poolIn * out + poolOut * cfee/poolIn`. In multi-path mode it constructs a constant quality function (`QualityFunction::CLOBLikeTag`) that behaves identically to a CLOB offer.

## Consumption and State Updates

`consume()` is deliberately minimal. It validates that the consumed amounts do not exceed the offer's `amounts_`, sets `consumed_ = true`, and calls `ammLiquidity_.context().setAMMUsed()` to inform `AMMContext` that an AMM offer was crossed during this payment iteration. It does **not** update the ledger pool balances — that update happens in `BookStep::consumeOffer()` when funds are actually transferred. The `key()` method returning `std::nullopt` is the structural signal of this: there is no ledger object to write back.

`AMMContext::MaxIterations` (30) caps how many payment engine iterations may consume AMM offers in a single transaction, preventing an unbounded loop since AMM offers — unlike CLOB offers — are not exhausted from a queue.

## Fee and Funding Semantics

`isFunded()` unconditionally returns `true` because the AMM pool is always the issuer of its own synthetic offer; there is no owner account that could be underfunded. `adjustRates()` returns `{ofrInRate, QUALITY_ONE}`, zeroing out the output-side transfer fee — AMM swaps on Payment transactions are exempt from transfer fees, in contrast to `TOffer::adjustRates()` which passes both rates through unchanged. Similarly, `send()` calls `accountSend()` with `WaiveTransferFee::Yes`, which is the other half of the same invariant.

## Invariant Checking

`checkInvariant()` enforces the AMM constant-product rule after each swap: the new pool product `(balances_.in + consumed.in) × (balances_.out - consumed.out)` must be greater than or equal to the original product `balances_.in × balances_.out`, or the decrease must be within a 1e-7 relative tolerance. This guard exists because rounding in the swap formulas can produce tiny violations of the constant-product invariant; the tolerance absorbs those without masking genuinely broken swaps.

## Instantiations

The `.cpp` file explicitly instantiates eight combinations of `IOUAmount`, `XRPAmount`, and `MPTAmount` pairs, covering all token types that XRPL supports for AMM pools. This keeps template code out of headers and centralizes the compilation cost.