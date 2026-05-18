# `Zero.h` — Type-Safe Zero Comparison for Unit-Bearing Quantities

## Role and Motivation

`Zero.h` defines a sentinel tag type, `beast::Zero`, that enables clean, type-safe zero comparisons for classes where comparing against zero is meaningful but comparing against arbitrary integers is not. The canonical use case in the XRPL codebase is financial amount types — `XRPAmount`, `IOUAmount`, `MPTAmount`, and `base_uint` — which have a natural concept of positive, zero, or negative, but where an expression like `amount > 1` would be semantically wrong (what unit does `1` carry?). The header provides the `zero` constant and a full suite of comparison operators so code can write `amount > zero` or `amount != zero` without constructing a dummy amount object.

## The `Zero` Struct

`Zero` is an empty struct with an `explicit` default constructor. The `explicit` keyword is intentional: it prevents implicit conversions from integer literals or other types to `Zero`, keeping the sentinel precisely typed. A `static constexpr Zero zero{}` is declared in an anonymous namespace, providing a convenient expression-level constant that the compiler can treat as a no-cost token — no object is built, no memory is touched.

## The `signum()` Contract

Participation in the zero-comparison machinery requires a type to expose a `signum()` operation returning a negative, zero, or positive integer. This can be either a member function or a free function found by ADL in the type's own namespace. The default `beast::signum(T const& t)` template simply forwards to `t.signum()`, so member-based types work without any additional boilerplate. Types that cannot or should not expose a public `signum()` member can instead provide a free function in their own namespace.

Across the XRPL protocol layer every core amount type implements this contract the same idiomatic way:

```cpp
// XRPAmount
constexpr int signum() const noexcept {
    return (drops_ < 0) ? -1 : (drops_ ? 1 : 0);
}

// IOUAmount
inline int IOUAmount::signum() const noexcept {
    return (mantissa_ < 0) ? -1 : (mantissa_ ? 1 : 0);
}
```

This three-way sign extraction is a consistent convention across all amount types in the codebase.

## The ADL Indirection in `detail::zero_helper`

The most subtle aspect of the design is `detail::zero_helper::call_signum`. The comment explains it directly: calls to `signum` must originate from a namespace that does not itself declare an overload of `signum`. The reason is classic two-phase lookup and ADL interaction. If the comparison operators called `signum(t)` directly from within `namespace beast`, the compiler would find `beast::signum` via ordinary unqualified name lookup — before ADL even runs — potentially shadowing a more specific free function defined in `T`'s own namespace. By delegating the call through `detail::zero_helper`, which declares no `signum` of its own, ordinary lookup finds nothing, and ADL is free to discover any namespace-level `signum` associated with `T`. This is the standard pattern for building ADL-friendly customization points in C++ prior to the `tag_invoke` era.

## Operator Layout

The twelve operator overloads are split into two symmetric groups. The first six handle `T op Zero` — the type under scrutiny is on the left — by calling `call_signum` and comparing the result against zero with the appropriate relational operator. The second six handle `Zero op T` — the zero constant is on the left — by simply reversing the operand order and delegating to the first group. For example, `zero < t` becomes `t > zero`. This keeps the sign-extraction logic entirely in the first group and avoids duplicating the `call_signum` call.

## Why Not `operator==(T const&, int)`?

The alternative — overloading comparison against `int` and checking for literal `0` — would allow expressions like `amount == 1` or `amount < 5`, which are semantically nonsensical for unit-bearing types. The `Zero` sentinel type closes this loophole at compile time: the only integer-like value the operator set accepts is the `zero` constant, and that constant has a distinct C++ type that no integer literal can implicitly become. This is the same technique used by `std::nullptr_t` to give `nullptr` a type that can never be confused with an integer zero.

## Summary

`Zero.h` is a small but architecturally principled header. Its value lies entirely in what it prevents: spurious numeric comparisons on quantity types that carry units or other semantic constraints. The `signum()` convention reduces all zero comparisons to a single integer sign check, the ADL indirection in `call_signum` ensures extensibility without namespace pollution, and the explicit constructor on `Zero` closes the implicit-conversion loophole. Together these decisions make the pattern robust enough that every financial primitive in the XRPL protocol layer relies on it.