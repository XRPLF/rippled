# `PathAsset.h` — Token Identifier for Payment Path Steps

`PathAsset` is a lightweight discriminated union representing the asset side of a single hop within an XRPL payment path. It exists as a distinct type from the richer `Asset` class because path elements carry the token identifier separately from the issuer: `STPathElement` stores both a `PathAsset mAssetID` and an `AccountID mIssuerID` as independent members. `PathAsset` holds only the *which currency* or *which MPT* component — never the full issue specification.

## The Core Distinction: `PathAsset` vs `Asset`

`Asset` (from `Asset.h`) wraps `std::variant<Issue, MPTIssue>`, where `Issue` bundles a `Currency` together with an `AccountID` issuer, and `MPTIssue` bundles an `MPTID` with its issuing account. This makes `Asset` a complete tradeable instrument description.

`PathAsset` reduces this to `std::variant<Currency, MPTID>` — purely the token-type identifiers defined in `UintTypes.h`: `Currency` is a 160-bit hash, `MPTID` is a 192-bit identifier. The issuer is not part of `PathAsset` because payment path serialization (and `STPathElement`) records the issuer in a separate field; collapsing them would duplicate data and complicate the encoding.

The `PathAsset(Asset const& asset)` constructor performs the projection: it visits the incoming `Asset`, strips the issuer, and retains only the token identifier. XRP/IOU assets contribute their `currency` field; MPT assets contribute their `getMptID()`.

## Type Safety via `ValidPathAsset`

The `ValidPathAsset` concept in `Concepts.h` constrains template parameters to exactly `Currency` or `MPTID`. Both `holds<T>()` and `get<T>()` are gated by this concept, ensuring only those two types can be queried at compile time. The trait templates `is_currency_v<PA>` and `is_mptid_v<PA>` provide compile-time predicates for generic code that branches on asset kind.

`get<T>()` throws `std::runtime_error` if the wrong alternative is requested at runtime, making misuse detectable rather than silently undefined. This is appropriate given that callers are expected to check `holds<T>()` or dispatch through `visit()` before calling `get<T>()`.

## Visitation Pattern

`PathAsset::visit()` delegates to `detail::visit()` from `Concepts.h`, which employs the overloaded-lambda (`CombineVisitors`) pattern to synthesize a single callable from multiple lambdas and forward it to `std::visit`. This infrastructure is shared by `Asset` and `PathAsset`, giving both a uniform, clean visitation API without boilerplate.

`isXRP()` illustrates the pattern: a `Currency` alternative consults `xrpl::isXRP(currency)`, while an `MPTID` alternative unconditionally returns `false`. An MPT can never be the native asset; encoding this directly as a `constexpr` lambda branch rather than a runtime dispatch makes the intent explicit and allows the compiler to eliminate the dead branch.

## Equality and Hashing

`operator==` uses a two-variant `std::visit` with a `if constexpr` guard: two `PathAsset` values holding different alternative types are *never* equal, even if their byte representations coincidentally match. This prevents cross-type false positives. The same logic appears in `Asset::operator==`.

`hash_append` dispatches to the appropriate `hash_append` overload for the held type, enabling `PathAsset` to be used as keys in hash-based containers that rely on the `beast::uhash` infrastructure.

## Role in Payment Path Processing

In `PaySteps.cpp`, `PathAsset` is retrieved from path elements via `getPathAsset()` and compared directly with `curAsset` to track what currency/MPT flows through each step. When a path element specifies no explicit asset (`!(nodeType & typeAsset)`), the current asset propagates unchanged. When a step does specify an asset, the `PathAsset` extracted from it becomes the new current asset, enabling path-building logic to detect type changes and validate consistency across hops. The `STPathElement::typeAsset` bitmask is `typeCurrency | typeMPT`, reflecting exactly the two alternatives that `PathAsset` can hold.