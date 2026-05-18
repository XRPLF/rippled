# `MPTAmount.h` — Typed Integer Amount for Multi-Purpose Tokens

## Purpose and Context

`MPTAmount` is the canonical numeric type for quantities of Multi-Purpose Tokens (MPTs), a token category introduced to XRPL alongside the existing native XRP and IOU trust-line tokens. It sits in the same tier as `XRPAmount` and `IOUAmount`, and the three together satisfy the `StepAmount` concept defined in `Concepts.h`, which allows generic payment-path and DEX algorithms to operate over all token types via C++20 templates.

The fundamental design decision for MPT amounts is that they are plain signed integers, not floating-point or mantissa/exponent pairs. This matches XRP drops and contrasts sharply with `IOUAmount`, which stores a `(mantissa, exponent)` pair to handle the 10¹⁵ range of IOU values. MPT issuers set a precision at issuance time; at runtime the ledger simply tracks whole units up to the protocol cap of `maxMPTokenAmount = 0x7FFF'FFFF'FFFF'FFFFull` (equal to `INT64_MAX`), validated in `Protocol.h` with a `static_assert` that `Number::maxRep >= maxMPTokenAmount`.

## Class Structure

`MPTAmount` inherits privately from four Boost operator templates, a well-established CRTP pattern for composing arithmetic without writing every combination by hand:

- `boost::totally_ordered<MPTAmount>` — synthesizes `!=`, `>`, `>=`, `<=` from the declared `==` and `<`.
- `boost::additive<MPTAmount>` — synthesizes `operator+` and `operator-` (binary) from `+=` and `-=`.
- `boost::equality_comparable<MPTAmount, std::int64_t>` — heterogeneous `!=` from `operator==(value_type)`.
- `boost::additive<MPTAmount, std::int64_t>` — heterogeneous `+`/`-` with raw integers.

The field `value_` is declared `protected` rather than `private`, which is conspicuous given that `XRPAmount` makes `drops_` private. This opens a subclassing path without exposing the raw integer to unrelated code. No subclasses appear in the current codebase, so this is either forward-looking or a minor inconsistency.

## Key Design Choices

**`value()` is deliberately awkward to call.** The accessor carries a documented comment: "Code SHOULD NOT call this function unless the type has been abstracted away." The intention is to keep arithmetic in the typed domain. Callers who work with generic templates parameterized on amount type may call `value()` without knowing the concrete type; everyone else should use the type directly. This same convention appears word-for-word in `XRPAmount`.

**`beast::Zero` integration** provides a conventional zero sentinel that avoids constructing `MPTAmount(0)` explicitly. Both the constructor `MPTAmount(beast::Zero)` and `operator=(beast::Zero)` set `value_` to 0. This is idiomatic throughout the XRPL codebase: `beast::zero` is a global constant of type `beast::Zero` used for zero-initialization in generic code.

**Implicit conversion to `Number`** via `operator Number() const noexcept` allows `MPTAmount` to be passed anywhere a `Number` is expected — arithmetic operations, rounding, comparisons. The reverse direction (constructing from `Number`) is explicit and rounds to nearest with ties going to even, matching IEEE 754 default rounding. The `XRPAmount` class uses the identical comment and mechanism.

## The `mulRatio` Function

`mulRatio` is a free function (not a method) that computes `amt * num / den` with configurable rounding direction:

```cpp
MPTAmount mulRatio(MPTAmount const& amt, uint32_t num, uint32_t den, bool roundUp);
```

The intermediate product is computed in `boost::multiprecision::int128_t` to avoid overflow — multiplying a 63-bit value by a 32-bit numerator can produce up to 95 bits. After division, if there is a remainder, the function adjusts the result according to the sign of `amt` and the `roundUp` flag: positive amounts round up when `roundUp=true`; negative amounts round away from zero (more negative) when `roundUp=false`. If the final result exceeds `std::numeric_limits<int64_t>::max()`, it throws `std::overflow_error`. This function is used for fee and reserve calculations that apply ratios (e.g., percentage-of-amount fees).

A zero denominator throws `std::runtime_error` immediately, before any arithmetic, via the XRPL `Throw` mechanism which integrates with test-override hooks.

## Integration with the Amount Type System

`AmountConversions.h` shows where `MPTAmount` connects to the on-ledger `STAmount` representation:

- `toSTAmount(MPTAmount const& mpt, Asset const& asset)` wraps the integer in an `STAmount` tagged with the `MPTIssue` asset.
- `toAmount<MPTAmount>(STAmount const& amt)` extracts back, asserting that the `STAmount` holds an `MPTIssue`, that the mantissa does not exceed `maxMPTokenAmount`, and that the exponent is exactly 0. A non-zero exponent would mean the value came from the floating-point IOU encoding path — a protocol invariant violation — so a runtime `Throw` fires in addition to the debug `XRPL_ASSERT`.

This double-layer defense (assert + throw) is unique to MPT among the three amount types, reflecting that the integer-only constraint is a protocol rule that must be enforced even in release builds.

## Relationship to `XRPAmount`

The two classes are structurally near-identical: same `value_type`, same Boost bases, same `mulRatio` implementation, same `to_string` and streaming operator, same `minPositiveAmount()` returning the type holding `1`. The functional differences are that `XRPAmount` exposes the domain-specific name `drops()` alongside the generic `value()`, provides `dropsAs<Dest>()` for safe narrowing conversions, `decimalXRP()` for human-readable display, and `jsonClipped()` for JSON serialization. `MPTAmount` omits these because MPT has no sub-unit naming convention and its JSON encoding is handled at the `STAmount` layer.