# `include/xrpl/protocol/AmountConversions.h`

## Role in the System

The XRPL protocol defines four distinct amount representations, each optimized for a different concern. `XRPAmount` and `MPTAmount` are simple integer wrappers — strongly-typed drop and multi-purpose token counts respectively. `IOUAmount` is a normalized floating-point type (signed mantissa + exponent) optimized for IOU arithmetic. `STAmount` is the wire-level union type that unifies all three under a single `Asset` tag and can be serialized to/from the ledger binary format.

Generic algorithmic code — AMM pricing, path finding, offer crossing — needs to work with any amount type without duplicating logic. This header provides the glue: a focused set of inline conversion functions that let callers move freely between the four representations. It is intentionally a conversion-only header; all logic lives in the amount types themselves.

## Direction One: Lean Types → `STAmount`

The six `toSTAmount()` overloads wrap a lean value in a serializable `STAmount`. The IOU variant is worth examining closely:

```cpp
inline STAmount
toSTAmount(IOUAmount const& iou, Asset const& asset)
{
    bool const isNeg = iou.signum() < 0;
    std::uint64_t const umant = isNeg ? -iou.mantissa() : iou.mantissa();
    return STAmount(asset, umant, iou.exponent(), isNeg, STAmount::unchecked());
}
```

`STAmount` stores an *unsigned* mantissa with a separate sign bit, while `IOUAmount` stores a *signed* mantissa. The conversion manually splits the sign, then uses the `STAmount::unchecked` sentinel constructor to bypass re-canonicalization. This is intentional: `IOUAmount` is already normalized (mantissa range `[10^15, 10^16 - 1]`), so running canonicalize again would be wasted work and could introduce subtle drift. The `XRPL_ASSERT` guarding `asset.holds<Issue>()` confirms the caller hasn't accidentally passed an XRP or MPT asset — a mismatch that would silently produce wrong data if uncaught.

The XRP overload with an `Asset` argument delegates immediately to the asset-less version after asserting `isXRP(asset)`. This exists solely to give generic code a uniform call signature — callers that hold an `Asset` and an amount type can always call `toSTAmount(amount, asset)` without branching on type.

## Direction Two: `STAmount` → Lean Types

The `toAmount<T>(STAmount const&)` family uses explicit template specializations. The base template is deliberately `= delete`:

```cpp
template <class T>
T
toAmount(STAmount const& amt) = delete;
```

This means calling `toAmount<SomeUnsupportedType>(stamt)` is a hard compile error rather than a linker error or silent instantiation of something wrong. Similarly, identity-conversion overloads (`toAmount<IOUAmount>(IOUAmount const&)` etc.) are provided so generic code calling `toAmount<T>` doesn't need to branch on whether the source is already the target type.

The MPT specialization is the strictest:

```cpp
template <>
inline MPTAmount
toAmount<MPTAmount>(STAmount const& amt)
{
    XRPL_ASSERT(
        amt.holds<MPTIssue>() && amt.mantissa() <= maxMPTokenAmount && amt.exponent() == 0, ...);
    if (amt.mantissa() > maxMPTokenAmount || amt.exponent() != 0)
        Throw<std::runtime_error>("toAmount<MPTAmount>: invalid mantissa or exponent");
    ...
}
```

The assert fires in debug builds; the explicit throw fires in release builds. This double-check is unique to MPT because MPT amounts are integers (exponent must be 0) bounded by `maxMPTokenAmount = 0x7FFF'FFFF'FFFF'FFFFull`. A non-zero exponent or out-of-range mantissa would indicate data corruption or a ledger encoding bug, both of which should surface loudly rather than silently truncate.

## Generic `Number`-Based Construction and Rounding

The two-phase template `toAmount<T>(Asset, Number, mode)` is the most nuanced function in the file. It converts an intermediate `Number` result — the output of AMM pricing arithmetic — into a typed amount, applying a caller-specified rounding mode:

```cpp
saveNumberRoundMode const rm(Number::getround());
if (isXRP(asset))
    Number::setround(mode);
```

The rounding mode override is applied **only for XRP**. IOU amounts use arbitrary-precision internal representation that doesn't require rounding during construction; the `IOUAmount(n)` constructor handles normalization cleanly. But XRP is an integer count of drops, and converting a rational intermediate result (e.g., from AMM math) to an integer requires a deterministic rounding decision. By letting the caller specify `Number::downward` or `Number::upward`, the ledger can implement "give the taker less, charge the taker more" semantics without hardcoding a rounding direction into the amount type itself.

`saveNumberRoundMode` is RAII — it captures the current thread-local rounding mode on construction and restores it on destruction, ensuring that even if the conversion throws, the rounding mode is not left in a mutated state.

`toMaxAmount<T>(Asset)` mirrors this template but returns the maximum representable value for each type. For `STAmount`, it dispatches through `asset.visit(...)`, which accepts typed lambdas for `Issue` and `MPTIssue` — the variant pattern that `Asset` uses internally to avoid dynamic dispatch.

## Helper Utilities

`getAsset<T>(amt)` inverts the typical direction: given an amount, return the associated `Asset`. For `STAmount` this is a simple delegation to `amt.asset()`. For the lean types, which do not carry asset identity, the function returns placeholder sentinels (`noIssue()`, `xrpIssue()`, `noMPT()`). This is used in `AMMHelpers.h` where templated pool arithmetic needs to call `toAmount<T>(getAsset(pool.out), result, rounding)` — the sentinel return from `getAsset` is immediately consumed by another call that knows the true asset from context.

`get<T>(STAmount)` extracts a typed value from an `STAmount` by delegating to the appropriate observer method (`a.iou()`, `a.xrp()`, `a.mpt()`, or identity for `STAmount`). The `static_assert` in the else branch — `constexpr bool alwaysFalse = !std::is_same_v<T, T>` — is the canonical C++ pattern for a template-dependent `static_assert(false)` that triggers only when the unsupported branch is actually instantiated, not on every parse of the template body.

## Summary

`AmountConversions.h` is compact but load-bearing. It is the type-system bridge that lets the rest of the codebase treat XRP, IOU, and MPT amounts uniformly in generic algorithms while retaining strong type separation at the representation layer. The design favors compile-time errors over runtime surprises, uses `unchecked` construction where the source invariants are already established, and encapsulates all rounding-mode side-effect management in a single well-guarded location.