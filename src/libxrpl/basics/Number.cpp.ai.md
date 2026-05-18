# `src/libxrpl/basics/Number.cpp`

## Role and Purpose

`Number.cpp` provides the complete arithmetic implementation of `xrpl::Number`, the XRPL codebase's custom fixed-precision decimal floating-point type. `Number` was introduced to replace the ad-hoc arithmetic previously embedded in `STAmount` and related types, giving the ledger a single, auditable, and precisely-rounded numeric type capable of representing all asset classes — IOU amounts, XRP drops, and MPT (Multi-Purpose Token) amounts — with correct, amendment-controlled precision.

The file is dense with deliberate design decisions driven by two overriding constraints: **exact decimal rounding** (to match on-ledger determinism requirements) and **support for two different mantissa precisions** selected at runtime based on active amendments.

---

## Thread-Local State

Two `thread_local` variables own all per-thread numeric behavior:

```cpp
thread_local Number::rounding_mode Number::mode_ = Number::to_nearest;
thread_local std::reference_wrapper<MantissaRange const> Number::range_ = largeRange;
```

`mode_` selects from four IEEE-754-style rounding modes (`to_nearest`, `towards_zero`, `downward`, `upward`). `range_` is a `std::reference_wrapper` pointing to either the `smallRange` or `largeRange` static constants — using a reference wrapper rather than copying avoids accidental mutation of the range values and makes reassignment a pointer swap. Callers change the active range through `setMantissaScale()`, which validates the input and updates the wrapper to point at the appropriate constant.

The thread-local design is critical: XRPL processes transactions in parallel across worker threads. Each thread must have its own rounding mode and range so that amendment-gate changes at transaction start don't race with arithmetic in progress.

---

## Dual Mantissa Ranges

`MantissaRange` encodes two normalization regimes:

- **`small`**: min = 10¹⁵, max = 10¹⁶ − 1 (16 significant decimal digits). This matches the legacy `STAmount` IOU representation and is used when neither `SingleAssetVault` nor `LendingProtocol` amendments are active.
- **`large`**: min = 10¹⁸, max = 10¹⁹ − 1 (19 significant decimal digits). This covers the full `int64_t` positive range (2⁶³ − 1 ≈ 9.2 × 10¹⁸), enabling precise integer representation of XRP drops and MPT amounts. 10¹⁹ − 1 is the largest value of the form 10^k − 1 that fits in an unsigned 64-bit integer, which is why this is the maximum.

The `large` range's max (10¹⁹ − 1) intentionally exceeds `INT64_MAX` (≈ 9.22 × 10¹⁸). This is handled carefully: internal storage uses `uint64_t`, but the external `mantissa()` method divides by 10 before returning if the internal value exceeds `maxRep`, adjusting the exponent accordingly. This preserves the invariant that external callers always see a signed 63-bit mantissa.

---

## The `Guard` Class

`Guard` is an internal, file-local class that implements **guard digits** — extra decimal digits of precision kept during multi-step calculations so the final result can be correctly rounded without accumulating error. It stores 16 guard digits packed into a `uint64_t` as 4-bit BCD nibbles, plus an `xbit_` sticky bit that records whether any non-zero digit was ever shifted off the bottom.

The `push(d)` / `pop()` protocol captures digits that fall out of the main mantissa during normalization:

- `doPush(d)` shifts `digits_` right by 4 bits (losing the least significant nibble into `xbit_` if non-zero) and places the new digit at the top.
- `pop()` shifts `digits_` left, recovering the most significant guard digit.

The `round()` method consults the current thread-local rounding mode to decide whether to round up (+1), round down (−1), or tie-break to even (0). For `to_nearest` it compares the packed guard value against `0x5000'0000'0000'0000` — exactly half, in the 4-bit-per-digit encoding — using `xbit_` to break ties toward "more than half."

`doRoundUp` and `doRoundDown` apply the rounding decision by incrementing or decrementing the mantissa, then renormalizing: if incrementing pushes the mantissa beyond `maxMantissa`, the mantissa is divided by 10 and the exponent is incremented; if decrementing drops it below `minMantissa`, the mantissa is multiplied by 10 and the exponent is decremented. Overflow beyond `maxExponent` throws `std::overflow_error`.

`doRound(rep& drops)` is a separate entry point used when converting a `Number` to integer `rep` — it rounds the already-scaled integer value and applies the sign.

---

## Normalization (`doNormalize`)

The free function `doNormalize` (a friend, called through the templated `Number::normalize<T>` specializations) brings any `(negative, mantissa, exponent)` triple into canonical form for a given range:

1. If the mantissa is zero, the result is canonical zero.
2. While the mantissa is below `minMantissa` and the exponent is above `minExponent`, multiply the mantissa by 10 and decrement the exponent.
3. While the mantissa exceeds `maxMantissa` or exceeds `maxRep`, push the last digit into the `Guard` and divide by 10.
4. A special extra division handles the case where the intermediate value exceeds `INT64_MAX` but the final stored value must not (relevant for the large range when the mantissa is, say, 9.9 × 10¹⁸).
5. `doRoundUp` is then called with the guard digits to produce the correctly-rounded result.

The function is templated over the mantissa type (`uint128_t`, `unsigned long long`, `unsigned long`) to handle multiplication products in `operator*=` that temporarily require 128 bits.

---

## Arithmetic Operators

**Addition (`operator+=`)**: Aligns the two operands to the same exponent by repeatedly dividing the operand with the smaller exponent by 10, pushing guard digits. If both have the same sign, their mantissas are added (using `uint128_t` to avoid overflow) and the result is rounded up. If they have opposite signs, the smaller is subtracted from the larger, and digits recovered from the guard via `pop()` refine the result, followed by `doRoundDown`.

**Multiplication (`operator*=`)**: Computes the product of the two mantissas as `uint128_t`, sums the exponents, and trims the product into range using `divu10` — a Hacker's Delight-derived routine that avoids the expensive native 128-bit modulo operation by approximating division by 10 with bit shifts and a correction step.

**Division (`operator/=`)**: Pre-scales the numerator by a power of 10 (10¹⁷ for the small range, 10¹⁹ for the large range) to preserve precision through integer division by the denominator. For the large range, a further 1000× correction term using the division remainder is computed separately (because the combined scale factor would overflow `uint128_t`) and added before the final normalization. Division by zero throws `std::overflow_error`.

---

## Integer Conversion and `truncate`

`operator rep()` converts a `Number` to `int64_t` by scaling the mantissa to an integer via repeated division (pushing guard digits) or multiplication (checking for overflow), then calling `doRound` with the accumulated guard. This is the primary way XRP drop values are materialized from `Number` arithmetic.

`truncate()` removes the fractional part without rounding — it divides the mantissa and increments the exponent until the exponent is non-negative, then renormalizes.

---

## `to_string` Formatting

`to_string(Number)` produces human-readable decimal output. For exponents far from zero it falls back to scientific notation (`Me+E`). For normal ranges it constructs a padded string representation of the integer mantissa, then uses pointer arithmetic to locate the decimal point position and strip leading and trailing zeros. The approach avoids repeated character-by-character operations by exploiting the fixed-length padding.

---

## Power and Root Functions

`power(f, n)` uses binary exponentiation (log₂(n) multiplications) to compute f^n.

`root(f, d)` computes f^(1/d) via Newton–Raphson iteration. To ensure rapid convergence regardless of scale, it first brings `f` into the range (0, 1) by subtracting a multiple of d from the exponent (Euclidean remainder ensures the exponent shift is a multiple of d, so the root's exponent is exact). A quadratic least-squares curve fit provides the initial guess `r` for the iteration `r ← ((d−1)r + f/r^(d−1)) / d`, which continues until the result stabilizes or oscillates between two values (a cycle-of-2 detector prevents infinite loops near the rounding boundary). The result's exponent is then shifted back by `e/d`.

`root2(f)` is a specialized, faster square root using the same pattern with hardcoded coefficients for d=2.

`power(f, n, d)` composes the above: it reduces n/d by GCD, applies `power(f, n)` then `root(result, d)`, with appropriate handling for zero, one, and infinity corner cases consistent with IEEE 754 Annex F.

---

## Key Design Tradeoffs

**Separate `Guard` digits vs. extended mantissa**: Rather than storing a wider mantissa, the `Guard` is a thin wrapper around a single `uint64_t` BCD integer. This keeps `Number` objects small (one `bool`, one `uint64_t`, one `int`) while providing sufficient precision for correct rounding in all operations.

**`externalToInternal` UB avoidance**: Converting `INT64_MIN` to its absolute value as a `uint64_t` is undefined behavior in C++ when done via negation of an `int64_t`. `externalToInternal` routes through `int128_t` for the one edge case where the value does not fit in the positive range of `int64_t`.

**Amendment-gated precision via thread-local `range_`**: Rather than making `Number` a compile-time template over its precision, the range is a runtime, per-thread switch. This allows the same `Number` code to participate in both amendment-gated (small range) and post-amendment (large range) transaction contexts without code duplication or binary explosion. The cost is a thread-local dereference on every normalization, which is negligible compared to the arithmetic itself.