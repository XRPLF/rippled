# `include/xrpl/protocol/Quality.h`

## Purpose and Context

`Quality.h` defines the core exchange-rate abstraction that powers XRPL's on-ledger decentralized exchange (DEX). Every offer on the order book expresses a willingness to swap one currency for another at some rate; `Quality` is the precise, sortable representation of that rate. The entire offer-crossing engine — deciding which offers are best, scaling partial fills, and composing multi-hop paths — is expressed in terms of `Quality` and its companion type `TAmounts`.

## `TAmounts<In, Out>`: Typed Amount Pairs

`TAmounts` is a simple template pair bundling an input amount (`in`) and an output amount (`out`). In the offer-book domain, `in` is always `TakerPays` and `out` is always `TakerGets`. The template parameters are intentionally generic: the codebase instantiates this over `STAmount`, `IOUAmount`, `XRPAmount`, and `MPTAmount` (the latter for multi-purpose token support). `Amounts` is the canonical `TAmounts<STAmount, STAmount>` alias used by the STAmount-based offer-crossing path.

`empty()` returns `true` if either side is non-positive — a guard used by the engine to skip exhausted or invalid offers early without further computation.

## `Quality`: Inverted Floating-Point Rate

`Quality` wraps a single `uint64_t` (`m_value`) using the same bit layout as `STAmount`: the top 8 bits hold a biased exponent (actual exponent + 100, so valid range is stored as 1–255), and the lower 56 bits hold an unsigned mantissa. The canonical value represents the rate `out/in` (TakerGets / TakerPays), i.e., how much output the taker receives per unit of input — a higher number is better for the taker.

The critical non-obvious design choice is that the **integer value is stored inverted** relative to the economic concept: a *higher* quality (more favorable rate for the taker) corresponds to a *lower* `uint64_t`. This is why the comparison operators are counterintuitive on the surface:

```cpp
friend bool operator<(Quality const& lhs, Quality const& rhs) noexcept {
    return lhs.m_value > rhs.m_value;  // larger integer = worse quality
}
```

This inversion is intentional and useful: when offers are stored in a sorted order book keyed on `m_value` as a raw integer (as they are in the ledger's offer directories), ascending integer order corresponds to descending quality, matching the convention that the best offers are processed first from the front of each directory.

Construction from an `Amounts` pair calls `getRate(out, in)` from `STAmount.h`, which encodes the ratio into the compact floating-point format. The `composed_quality()` free function extends this to two-hop paths by multiplying the two rates via `mulRound` and re-encoding the result.

## Increment/Decrement: Stepping the Rate Grid

`operator++` on a `Quality` moves to the *next higher* quality level, which means it **decrements** `m_value` by one integer step. Similarly, `operator--` increments `m_value` to move to the next lower quality. The operators thus navigate the discrete floating-point grid of representable rates one ULP at a time. This is used in offer-book traversal to find the closest acceptable crossing price. The implementation correctly asserts against underflow and overflow with `XRPL_ASSERT`.

## Scaled Amount Methods: `ceil_in` and `ceil_out`

These four methods (two base, two strict variants) are the computational heart of offer crossing. Given an `Amounts` pair that describes a full offer, and a limit on one side, they scale the pair down proportionally to respect the limit while preserving the implied quality.

`ceil_in(amount, limit)` caps the input at `limit`. If `amount.in > limit`, the new output is computed as `limit / rate` (the quality's rate is `out/in`, so dividing limit by the rate gives the proportional output). To prevent floating-point multiplication from synthesizing value out of rounding, the computed output is clamped to the original `amount.out` if the arithmetic produces a larger number. A symmetric clamping rule applies in `ceil_out`.

The "strict" variants (`ceil_in_strict`, `ceil_out_strict`) differ only in which underlying rounding function they delegate to: the non-strict path uses `divRound`/`mulRound` which historically ignored low-order bits of `STAmount`, while the strict path uses `divRoundStrict`/`mulRoundStrict` which respect all bits and accept an explicit `roundUp` flag. This distinction matters for correctness in scenarios where borderline rounding could influence whether an offer crosses or not.

## Template Delegation via `ceil_TAmounts_helper`

All four methods have overloads templated on `<In, Out>` for working with typed amounts rather than raw `STAmount`. Rather than duplicating the logic four times, a single private helper `ceil_TAmounts_helper` performs the type conversion pattern: convert both sides of the `TAmounts` to `STAmount`, call the appropriate `Amounts`-based overload via a function pointer argument, then convert the result back using `toAmount<In>` and `toAmount<Out>`. The function pointer type is passed as a template parameter; the optional `bool roundUp` is forwarded via a variadic `std::same_as<bool>...` pack, allowing both the strict and non-strict variants to share the same helper without separate instantiations.

## `round()` and Tick Sizes

`round(digits)` rounds the quality's mantissa upward to the specified number of significant decimal digits. XRPL's protocol supports a `TickSize` field (valid range `minTickSize=3` to `maxTickSize=16`) on currency issuers and gateways, coarsening the price grid to improve offer-book depth. The implementation uses a precomputed modulus table indexed by digit count and rounds up by adding the modulus minus one before truncating — a standard ceiling-division idiom applied to the 56-bit mantissa.

## `relativeDistance` and the Encoding

`relativeDistance` (used only in tests) is a window into the internal representation: it manually extracts the exponent and mantissa from two `Quality` values, scales them to a common exponent, and returns `|a - b| / min(a, b)`. The lambda `exponent = (rate >> 56) - 100` and `mantissa = rate & ~(255ull << 56)` confirm the 8-bit/56-bit split and the +100 exponent bias, matching the `STAmount` encoding documented elsewhere.

## `QUALITY_ONE`

The macro `QUALITY_ONE 1'000'000'000` represents the unity rate (one unit of input gives one unit of output, scaled to the 9-decimal-place precision used in XRPL's fixed-point arithmetic). It appears throughout offer parsing and fee calculations wherever a 1:1 exchange rate must be expressed as a raw integer.

## Relationship to Other Files

`Quality.h` depends on `STAmount.h` for `amountFromQuality()` and `getRate()`, and on `AmountConversions.h` for the `toSTAmount()` / `toAmount<T>()` adapters that allow typed-amount templates to bridge into the `STAmount`-based computation core. `QualityFunction.h` builds on `Quality` to model the continuous price function of AMM pools, where quality varies with the pool's reserve ratio rather than being a fixed constant.