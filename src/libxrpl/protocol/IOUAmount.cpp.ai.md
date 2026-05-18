# `IOUAmount.cpp` — IOU Amount Arithmetic and Normalization

`IOUAmount.cpp` is the implementation layer for the XRPL's IOU (non-native token) amount type. `IOUAmount` represents all non-XRP balances in the ledger — trust line amounts, offers, AMM pools — as a **signed floating-point value** with a 64-bit signed mantissa and an integer exponent. This file provides the normalization engine, construction from the higher-precision `Number` type, addition, and the ratio-multiplication primitive `mulRatio`, which is used wherever fee calculations, transfer rates, and AMM math must multiply an IOU by a rational fraction.

## Representation and Invariants

The format encodes a value as `mantissa × 10^exponent`. Non-zero amounts are kept in a canonical form where the absolute value of the mantissa lies in `[10^15, 10^16 − 1]` and the exponent lies in `[-96, 80]`. These constants are imported directly from `STAmount::cMinValue`, `cMaxValue`, `cMinOffset`, and `cMaxOffset`, locking `IOUAmount`'s precision to the on-wire serialization format of the ledger.

Zero is a special case: its canonical representation uses `mantissa_ = 0` and `exponent_ = -100`. The exponent of -100 is deliberately below `minExponent` (-96) so that zero sorts less than any representable positive value — essential for correct ordering when zero and sub-minimum amounts appear together in sorted structures.

## The STNumber Switchover

A central design feature of this file is the `getSTNumberSwitchover()` / `setSTNumberSwitchover()` pair. This acts as a runtime flag that selects between two arithmetic backends:

- **Legacy path** (switchover `false`): Normalization and addition use manual base-10 digit-shifting loops, preserving the original `STAmount` rounding behavior.
- **Number path** (switchover `true`, the default): Operations delegate to the `Number` class, which uses the newer, more precise rounding model required for AMM and multi-asset calculations.

The flag is stored in a `LocalValue<bool>` — the XRPL coroutine-aware thread-local mechanism. Because the rippled server processes multiple transactions concurrently across coroutines, a global `bool` would cause race conditions. `LocalValue` isolates the value per-coroutine, so each transaction can apply its own amendment-governed arithmetic mode without interfering with others. The `NumberSO` RAII guard (defined in the header) sets the flag and restores the previous value on scope exit — the idiomatic way to activate and deactivate the feature for a single transaction.

The static accessor `getStaticSTNumberSwitchover()` uses the function-local static idiom specifically to avoid C++ static initialization order problems that could corrupt the flag before first use.

## `normalize()` — Canonical Form Enforcement

`normalize()` is the enforcement point for the invariants above. On every construction from a raw `(mantissa, exponent)` pair (see the inline constructor in the header), `normalize()` is called.

Under the legacy path it works as a digit-shifting loop: while the mantissa is below `minMantissa`, multiply by 10 and decrement the exponent; while it is above `maxMantissa`, divide by 10 and increment the exponent. If the exponent would exceed `maxExponent` during scale-down, the function throws `std::overflow_error`. If the mantissa cannot be scaled up to `minMantissa` without pushing the exponent below `minExponent`, the amount silently becomes zero (underflow is not an error in IOU arithmetic — a sub-minimum payment simply rounds to nothing).

Under the switchover path, the round-trip through `Number::normalizeToRange` replaces those loops with a single well-tested precision-preserving step. The same overflow/underflow policy applies: the code re-checks the resulting exponent after `fromNumber()` and either throws or zeroes accordingly.

## `fromNumber()` — Construction Without Circular Recursion

The static `fromNumber()` factory exists because the `(mantissa, exponent)` constructor calls `normalize()`, and the switchover path inside `normalize()` calls `fromNumber()`. If `fromNumber()` used the public constructor, the system would recurse infinitely. Instead, `fromNumber()` constructs a default-initialized (zeroed) `IOUAmount` by direct field assignment, bypassing `normalize()` entirely, then lets `Number::normalizeToRange()` write the final mantissa and exponent into those fields. This is the only legal way to break the construction/normalization cycle.

## `operator+=` — Addition with Exponent Alignment

The legacy addition path implements standard floating-point addition on a decimal representation. It aligns the two operands to the same exponent by truncating digits from the smaller-magnitude value (dividing its mantissa by 10 and incrementing its exponent until the exponents match), then adds mantissas. After addition, the near-cancellation guard checks whether the result is in `[-10, 10]`; values this small cannot be normalized to the required mantissa range and are zeroed. Otherwise, `normalize()` canonicalizes the sum.

The switchover path avoids this alignment logic entirely by round-tripping through `Number` arithmetic, which handles alignment and rounding internally with higher precision.

## `mulRatio()` — Ratio Multiplication with 128-bit Intermediate Arithmetic

`mulRatio(amt, num, den, roundUp)` computes `amt × num / den` while preserving as much precision as possible and applying controlled rounding. It is the building block for transfer-rate application, fee calculation, and AMM invariant adjustment.

The challenge is intermediate overflow: a 64-bit mantissa multiplied by a 32-bit numerator produces a 96-bit product that overflows `int64_t`. The function uses `boost::multiprecision::uint128_t` throughout, accumulating the product and remainder in 128 bits before scaling back down.

A precomputed static table `powerTable` holds powers of ten from `10^0` to `10^29`, initialized exactly once via a lambda on first call. `log10Floor` and `log10Ceil` are binary-search lambdas over this table. Using a lookup table rather than `std::log10` is deliberate: floating-point log functions can produce rounding errors that make an integer boundary ambiguous; a table lookup is exact.

The precision-maximization step is the non-obvious core of `mulRatio`. After dividing the 128-bit product by `den`, there is a quotient `low` and a remainder `rem`. `rem / den` would be zero in integer arithmetic. Instead, the code calculates how much headroom exists before `low` would overflow a 64-bit mantissa (`roomToGrow = fl64 - log10Ceil(low)`), then scales both `low` and `rem` up by that many powers of ten — effectively recovering fractional digits that would otherwise be lost. The remainder term `addRem = rem / den128` is then large enough to contribute meaningful precision before being added back to `low`.

Finally, if `mustShrink > 0` (the result is still too large for 64 bits after all scaling), the code divides `low` down and tracks whether any set bits were lost (setting `hasRem` for rounding purposes).

Rounding is applied to the last representable mantissa digit: when `roundUp` and the amount is positive, incrementing `mantissa() + 1` by one ULP is safe because the mantissa is already normalized and cannot overflow the range. When `roundUp` is true but the result rounded down to zero (meaning the true value is between 0 and `minPositiveAmount()`), `minPositiveAmount()` is returned rather than zero — ensuring that a non-zero fee is never silently dropped.

## Relationships

`IOUAmount.cpp` depends on `Number` for both its conversion path and its to-string output (`to_string(IOUAmount)` simply delegates to `to_string(Number{amount})`). It imports boundary constants from `STAmount` but does not depend on `STAmount` for any logic — only for the shared numeric range specification. The `LocalValue` mechanism it uses for the switchover flag is the same coroutine-aware TLS used throughout the rippled server to avoid global mutable state in concurrent contexts.