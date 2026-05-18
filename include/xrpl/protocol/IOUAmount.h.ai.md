# `include/xrpl/protocol/IOUAmount.h`

## Purpose and Role

`IOUAmount` is XRPL's fixed-precision floating-point type for representing IOU (non-native asset) balances and trust line amounts. It models the ledger's custom numeric encoding: a 64-bit signed mantissa paired with an integer exponent, where the value is `mantissa × 10^exponent`. This is distinct from `XRPAmount`, which stores drops as a plain 64-bit integer, because IOU amounts must span an enormous dynamic range — from microscopic fractional token balances to astronomical supply totals — without the rounding artifacts that binary floating-point would introduce.

The file also houses the `NumberSO` RAII guard and the `getSTNumberSwitchover()`/`setSTNumberSwitchover()` pair, which together manage a per-coroutine feature flag controlling which normalization path is active.

## Representation and Normalization

The encoding imposes strict invariants on a canonical form. The absolute value of the mantissa must lie in `[10^15, 10^16−1]` (i.e., `[1000000000000000, 9999999999999999]`), and the exponent must be in `[-96, 80]`. These bounds match the constants `STAmount::cMinValue`, `STAmount::cMaxValue`, `cMinOffset`, and `cMaxOffset`, tying `IOUAmount` directly to the on-wire serialization format used by `STAmount`.

`normalize()` is private and called from the `(mantissa, exponent)` constructor. It enforces this canonical form by scaling the mantissa up (multiply by 10, decrement exponent) or down (divide by 10, increment exponent) until the mantissa is in range. If the value is too large after scaling, it throws `std::overflow_error`. If it is too small, the amount silently rounds to zero — this asymmetry is intentional: overflow is a programming error worth detecting, while sub-minimum amounts arise naturally from interest accrual calculations and must degrade gracefully to zero.

The zero representation stores `mantissa_ = 0, exponent_ = -100`. The comment in `operator=(beast::Zero)` explains the sentinel: zero must sort *below* the smallest representable positive amount, whose exponent is `-96`. Using `-100` as the exponent for zero ensures that any numeric comparison via the exponent field gives the correct ordering.

## Two Normalization Paths and the Switchover Flag

`normalize()` has two code paths, selected by `getSTNumberSwitchover()`:

**Legacy path** (switchover off): A simple loop adjusts the mantissa digit-by-digit in place. This was the original XRPL algorithm.

**Number path** (switchover on, the default): Delegates entirely to `Number::normalizeToRange(minMantissa, maxMantissa)`. The `Number` class is a richer floating-point type that uses 128-bit intermediate arithmetic and supports configurable mantissa ranges (a "small" range matching IOUAmount's 10^15 precision, and a "large" range for 10^18 precision needed by newer features like SingleAssetVault and LendingProtocol). Similarly, `operator+=` under the switched-on path routes through `Number{*this} + Number{other}` rather than performing manual exponent alignment.

This dual-path design is an amendment-gated migration. The switchover is stored in a `LocalValue<bool>` — a coroutine-local (not merely thread-local) key-value store that XRPL uses so that concurrent coroutines processing different transactions each see their own copy of the flag. The `NumberSO` RAII class wraps `getSTNumberSwitchover()`/`setSTNumberSwitchover()`, saving the current value on construction and restoring it on destruction. Test code and ledger replay can use this guard to temporarily force either path without global side effects.

## Operator Design via Boost.Operators

`IOUAmount` privately inherits from `boost::totally_ordered<IOUAmount>` and `boost::additive<IOUAmount>`. These policy mixins generate the full set of comparison operators (`>`, `<=`, `>=`, `!=`) from just `operator==` and `operator<`, and generate binary `operator+` and `operator-` from just `operator+=` and `-=`. This eliminates repetitive boilerplate while keeping the hand-written operators small and auditable.

The equality check (`operator==`) directly compares the raw `(mantissa_, exponent_)` fields. This is valid because after normalization, every non-zero value has a unique canonical representation; zero always has `(0, -100)`. The less-than check (`operator<`) converts both operands to `Number` and defers to `Number`'s own comparison. The `Number` type handles the zero-sentinel case correctly (it tests `mantissa_ == 0` first) and is safer when comparing across the two normalization regimes.

Unary negation (`operator-`) is implemented without calling `normalize()` — it simply flips the sign of the mantissa. This is safe because the negation of a normalized value is also normalized; the only edge case is negating zero, where `mantissa_ = 0` and the exponent sentinel remains unchanged.

## `mulRatio`: Precision-Preserving Scaled Multiplication

```cpp
IOUAmount mulRatio(IOUAmount const& amt, uint32_t num, uint32_t den, bool roundUp);
```

This free function computes `amt × num / den` while retaining more precision than the naïve approach of constructing intermediate `IOUAmount` values. The intermediate products are held in `boost::multiprecision::uint128_t`, which can accommodate the product of a 64-bit mantissa and a 32-bit numerator (up to ~96 bits), far beyond what `int64_t` alone could hold. The algorithm uses precomputed powers of ten to rescale the quotient and remainder so that the final mantissa fits back into the 64-bit range, then applies rounding at the bit that was lost. The `roundUp` flag follows directed rounding semantics: it rounds up for positive results and down (more negative) for negative results.

## Conversion to `Number`

`IOUAmount` provides a non-explicit conversion `operator Number()` that constructs a `Number` with `{mantissa_, exponent_}`. This is the bridge between the legacy type and the modern arithmetic layer. The conversion is implicit to allow `IOUAmount` values to participate naturally in `Number` arithmetic expressions — as noted in `Number.h`, "conversions to Number are implicit and conversions away from Number are explicit." The reverse conversion (from `Number` to `IOUAmount`) goes through the private static `fromNumber()` which calls `Number::normalizeToRange`, fitting the `Number` mantissa back into IOU's 10^15 range before storing it.