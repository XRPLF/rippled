# `MPTAmount.cpp` — Arithmetic and Comparison Operators for Multi-Purpose Token Amounts

## Role in the System

`MPTAmount.cpp` provides the out-of-line operator implementations for `xrpl::MPTAmount`, the ledger's amount type for Multi-Purpose Tokens (MPTs). MPTs are XRPL's integer-valued custom token primitive — a simpler alternative to IOU trust-line tokens that avoids the floating-point mantissa/exponent representation used by `IOUAmount`. Because MPT balances are plain 64-bit signed integers, arithmetic is direct and cheap, and this file reflects that simplicity: it is the entire runtime body of the class.

## Class Design Philosophy

`MPTAmount` is structurally parallel to `XRPAmount`. Both classes wrap a single `std::int64_t` field (`value_` / `drops_`), both declare the same four `boost::operators` mixin bases in their class head, and both expose `operator+=(MPTAmount)`, `operator-=(MPTAmount)`, `operator-()`, `operator==(MPTAmount)`, `operator==(value_type)`, and `operator<(MPTAmount)`.

The Boost.Operators inheritance is deliberate: by privately inheriting from `boost::totally_ordered<MPTAmount>`, `boost::additive<MPTAmount>`, `boost::equality_comparable<MPTAmount, std::int64_t>`, and `boost::additive<MPTAmount, std::int64_t>`, the class gets `operator>`, `operator<=`, `operator>=`, `operator!=`, binary `operator+`, binary `operator-`, and their mixed-type counterparts for free — synthesized from the handful of primitives defined here. No hand-written boilerplate, no risk of inconsistency between `<` and `>`.

The explicit constructor (`constexpr explicit MPTAmount(value_type)`) and the zero-valued helpers (`MPTAmount(beast::Zero)`, `operator=(beast::Zero)`) live in the header as `constexpr` so the compiler can constant-fold them. The comparison and mutation operators do runtime work — small, but not zero-cost for the inliner — so they are out-of-lined here.

## Operator Implementations

`operator+=` and `operator-=` perform direct integer addition and subtraction on `value_` with no overflow guard. This is intentional: MPT amounts flow through the same ledger constraint machinery as XRP drops, and callers are expected to validate bounds before mutating balances. The absence of overflow checks matches `XRPAmount`'s operators and keeps the hot path branch-free.

`operator-()` (unary negation) returns a new `MPTAmount{-value_}`. This is meaningful in transaction arithmetic where a credit to one account and a debit to another are represented as equal-magnitude amounts of opposite sign before being applied, but callers must ensure the magnitude stays representable in `int64_t`.

The two `operator==` overloads — one taking `MPTAmount const&` and one taking `value_type` — serve different call sites. The second overload enables comparisons like `amt == 0` without constructing a temporary, which the `boost::equality_comparable<MPTAmount, std::int64_t>` mixin then uses to synthesize the mixed-type `!=`.

`operator<` is the single total-order primitive from which Boost synthesizes `>`, `<=`, and `>=`. Because `value_type` is `int64_t`, the natural signed-integer ordering gives correct semantics for negative amounts (a negative MPT balance is less than zero).

## `minPositiveAmount()`

The static factory `minPositiveAmount()` returns `MPTAmount{1}`. In the XRPL protocol, this represents the smallest transferable unit of any MPT — analogous to one drop of XRP. The method exists as a named factory rather than a constant so code that works generically across amount types (`XRPAmount`, `IOUAmount`, `MPTAmount`) can call a uniform interface. For `IOUAmount` the equivalent is more involved (the smallest representable positive normalized value), so `minPositiveAmount()` abstracts that variation behind a common name.

## Overflow Safety in Context

The `.cpp` file itself performs no overflow detection. The safe multiplication path for MPT is `mulRatio`, defined inline in `MPTAmount.h`. It uses `boost::multiprecision::int128_t` to widen the intermediate product before dividing, then explicitly checks whether the result fits in `int64_t` before converting — throwing `std::overflow_error` if not. This split (unsafe primitives in `.cpp`, guarded ratio math in the header) mirrors the approach taken by `XRPAmount` and keeps the common-case operators branch-free while still providing a safe path for fee and proportion calculations.

## Relationship to `IOUAmount`

`MPTAmount` deliberately does not inherit from or compose with `IOUAmount`. IOU amounts carry a mantissa and a decimal exponent, support values across a wide dynamic range, and require normalization after every arithmetic operation. MPT amounts are whole integers bounded by `int64_t`, so that machinery would be dead weight. Keeping the types separate also allows the type system to reject, at compile time, mixing MPT and IOU amounts in expressions where only one is valid.