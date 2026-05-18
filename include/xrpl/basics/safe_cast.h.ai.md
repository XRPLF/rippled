# `include/xrpl/basics/safe_cast.h`

This header provides a small family of casting utilities that replace raw `static_cast` at the call site with one that encodes intent and enforces correctness at compile time. Its motivating use case is enum-to-integer and integer-to-enum conversions, where `static_cast` is technically required but silently accepts lossy or sign-mismatching casts.

## `SafeToCast` Concept

The `SafeToCast<Src, Dest>` concept is the foundational correctness predicate. It evaluates to true only when both types are integral, neither cast direction loses sign range (a signed source cannot be cast to unsigned of equal size), and the destination is at least as wide as the source — with an extra byte of headroom required when signedness differs, since a signed destination must accommodate values that were representable in the unsigned source beyond the signed destination's positive range. This encoded arithmetic captures exactly what an implicit promotion would guarantee, made explicit and inspectable by the compiler.

## `safe_cast`: Verified Lossless Conversion

`safe_cast<Dest>(src)` comes in three overloads covering integral→integral, integral→enum, and enum→integral conversions.

The integral→integral overload enforces the `SafeToCast` rules via two `static_assert` statements and then delegates to `static_cast`. Because both assertions fire at compile time, zero-overhead is guaranteed — the optimizer sees a plain `static_cast` after template instantiation. The function is `constexpr noexcept`, so it composes freely in constant expressions and `noexcept` propagation chains.

The enum overloads decompose the cast into a two-step path through the enum's underlying integer type: `enum→underlying_type→Dest` or `Src→underlying_type→enum`. This pattern forces the sign and width checks to operate on actual integer types rather than the nominal enum type, which has undefined underlying width without an explicit base, and keeps the enum-related casts from bypassing the size checks.

## `unsafe_cast`: Explicit Acknowledgement of Lossy Casts

`unsafe_cast<Dest>(src)` inverts the `SafeToCast` predicate. Its `static_assert(!SafeToCast<Src, Dest>, ...)` will reject any call where the cast has actually become safe — meaning if underlying types are later changed to be compatible, the call site will break at compile time with a message suggesting promotion to `safe_cast`. This turns `unsafe_cast` into self-enforcing documentation: it records both the current necessity and the future obligation to revisit the decision.

A real example from `STAmount.cpp` illustrates the intent:

```cpp
mValue = unsafe_cast<std::uint64_t>(-amount.drops());
```

Here a signed drop count is negated and stored in an unsigned field; the caller knows the invariant holds, but the types make no such promise, so `unsafe_cast` marks the acknowledgement in code rather than a comment that can drift out of sync.

## `safe_downcast`: Polymorphic Hierarchy Navigation

`safe_downcast<Dest>(src)` handles pointer and lvalue-reference downcasts within a polymorphic class hierarchy. Its two-mode design is characteristic of performance-sensitive C++:

- **Release builds** (`NDEBUG` defined): compiles to a `static_cast` with a `// NOLINT` suppressing the Clang-Tidy warning about unchecked pointer downcasts.
- **Debug builds**: uses `dynamic_cast` for the pointer overload and checks the result against `nullptr` via `XRPL_ASSERT`; uses a `dynamic_cast` to a pointer in the reference overload just to validate the downcast, then falls through to `static_cast` for the actual conversion (since `dynamic_cast` to a reference throws on failure, and the assertion messaging is preferred here).

This avoids the runtime overhead of `dynamic_cast` in production while catching incorrect casts during development and CI. The `XRPL_ASSERT` macro (aliased to `ALWAYS_OR_UNREACHABLE` from `instrumentation.h`) marks the check as an invariant that must hold, and during fuzzing the execution continues past a failure rather than aborting.

## Integration with `Units.h`

`Units.h` extends the same `safe_cast`/`unsafe_cast` names into the `xrpl` namespace for strong-typed unit wrappers (`XRPAmount`, `IOUAmount`, etc.). Those overloads accept `IntegralValue` wrapper types and unwrap them to their underlying integer, delegate to the integral overloads here, and rewrap the result. This pattern lets code cast between unit types using the same idiom as raw integer casts, maintaining compile-time correctness guarantees across the abstraction boundary.

## Design Rationale

Raw `static_cast` between integers and enums is legal C++ but invisible to reviewers — a narrowing cast looks identical to a widening one. By separating casts into `safe_cast` (always correct) and `unsafe_cast` (annotated exception), this header makes every numeric conversion a policy decision visible in the source. The compile-time inversion in `unsafe_cast` additionally prevents the codebase from accumulating dead safety exceptions as the code evolves.