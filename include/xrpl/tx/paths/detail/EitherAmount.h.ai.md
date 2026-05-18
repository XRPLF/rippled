# `EitherAmount.h` — Type-Erased Amount Wrapper for the Payment Path Engine

## Role in the System

`EitherAmount` exists to solve a specific interface problem in XRPL's payment path engine: the engine's `Step` abstraction is polymorphic (virtual `fwd`/`rev` calls), yet each concrete step works with a specific, statically-typed amount — `XRPAmount`, `IOUAmount`, or `MPTAmount`. These three types have incompatible representations and no common base class. `EitherAmount` bridges that gap by wrapping `std::variant<XRPAmount, IOUAmount, MPTAmount>` behind a uniform value type that can flow freely through the virtual step interface.

## The `StepAmount` Concept Constraint

All template methods on `EitherAmount` are constrained by the `StepAmount` concept defined in `Concepts.h`:

```cpp
template <typename A>
concept StepAmount =
    std::is_same_v<A, XRPAmount> || std::is_same_v<A, IOUAmount> || std::is_same_v<A, MPTAmount>;
```

This constraint closes the open variant: you cannot accidentally construct an `EitherAmount` from `STAmount` or any other numeric type, even though `std::variant` itself would accept any compatible type. The concept acts as a compile-time invariant — the set of acceptable amount types is explicitly enumerated and enforced at every call site.

## Structure and Access Model

The struct holds a single `std::variant<XRPAmount, IOUAmount, MPTAmount>` member named `amount`. Access is mediated through two member templates:

- `holds<T>()` delegates to `std::holds_alternative<T>` and lets callers query the active type before extraction.
- `get<T>()` checks `holds<T>()` first and throws `std::logic_error` if the variant doesn't hold the requested type, then returns a `const` reference via `std::get<T>`.

This fail-fast design is deliberate. The alternative — letting `std::get` throw `std::bad_variant_access` directly — would produce the same failure outcome but with less diagnostic context. By throwing `std::logic_error` with an explicit message, the code signals that reaching a mismatched access represents a programming error in the flow engine itself, not a runtime data condition. A free function `get<T>(EitherAmount const&)` at namespace scope provides a convenient alternative calling convention used throughout `FlowDebugInfo.h`.

## Role in `Step` and the Flow Engine

In `Steps.h`, `EitherAmount` appears as the boundary type for every amount exchange:

```cpp
virtual std::pair<EitherAmount, EitherAmount>
rev(PaymentSandbox& sb, ApplyView& afView,
    boost::container::flat_set<uint256>& ofrsToRm,
    EitherAmount const& out) = 0;

virtual std::pair<EitherAmount, EitherAmount>
fwd(PaymentSandbox& sb, ApplyView& afView,
    boost::container::flat_set<uint256>& ofrsToRm,
    EitherAmount const& in) = 0;
```

Concrete step types (`XRPEndpointStep`, `DirectStepI`, `BookStepXI`, etc.) unpack the variant at the top of their implementations using `get<XRPAmount>()` or `get<IOUAmount>()`, perform their typed arithmetic, and then re-wrap the result into a new `EitherAmount` for return. This pattern means the variant is always created and consumed at well-defined boundaries — the type is always known at the concrete step, and the type-erasure serves only the virtual dispatch layer.

`cachedIn()` and `cachedOut()` on `Step` return `std::optional<EitherAmount>`, allowing a step to cache the last computed amount across forward/reverse passes. `XRPEndpointStep` demonstrates this: it stores an `std::optional<XRPAmount>` internally and wraps it in `EitherAmount` only when the `Step` interface demands it.

## Debug Output

The `operator<<` overload is conditionally compiled only in debug builds (`#ifndef NDEBUG`). It uses `std::visit` with a C++20 template lambda to dispatch `to_string` to whichever type the variant currently holds. Excluding this from release builds reflects the performance-conscious design of the path engine, where formatting strings would be dead weight on the hot path.

## Design Choice: `std::variant` over Inheritance

Using `std::variant` rather than a polymorphic base class gives `EitherAmount` value semantics: it can be stored in `std::vector`, returned by value, and passed cheaply without heap allocation or pointer indirection. `FlowDebugInfo.h` exploits this directly by storing `std::vector<EitherAmount>` to record per-pass input/output amounts during path simulation. A pointer-based design would have required careful ownership management and allocation overhead for what is essentially a diagnostic scratchpad.