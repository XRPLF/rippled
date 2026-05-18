# `src/libxrpl/protocol/Quality.cpp`

## Role in the System

`Quality.cpp` implements the core arithmetic of XRPL's offer-matching engine. A `Quality` represents the exchange ratio between two currencies — specifically `out / in`, the amount of output currency a taker receives per unit of input. This ratio drives how the order book is sorted and how path payments are evaluated: better offers (higher output per input) rank first. The file is tightly coupled to `STAmount` and the path-finding subsystem, and its calculations directly affect whether a trade is filled and at what price.

## Internal Encoding

`Quality` stores its value in a single `uint64_t` `m_value` that mirrors the wire encoding of `STAmount`. The top 8 bits hold a biased exponent (actual exponent + 100, so valid stored exponents are 1–255), and the bottom 56 bits hold the mantissa. This packing is the same layout used by `amountFromQuality()` and `getRate()` — making conversion to and from `STAmount` a cheap bit operation.

The most non-obvious aspect is **inverted ordering**: a *higher* quality (better deal for the taker) corresponds to a *lower* integer in `m_value`. This is explicit in the comparison operators defined in the header, where `operator<` compares `lhs.m_value > rhs.m_value`. The consequence flows through the arithmetic operators:

```cpp
Quality& Quality::operator++()  { --m_value; return *this; }  // advances to higher quality
Quality& Quality::operator--()  { ++m_value; return *this; }  // retreats to lower quality
```

Incrementing `m_value` would make quality worse; decrementing it makes quality better. `XRPL_ASSERT` guards protect both directions against underflow (`m_value > 0`) and overflow (`m_value < UINT64_MAX`), since `m_value == 0` or full saturation would be invalid encoded values.

## Clamping: `ceil_in` and `ceil_out`

These four methods (plus their `_strict` variants) are the engine of proportional scaling during offer execution. When path payment fills an offer partially, the engine needs to shrink either the input or output to a limit while keeping the ratio consistent.

Both directions share a private template:

```cpp
template <STAmount (*DivRoundFunc)(...)>
static Amounts ceil_in_impl(Amounts const& amount, STAmount const& limit, bool roundUp, Quality const& quality)
```

**`ceil_in`** caps the input side: if `amount.in > limit`, it sets `in = limit` and computes the proportional output via division by the rate (`DivRoundFunc(limit, quality.rate(), ...)`). A secondary clamp `if (result.out > amount.out) result.out = amount.out` then prevents rounding from producing more output than the original offer promised — this is the "no money creation" invariant. An `XRPL_ASSERT` confirms `result.in == limit` after clamping.

**`ceil_out`** caps the output side symmetrically: it computes the required input via multiplication (`MulRoundFunc(limit, quality.rate(), ...)`), then clamps `result.in` to not exceed the original `amount.in`.

The template parameter approach (taking a function pointer as a template argument) lets `ceil_in` and `ceil_out` share logic with their `_strict` siblings. The difference between `divRound`/`mulRound` and `divRoundStrict`/`mulRoundStrict` is precision: the non-strict variants ignore low-order bits that could influence rounding decisions, while the strict variants consider all bits. The `_strict` variants were introduced to fix subtle rounding bugs without changing the default behavior for existing callers.

## Composing Qualities Across Hops

`composed_quality(lhs, rhs)` computes the effective exchange rate for a two-hop path: if one leg converts A→B at quality `lhs` and the second converts B→C at quality `rhs`, the end-to-end rate is their product. The implementation multiplies the two `STAmount` rates with `mulRound`, then re-encodes the result into the 64-bit packed format:

```cpp
std::uint64_t stored_exponent(rate.exponent() + 100);
std::uint64_t stored_mantissa(rate.mantissa());
return Quality((stored_exponent << (64 - 8)) | stored_mantissa);
```

An assertion verifies the exponent fits in the 8-bit field (1–255); this would fire if the composed rate is astronomically large or small, indicating a broken path. Both input rates are asserted non-zero to prevent division-by-zero in `mulRound`.

## Tick-Size Rounding with `round()`

`round(digits)` truncates quality precision for tick-size enforcement. It extracts the exponent and mantissa from `m_value`, then does a ceiling round of the mantissa to `digits` significant decimal digits using a precomputed power-of-ten table:

```cpp
mantissa += mod[digits] - 1;
mantissa -= (mantissa % mod[digits]);
```

This is a classic ceiling-via-bias technique: adding `mod - 1` before masking ensures any non-zero remainder rounds up rather than down. Rounding up the mantissa means the encoded rate is slightly higher (worse for the taker), which prevents the rounded quality from being mistakenly treated as better than the original. Valid `digits` range from `minTickSize = 3` to `maxTickSize = 16` (enforced by callers).

## Design Tradeoffs

The decision to store quality as an inverted integer is a deliberate space and performance optimization: it lets the XRPL order book sort offer IDs directly using unsigned integer comparison without decoding the floating-point representation. The tradeoff is programmer confusion — every place that manipulates `m_value` directly must remember that "bigger integer = worse quality."

The `ceil_*_strict` variants represent a common XRPL pattern for backward-compatible precision fixes: the old rounding path (`divRound`/`mulRound`) is preserved for code already deployed, while new code can opt into the more precise path without risking consensus-breaking changes on existing transactions.