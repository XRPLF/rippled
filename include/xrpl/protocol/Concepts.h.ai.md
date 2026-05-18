# `include/xrpl/protocol/Concepts.h`

## Role in the System

This header is the compile-time type vocabulary for the XRPL protocol layer. It centralises all C++20 `concept` definitions that constrain the three payment asset families — XRP, IOU (trust-line), and MPT (Multi-Purpose Token) — and provides a small `std::variant` visitor utility that those same types rely on internally. Because it is included by both protocol-level types (`Asset`, `PathAsset`) and the payment path engine (`EitherAmount`, `OfferStream`), keeping it in a single header avoids duplicated constraints and ensures consistency across a large surface area.

## Concepts

### `StepAmount`

```cpp
template <typename A>
concept StepAmount =
    std::is_same_v<A, XRPAmount> || std::is_same_v<A, IOUAmount> || std::is_same_v<A, MPTAmount>;
```

Constrains the three distinct numeric representations used as individual payment-step quantities. `EitherAmount`, the type-erased amount carrier used throughout the path-finding engine, restricts its constructor, `holds<T>()`, and `get<T>()` template parameters with `StepAmount`. This means the compiler rejects, at instantiation time, any attempt to store or query an amount type that doesn't belong to the sanctioned set.

### `ValidIssueType`

```cpp
template <typename TIss>
concept ValidIssueType = std::is_same_v<TIss, Issue> || std::is_same_v<TIss, MPTIssue>;
```

Scopes the `Asset::get<T>()` and `Asset::holds<T>()` template methods to exactly the two issue types that `Asset`'s internal `std::variant<Issue, MPTIssue>` can hold. Without this gate, a caller could instantiate `get<XRPAmount>()`, which would be nonsensical and produce a hard-to-understand compile error deep inside the variant machinery. The concept surfaces the constraint where it is intentional.

`ValidIssueType` is also used for the `is_issue_v` and `is_mptissue_v` boolean constant templates in `Asset.h`, which drive `if constexpr` branches in the comparison operators.

### `AssetType`

```cpp
template <typename A>
concept AssetType = std::is_convertible_v<A, Asset> || std::is_convertible_v<A, Issue> ||
    std::is_convertible_v<A, MPTIssue> || std::is_convertible_v<A, MPTID>;
```

This concept is intentionally broader than `ValidIssueType`. It uses `is_convertible_v` rather than `is_same_v`, which means it accepts any type with an implicit conversion path to one of the four named types. This enables generic code that accepts any "asset-like" value — including `Asset` itself — without forcing callers to normalise to a canonical form first.

### `ValidPathAsset`

```cpp
template <typename T>
concept ValidPathAsset = (std::is_same_v<T, Currency> || std::is_same_v<T, MPTID>);
```

`PathAsset` represents the currency/token specifier inside a payment path element — it explicitly does not carry issuer information, only the token identity. `Currency` covers both XRP (the zero currency) and IOU tokens; `MPTID` covers MPT issuances. `ValidPathAsset` constrains `PathAsset::get<T>()`, `holds<T>()`, and the `is_currency_v`/`is_mptid_v` helper constants in the same way `ValidIssueType` constrains `Asset`.

### `ValidTaker`

```cpp
template <class TTakerPays, class TTakerGets>
concept ValidTaker = (... && !std::is_same_v<TTakerPays, XRPAmount> || !std::is_same_v<TTakerGets, XRPAmount>);
```

This two-parameter concept captures a fundamental DEX invariant: an offer cannot have both the `TakerPays` and `TakerGets` sides denominated in XRP. Both sides must independently be one of the three step-amount types, but the XRP/XRP combination is structurally illegal on the XRPL order book. `OfferStream::shouldRmSmallIncreasedQOffer<TTakerPays, TTakerGets>()` uses this constraint to prevent the template from being instantiated for a nonsensical trading pair, encoding the rule at the type system level rather than as a runtime assertion.

## The `detail::CombineVisitors` Visitor Utility

Both `Asset` and `PathAsset` wrap `std::variant` internally and expose a `.visit(lambdas...)` member that accepts multiple lambdas and dispatches to the correct one based on the active alternative. This is the classical *overloaded pattern* for `std::visit`, and `CombineVisitors` is its implementation:

```cpp
template <typename... Ts>
struct CombineVisitors : Ts...
{
    using Ts::operator()...;
    constexpr CombineVisitors(Ts&&... ts) : Ts(std::forward<Ts>(ts))...{}
};
```

By inheriting from all lambda types simultaneously and pulling every `operator()` into the derived scope with `using Ts::operator()...`, `CombineVisitors` becomes a single callable that overload-resolution can dispatch correctly. `std::visit` then selects among those overloads based on the variant's active type at runtime.

The factory function `make_combine_visitors` is preferred over a CTAD deduction guide. The comment in the file explains the reasoning: function-template argument deduction is more robust than class-template argument deduction (CTAD) when the template parameters involve parameter packs — CTAD for variadic class templates has historically had portability problems. The factory also applies `std::decay_t` to strip reference and cv-qualifiers from lambda types before they become base classes, which ensures the inherited `operator()` calls have the correct value categories.

The entire utility lives in `xrpl::detail`, signalling that it is an implementation detail not intended for direct external use; callers always go through `Asset::visit()` or `PathAsset::visit()`, which delegate to `detail::visit()`.

## Design Rationale

Centralising these constraints in one header rather than repeating `std::is_same_v` chains in every consumer file has two practical benefits. First, adding a new payment asset family (a future fourth type alongside XRP, IOU, and MPT) requires updating `StepAmount`, `ValidIssueType`, and `ValidTaker` in one place; the compiler then propagates errors to every call site that needs to handle the new case. Second, the concepts produce clean diagnostic messages — when a caller passes an unsatisfied type, the error points to the named concept rather than to a deep template instantiation chain inside variant machinery.