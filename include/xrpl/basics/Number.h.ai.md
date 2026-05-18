# `include/xrpl/basics/Number.h` — Decimal Floating-Point Arithmetic for XRPL

`Number` is the XRPL ledger's custom decimal floating-point type. It exists because IEEE 754 `double` is unsuitable for consensus-critical financial arithmetic: binary floating point introduces rounding artifacts that can cause two nodes to compute slightly different results for the same transaction, breaking the ledger's agreement requirement. All arithmetic needed during transaction processing — AMM liquidity calculations, lending protocol interest, IOU amounts, XRP drops, MPT balances — routes through `Number`.

## Internal Representation

A `Number` stores three fields: a `bool negative_` sign flag, a `uint64_t mantissa_` (called `internalrep` in the code), and an `int exponent_`. The mantissa is kept *normalized*, meaning it sits in a range `[min, max]` where `min` is a power of ten and `max = min * 10 - 1`. The exponent is bounded to `[minExponent, maxExponent]` = `[-32768, 32768]`. Zero is the special case where mantissa equals zero and `exponent_` is `std::numeric_limits<int>::lowest()`.

This representation was inherited from `STAmount`, which encodes IOU values the same way. The key insight is that financial values in the ledger are already expressed in decimal — storing them in a decimal floating-point type avoids any binary-to-decimal conversion loss.

## The Two Mantissa Scales — and Why They Both Exist

The file introduces `MantissaRange`, a small value-type holding the `min`, `max`, and `log` for one of two permitted scales:

- **Small scale** (`10^15` to `10^16 - 1`): the original `STAmount` normalization range. It gives 15–16 significant decimal digits, which is sufficient for IOU values but cannot exactly represent large integers like XRP drops or MPT balances up to `2^63 - 1 ≈ 9.2 × 10^18`.
- **Large scale** (`10^18` to `10^19 - 1`): introduced for `SingleAssetVault` and `LendingProtocol`. It gives 18–19 significant decimal digits, covering the full positive `int64_t` range and the integer values needed for XRP and MPT precisely.

The active scale is controlled by the thread-local `range_` member, a `std::reference_wrapper<MantissaRange const>` that points to one of two `constexpr static` instances (`smallRange` or `largeRange`). Using a reference wrapper rather than copying the range is deliberate: it prevents accidentally mutating the authoritative constants and makes the scale switch cheap — just a pointer swap.

Scale switching is amendment-gated. In `applySteps.cpp`, the `with_txn_type` function checks `featureSingleAssetVault` and `featureLendingProtocol` and, if either is enabled, installs a `NumberMantissaScaleGuard` that sets the scale to `large` for the duration of the transaction and restores the previous value on exit. This means pre-amendment transactions continue to use the small scale, preserving backward-compatible arithmetic results even in mixed-amendment ledgers.

## External vs. Internal Interface

The internal mantissa is an unsigned `uint64_t` and can hold the large-scale maximum of `9,999,999,999,999,999,999` — which exceeds `INT64_MAX = 9,223,372,036,854,775,807`. However, the external interface (`mantissa()` and `exponent()`) must return a signed `int64_t`, so `mantissa()` silently divides by 10 and `exponent()` increments by 1 whenever the internal mantissa is in this "overflow zone." The pair is guaranteed consistent. This is the mechanism that allows the internal representation to precisely track the full large-scale range while still presenting a canonical 63-bit external view.

`Number` cannot represent `-2^63` exactly, but that value is not a valid ledger amount: XRP drops are non-negative, and MPT maximum is `2^63 - 1`.

The conversion from signed external `rep` to internal unsigned `internalrep` is handled by `externalToInternal()`. It cannot simply negate `INT64_MIN` because that is undefined behavior in C++; the method falls through to a 128-bit intermediate cast for that edge case.

## Guard Digits and Rounding

The `Guard` inner class is the arithmetic workhorse. It maintains a 64-bit BCD-like register of up to 16 guard decimal digits (each stored in 4 bits using a shift-and-push register) plus a sticky `xbit_` that records whether any non-zero digit was ever pushed beyond the 16-digit window. This is classic guard-digit arithmetic, ensuring that intermediate precision lost during alignment or multiplication is available for correct final rounding.

Rounding mode is also thread-local (`mode_`). The four modes — `to_nearest` (banker's rounding / round-half-to-even), `towards_zero`, `downward`, `upward` — map directly to IEEE 754 semantics. The default is `to_nearest`. `NumberRoundModeGuard` sets a new mode on construction and restores the old one on destruction, enabling scoped rounding control without global mutation.

## Arithmetic Implementation

Addition aligns the two operands to the same exponent by dividing the smaller one by successive powers of 10, pushing each lost digit into a `Guard`. Subtraction is implemented as `*this += -y`. Multiplication and division use `uint128_t` intermediates (GCC/Clang `__uint128_t`; Boost multiprecision on MSVC) to avoid 64-bit overflow before normalization. The `divu10()` function is a bespoke 128-bit divide-by-10 using the Hacker's Delight bit-trick, avoiding the cost of a full 128/64 hardware division.

`power(f, n)` uses repeated squaring (O(log n) multiplications). `root(f, d)` applies Newton–Raphson iteration until convergence, and `root2` is a specialized square-root shortcut. `power(f, n, d)` combines both for rational exponents.

## Constructor Design

The `unchecked{}` tag constructor bypasses normalization entirely. It exists for two legitimate purposes: constructing the compile-time constants `numZero`, `oneSml`, `oneLrg`, and the range sentinels; and at type-conversion boundaries where the caller guarantees the values are already normalized. The `normalized{}` tag constructor forces normalization from an unsigned internal representation and is reserved for unit tests. Regular constructors (taking a signed `int64_t` mantissa) always normalize via `externalToInternal()` + `normalize()`.

The `implicit` conversion direction is intentional: any integral type or `STAmount` converts *to* `Number` implicitly, while extraction back to `int64_t` is `explicit`. This makes `Number` the natural accumulator type for mixed-mode expressions like `MPTAmount + Number` without risk of silent truncation on the way out.

## Utility Helpers

`squelch(x, limit)` returns zero when `abs(x) < limit` and `x` otherwise. This handles the common financial pattern of zeroing out sub-precision residuals that arise from rounding chains.

`normalizeToRange<T>(minMantissa, maxMantissa)` is a public template that reprojects the `Number` into a caller-supplied integer range, returning a `(mantissa, exponent)` pair. This is used by `STAmount` conversion code to map back from `Number` to specific asset representations without needing `Number` to know about each asset type.

The `logTen()` and `isPowerOfTen()` constexpr templates at namespace scope verify the `MantissaRange` boundary invariants at compile time via `static_assert`, ensuring that the chosen scale boundaries are always exact powers of ten — a precondition for correct normalization arithmetic.