# `XRPAmount.h` — Type-Safe Representation of XRP in Drops

## Role in the System

`XRPAmount` is the canonical type for representing XRP quantities throughout the XRPL C++ codebase. All XRP values are stored and computed in *drops*, where one XRP equals exactly 1,000,000 drops — a fixed-point decimal avoiding floating-point imprecision for ledger-critical arithmetic. This header defines the class itself along with `DROPS_PER_XRP`, the `mulRatio()` helper, and stream I/O utilities.

The class sits at the center of a family of amount types — `IOUAmount`, `MPTAmount`, and the polymorphic `STAmount` — that together represent every asset class the XRPL supports. Where `IOUAmount` models issued-currency amounts with mantissa/exponent encoding for wide dynamic range, `XRPAmount` models XRP's native integer arithmetic directly. It integrates with `AmountConversions.h`'s `toSTAmount()` and `toAmount<T>()` families, which serve as bridges among all three representations.

## Design: Minimal Primitives, Derived Operators

The class inherits privately from four `boost::operators` mixins:

```cpp
class XRPAmount : private boost::totally_ordered<XRPAmount>,
                  private boost::additive<XRPAmount>,
                  private boost::equality_comparable<XRPAmount, std::int64_t>,
                  private boost::additive<XRPAmount, std::int64_t>
```

This is a classic CRTP operator-generation pattern. By implementing only `operator==`, `operator<`, `operator+=`, and `operator-=`, the class gets `!=`, `<=`, `>`, `>=`, `+`, and `-` for free. The two mixed `std::int64_t` variants allow direct comparison and arithmetic with raw integer literals — useful for fee and reserve computations in calling code. Private inheritance prevents these mixin types from appearing in the public interface.

## Unit Type Contract

`XRPAmount` declares:
```cpp
using unit_type = unit::dropTag;
using value_type = std::int64_t;
```

This exposes the same two-member interface expected by the `unit::Valid` and `unit::Usable` concepts defined in `Units.h`. Those concepts gate access to generic functions like `mulDiv()` and `jsonClipped()`. Despite sharing the concept interface, `XRPAmount` is *not* a specialization of the `ValueUnit<UnitTag, T>` template — it predates or intentionally diverges from that template for reasons of API clarity (it names its accessor `drops()` rather than `value()`) and for the richer XRP-specific functionality (`mulRatio()`, `decimalXRP()`, `dropsAs()`).

## API Walkthrough

**Construction** is deliberately explicit from raw integers (`explicit XRPAmount(value_type drops)`) to prevent accidental silent coercions. Two special-purpose constructors exist:

- `XRPAmount(beast::Zero)` — allows `XRPAmount x = beast::zero` in generic contexts where a zero sentinel is passed without knowing the concrete amount type.
- `XRPAmount(Number const& x)` — converts from the `Number` type (the codebase's high-precision arithmetic wrapper) using round-to-nearest-even, which is called out explicitly in a comment. This rounding mode matters for computed fees and reserves that may not land exactly on integer drop boundaries.

**Accessing the value** is intentionally split across two names:
- `drops()` — the semantically correct name; callers should use this.
- `value()` — an escape hatch for templated code that treats `XRPAmount` as a generic `unit::Valid` type, with a comment warning against its use elsewhere.

**`dropsAs<Dest>()`** is a safe narrowing conversion that returns `std::optional<Dest>`. It guards against both overflow (value exceeds `Dest::max`) and sign violations (negative drops into an unsigned type). This matters because JSON serialization uses 32-bit integers, and some internal fee fields use smaller types. The overload `dropsAs<Dest>(defaultValue)` provides a fallback for contexts that can tolerate a clipped value.

**`jsonClipped()`** takes a more direct approach: it saturates to `Json::Int` bounds rather than returning an optional. The comment is candid — this is only valid in contexts where the value is never expected to overflow 32 bits, specifically fees and reserves. It enforces the compile-time invariant that `value_type` is a signed integral type via `static_assert`.

**`signum()`** returns -1, 0, or +1, following the mathematical signum convention. `AmountConversions.h` uses it when converting `XRPAmount` to `STAmount`, checking `signum() < 0` to extract the sign bit before passing the magnitude as an unsigned value.

**`operator Number()`** is an *implicit* conversion to `Number`, deliberately non-explicit. This allows `XRPAmount` values to participate transparently in `Number`-based arithmetic expressions used throughout the payment engine.

## `mulRatio()`: Safe Fixed-Ratio Scaling

The free function `mulRatio()` scales an `XRPAmount` by a rational factor `num/den`:

```cpp
XRPAmount mulRatio(XRPAmount const& amt, std::uint32_t num, std::uint32_t den, bool roundUp)
```

The critical design choice is using `boost::multiprecision::int128_t` for the intermediate product. A 64-bit amount multiplied by a 32-bit numerator can produce up to 96 bits of result before division brings it back into range. Without the wider intermediate type, overflow would silently corrupt fee calculations. The function then applies sign-aware rounding: for positive amounts with a nonzero remainder, `roundUp=true` increments the result; for negative amounts, `roundUp=false` decrements (toward more negative), maintaining symmetric semantics. Overflow after division throws `std::overflow_error`. Division by zero throws `std::runtime_error`.

## `DROPS_PER_XRP` and `decimalXRP()`

The constant `DROPS_PER_XRP{1'000'000}` is declared as `constexpr XRPAmount` rather than a plain integer, which is a deliberate choice: it makes the constant participate in the type system and prevents inadvertently mixing it with unrelated integer values. `decimalXRP()` is defined *after* this constant precisely because it references `DROPS_PER_XRP.drops()` — a subtle declaration ordering dependency in an otherwise header-only file.

## Relationship to `Fees`

The `Fees` struct in `Fees.h` is the primary production consumer of `XRPAmount`. It stores `base`, `reserve`, and `increment` as `XRPAmount` fields and implements `accountReserve(ownerCount)` as:
```cpp
return reserve + ownerCount * increment;
```
This works because `XRPAmount` overloads `operator*(value_type const& rhs)`, allowing a `std::size_t` owner count to scale the increment naturally, and the result adds to the reserve via the Boost-derived `operator+`.

## Invariants and Failure Modes

The class imposes no internal validity constraints — negative drops are representable and used (e.g., for internal calculations). The burden of ensuring valid ledger amounts falls on calling code. `mulRatio()` is the only function that throws, doing so on division by zero or post-division overflow. The `dropsAs<Dest>()` family handles range violations via `std::optional` rather than exceptions, pushing the decision of how to handle out-of-range values to callers.