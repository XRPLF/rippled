# `include/xrpl/protocol/Units.h` — Type-Safe Unit Arithmetic for the XRP Ledger

## Purpose

`Units.h` solves a pervasive correctness problem in financial protocol code: raw integer types carry no information about what they measure. A `uint64_t` could be drops of XRP, a fee level, a basis-point rate, or a loop counter — and a misuse silently compiles. This file introduces a compile-time unit system that makes such mismatches impossible to write without an explicit cast.

The design uses the *phantom-type* or *tagged-value* idiom: a single generic template `ValueUnit<UnitTag, T>` wraps any arithmetic `T` and attaches an empty `UnitTag` type. The tag has no runtime cost, but the compiler uses it to reject operations between incompatible units.

## Unit Tags

Four tag types live in the nested `unit` namespace:

- `dropTag` — The smallest divisible unit of XRP. Most balance and fee calculations work in drops. The concrete alias `XRPAmount` (defined elsewhere as `ValueUnit<dropTag, std::int64_t>`) is the canonical drop-valued type.
- `feelevelTag` — A dimensionless ratio used by the transaction queue (`TxQ`) to compare relative fee cost across transactions that differ in processing effort. `FeeLevel64` (`ValueUnit<feelevelTag, std::uint64_t>`) and `FeeLevelDouble` are the primary aliases.
- `unitlessTag` — A plain scalar wrapped in `ValueUnit`. Used internally by `mulDiv` so that raw integers can participate in the unit-checked arithmetic without introducing new named units.
- `BipsTag` / `TenthBipsTag` — Basis points and tenth-of-a-basis-point values. `Bips16`, `Bips32`, `TenthBips16`, and `TenthBips32` are provided as aliases.

## The `ValueUnit<UnitTag, T>` Template

`ValueUnit` privately inherits from several `boost::operators` mixins (`totally_ordered`, `additive`, `equality_comparable`, `dividable`, `modable`, `unit_steppable`). This generates the full set of relational and arithmetic operators from a small number of explicitly defined primitives, avoiding repetition while keeping the operators consistent.

Arithmetic is carefully unit-typed:

- **Addition and subtraction** between two `ValueUnit`s of the *same* unit type return a `ValueUnit` of the same unit. Adding a raw scalar to a `ValueUnit` is also supported (it shifts the value by the scalar amount).
- **Multiplication** by a raw scalar preserves the unit (scaling a quantity). Multiplication is made commutative via a `friend operator*`.
- **Division** of two same-unit `ValueUnit`s returns the raw `value_type` (a dimensionless ratio, since drops/drops = 1). Division by a scalar preserves the unit.

The negation operator contains a `static_assert` that fires at compile time for unsigned `value_type`s — preventing silent integer wrapping when negating something like a `uint64_t`.

### Implicit Widening Conversion

A `ValueUnit<Unit, Wide>` can be implicitly constructed from `ValueUnit<Unit, Narrow>` if and only if the `SafeToCast<Narrow, Wide>` concept is satisfied (same-sign and destination is at least as wide, or destination is wider when signs differ). This mirrors the safe widening rules of `safe_cast.h`: a `FeeLevel<uint32_t>` can quietly become a `FeeLevel64`, but the reverse requires an explicit cast.

### Zero Comparisons via `beast::Zero`

`ValueUnit` integrates with `beast::Zero` by providing an explicit constructor from `beast::Zero` (sets value to 0) and a `signum()` method. The `beast` zero-comparison machinery then generates all six relational operators against `zero` for free, without constructing a temporary `ValueUnit`. The `explicit operator bool()` also follows — `if (amount)` is false exactly when `value_ == 0`.

### JSON Serialisation Gate

`jsonClipped()` converts a `ValueUnit` to a `Json::Value`, clamping to the JSON integer range (`Json::Int` or `Json::UInt`). It is only reachable when the `Usable` concept is satisfied. `Usable` is an explicit whitelist of the known unit tags; new tags do not automatically gain JSON serializability. This prevents accidentally exposing an internal intermediate type through the RPC layer.

## Concepts Hierarchy

The file defines a layered set of C++20 concepts:

- `Valid<T>` — requires `unit_type` and `value_type` nested types (i.e., it is a `ValueUnit`).
- `Usable<T>` — `Valid` plus must be one of the four named unit tags. Used as a guard on `jsonClipped()` and implicit conversions.
- `Compatible<Other, VU>` — `Other` is arithmetic and convertible to `VU::value_type`. Enables cross-scalar arithmetic.
- `IntegralValue<VU>` / `Integral<T>` — value type is integral. Required for `%=` and for cast operations.
- `CastableValue<VU1, VU2>` — both integral, same unit tag. Required for `safe_cast`/`unsafe_cast` between `ValueUnit`s.

The `muldiv*` family of concepts (`muldivSource`, `muldivDest`, `muldivSources`, `muldivable`, `muldivCommutable`) specifically constrain which type combinations can appear in `mulDiv` arguments, enforcing that unit tags are consistent.

## `mulDiv` — Overflow-Safe Scaled Arithmetic

The free function `mulDiv(value, mul, div)` computes `(value * mul) / div` with no intermediate overflow using `boost::multiprecision::uint128_t` as the intermediate type. It returns `std::optional<Dest>`, returning `std::nullopt` on overflow or on negative inputs (the latter protected by `XRPL_ASSERT` in addition to the nullopt path).

Two fast-path shortcuts avoid the 128-bit multiply when `value == div` (result is just `mul`) or when `mul == div` (result is just `value`, after a range check).

The public overloads handle multiple calling patterns — `ValueUnit`-to-`ValueUnit`, `ValueUnit`-to-`uint64_t`, and `uint64_t`-to-`ValueUnit` — with commutativity handled by reordering arguments rather than by duplicate implementations. Raw `uint64_t` arguments are wrapped in `unit::scalar()` (a `ValueUnit<unitlessTag, uint64_t>`) so the inner `mulDivU` function only needs to handle the typed case.

The `muldivCommutable` concept restricts the commuted overload to cases where the `Dest` unit differs from the `Source` unit tags — this is the cross-unit case, e.g., converting a `FeeLevel64` and a base fee in drops to an output in drops. If `Dest` and sources share the same tag, the commuted overload would be ambiguous with the non-commuted one.

## Cast Extension

`safe_cast` and `unsafe_cast` (from `safe_cast.h`) are extended in the `xrpl` namespace with overloads for `ValueUnit` types. `safe_cast<Dest>(src)` where both are `ValueUnit` with the same unit unwraps the source value, applies the scalar `safe_cast` to it, and rewraps it in `Dest`. This allows `safe_cast<FeeLevel64>(someFeeLevel32)` to work seamlessly. The `unsafe_cast` variant asserts at compile time that the cast is *not* trivially safe (preventing misuse as a drop-in replacement for `safe_cast`).