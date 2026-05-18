# `include/xrpl/protocol/Asset.h`

## Purpose and Context

`Asset.h` introduces the unified `Asset` abstraction for the XRPL protocol — a type that can represent any of the three kinds of value that can move across the ledger: native XRP, IOU tokens (issued currencies), and Multi-Purpose Tokens (MPT). Historically the codebase used `Issue` for both XRP and IOUs, and all amount-related classes were templated or specialized around `Issue`. When MPT support was added, a wrapper was needed that could represent all three forms without an inheritance hierarchy, without dynamic dispatch, and with minimal disruption to existing `Issue`-based APIs.

The file is deliberately header-only for its inline and constexpr methods. The non-trivial implementations (`getIssuer`, `operator()`, JSON parsing, streaming) live in the corresponding `Asset.cpp`.

---

## The Core Type: `Asset`

`Asset` wraps a `std::variant<Issue, MPTIssue>`. `Issue` already covers both XRP and IOU (its `native()` method distinguishes them), so the variant has exactly two arms but logically represents three asset kinds. This choice of `std::variant` over a polymorphic base class is central to the design: it enables `constexpr` comparisons, value semantics, and type-safe visitation without vtables or heap allocation.

Conversions **to** `Asset` are intentionally implicit — any `Issue`, `MPTIssue`, or raw `MPTID` can silently upgrade to an `Asset`. This design lets callers pass any of those types into functions that accept `Asset` without explicit casts, preserving backward compatibility with legacy `Issue`-taking code. Conversions **out** of an `Asset` to a specific issue type are explicit via the `get<TIss>()` template, which throws `std::logic_error` if the runtime type doesn't match. Callers should always guard with `holds<TIss>()` before calling `get<TIss>()` or use `visit()` instead.

---

## The Visitor Pattern

`Asset::visit()` accepts a variadic pack of lambdas and delegates to `detail::visit` defined in `Concepts.h`. That utility combines the lambdas into a `CombineVisitors` overload set using the `using Ts::operator()...` trick, then forwards to `std::visit`. This lets callers write multi-arm visitors inline without manually defining a visitor struct:

```cpp
asset.visit(
    [](Issue const& issue) { /* XRP or IOU path */ },
    [](MPTIssue const& mpt)  { /* MPT path */ });
```

The `native()` and `integral()` methods are implemented this way. `native()` is true only for XRP. `integral()` is true for both XRP and MPT — MPT amounts are always whole numbers, unlike IOU amounts which use a floating-point mantissa/exponent encoding. This distinction matters for serialization and arithmetic rounding.

---

## `AmountType` and `getAmountType()`

The `AmountType<T>` struct is a pure tag type: it carries no data but encodes a numeric type (`XRPAmount`, `IOUAmount`, or `MPTAmount`) as a template parameter. `getAmountType()` returns a `std::variant<AmountType<XRPAmount>, AmountType<IOUAmount>, AmountType<MPTAmount>>` that reflects the runtime asset kind.

This pattern exists to bridge runtime type information back into compile-time template dispatch. Code doing amount arithmetic can `std::visit` over the result of `getAmountType()` to select the correct templated path. It is non-obvious but elegant: the variant never holds meaningful data, only type information, and its sole purpose is to enable overload resolution at call sites that need to template on the amount kind.

---

## `BadAsset` Sentinel

`BadAsset` is an empty tag struct, and `badAsset()` returns a static instance of it. The equality operator `operator==(BadAsset const&, Asset const&)` returns true when the `Asset` holds an `Issue` with `badCurrency()`, or holds an `MPTIssue` whose issuer is `xrpAccount()` (the null account used as a sentinel in MPT). This mirrors the pre-existing pattern where `badCurrency()` signals an invalid IOU.

The pattern avoids `std::optional<Asset>` or an extra validity flag — invalid states are represented as well-known sentinel values, and `BadAsset` provides a uniform way to test for any of them without knowing which sub-type the asset holds.

---

## `equalTokens()` vs `operator==`

`operator==` on two `Issue` values considers both currency and issuer: XRP always compares equal (no issuer), but two IOUs with the same currency and different issuers compare unequal. `equalTokens()` relaxes this: it tests only the `Currency` field for `Issue`-vs-`Issue` comparisons, ignoring the issuer. For `MPTIssue`-vs-`MPTIssue` it compares the full `MPTID` (which already encodes issuer and sequence, so there is no issuer-free concept of an MPT currency). Cross-type comparisons always return false.

This distinction matters in path-finding and offer-matching logic where the token type must match but the issuer may differ (e.g., trust lines from different issuers in the same currency).

---

## Ordering and Hashing

`operator<=>` imposes a total order: when both assets hold the same variant arm, ordering is delegated to the arm's natural `<=>`. When arms differ, `Issue` sorts as greater than `MPTIssue`. This arbitrary but stable cross-type ordering allows `Asset` to be used as a key in sorted containers. The `hash_append` template dispatches hashing to whichever arm is active, enabling `Asset` in `unordered_map` and other hash-based structures.

---

## Validation Utilities

`isConsistent()` delegates to `Issue::isConsistent` for IOU/XRP assets (which checks that XRP has no account component) and trivially returns `true` for MPT. `validAsset()` is stricter: it rejects `badCurrency()` for issues and rejects the zero-issuer sentinel for MPT. `validJSONAsset()` enforces protocol rules at the JSON layer: an asset JSON object must contain either `currency` or `mpt_issuance_id`, but not both.

---

## Relationship to `STAmount`

`STAmount` — the ledger's serialized amount type — stores an `Asset` member (`mAsset`) and delegates `native()`, `integral()`, `holds<TIss>()`, and `get<TIss>()` directly to it. The `Asset::operator()(Number const&)` convenience method constructs an `STAmount` from the asset and a raw numeric value, allowing concise amount construction. This makes `Asset` effectively the type-identity half of `STAmount`, and the design allows `STAmount`'s constructors to be templated on `AssetType` (the concept from `Concepts.h`) while still storing a single unified `Asset` member internally.