# `Issue.h` — Currency-Issuer Pair in the XRPL Type System

## Role and Purpose

`Issue` is one of the foundational value types in the XRPL protocol layer. It models a **specific currency as issued by a specific account** — the minimal tuple needed to identify any IOU asset on the ledger. A USD amount issued by Bitstamp and an XRP amount are both expressible as `Issue` instances, but they differ in whether the issuer field carries meaning.

This type sits below `Asset` (which generalizes over XRP, IOU, and MPT) and above raw `Currency`/`AccountID` pairs. It is a peer to `MPTIssue` and feeds into `Book` (an ordered pair of issues defining an order book). Understanding `Issue` is prerequisite to understanding how offers, trust lines, and AMM pools are keyed and compared throughout the codebase.

## Class Design

`Issue` is a simple aggregate of two public fields — `currency` and `account` — with no encapsulation beyond accessor convenience. This is an intentional design choice: `Issue` is a **value type** used pervasively for keying and comparison, and heavy encapsulation would add friction without benefit. The fields are public and default-constructible so the type works naturally with structured bindings, aggregate initialization, and serialization routines.

`Currency` is a 160-bit strong typedef (`base_uint<160, CurrencyTag>`) and `AccountID` is likewise a 160-bit strong typedef (`base_uint<160, AccountIDTag>`). The distinct tag types prevent silent cross-assignment between them at compile time, a deliberate defensive pattern common throughout `UintTypes.h`.

The `getIssuer()` accessor exists primarily for generic code that operates across `Issue` and `MPTIssue` (see `Concepts.h`), providing a uniform interface without requiring virtual dispatch or full abstraction.

## The XRP Special Case in Equality and Ordering

The most architecturally significant detail in this file is how XRP is handled in `operator==` and `operator<=>`:

```cpp
[[nodiscard]] inline constexpr bool
operator==(Issue const& lhs, Issue const& rhs)
{
    return (lhs.currency == rhs.currency) &&
           (isXRP(lhs.currency) || lhs.account == rhs.account);
}
```

When both sides are XRP (identified by `xrpCurrency()`, which is the all-zero 160-bit value), the `account` field is **ignored**. This reflects a protocol invariant: XRP has no issuer, so any `Issue` with `xrpCurrency()` is semantically identical regardless of what happens to be stored in its `account` field.

The same logic applies in `operator<=>`: after currencies compare equal, if the currency is XRP, `std::weak_ordering::equivalent` is returned immediately without inspecting `account`. This ensures XRP issues form a single equivalence class in any ordered container, regardless of minor inconsistencies in how legacy code populated the account field.

The `isConsistent()` function (implemented in `Issue.cpp`) enforces the tighter invariant — `isXRP(ac.currency) == isXRP(ac.account)` — meaning a well-formed XRP issue must carry `xrpAccount()` (zero) and a well-formed IOU issue must carry a non-zero account. The comparison operators are deliberately more lenient than `isConsistent()`, providing defensive handling for partially-constructed or legacy data.

## `native()` and `integral()`

`native()` returns `true` when `*this == xrpIssue()`, and `integral()` simply delegates to `native()`. On the `Issue` type, these are always equivalent — the distinction matters more for the `Asset` hierarchy, where `MPTIssue` overrides `integral()` separately. These predicates surface throughout the codebase to branch on XRP-vs-IOU semantics in amount arithmetic and validation.

## Text and JSON Serialization

`getText()` and `to_string()` serve slightly different purposes. `getText()` produces a human-readable representation used for logging and debugging; for IOUs it formats as `"CURRENCY/ISSUER"` with special sentinel strings `"0"` for `xrpAccount()` and `"1"` for `noAccount()`, enabling detection of structurally inconsistent issues. `to_string()` by contrast uses `"ACCOUNT/CURRENCY"` order and simply emits the account's base-58 string, aligning with the external API format.

`to_json()` and `setJson()` exist as two distinct serialization interfaces for different call-site patterns: `to_json` constructs and returns a new `Json::Value` suitable for standalone serialization, while `setJson` mutates an existing object in-place for incremental JSON construction. Both omit the `issuer` field for XRP — the JSON representation of XRP issue is `{"currency": "XRP"}` with no issuer key.

`issueFromJson()` is the defensive parser: it validates that the input is a JSON object, rejects the `mpt_issuance_id` key (preventing accidental parsing of an `MPTIssue` as an `Issue`), validates that the currency string is neither `badCurrency()` nor `noCurrency()`, and enforces that XRP issues have no issuer field while IOU issues have a valid base-58 account.

## Hashing

`hash_append` feeds both `currency` and `account` into the hasher unconditionally, without applying the XRP special case that `operator==` uses. This is safe in practice because `isConsistent()` should ensure XRP issues always carry `xrpAccount()`, so consistent data produces consistent hashes. If an inconsistent XRP issue were hashed and then compared for equality, a hash collision could theoretically fail to resolve correctly — but the protocol ensures consistent data through validation at ingestion points.

## Sentinel Values

Two static singletons serve as well-known markers:

- `xrpIssue()` — the canonical XRP asset, initialized once with `xrpCurrency()` and `xrpAccount()` (both all-zero 160-bit values)
- `noIssue()` — a null/empty marker using `noCurrency()` and `noAccount()`, used in contexts where an absent issue must be represented without `std::optional`

Both are returned by `const&` to function-local statics, which is thread-safe under C++11's guaranteed initialization semantics and avoids repeated heap allocation.

## Relationship to Siblings

`Issue` is consumed directly by `Book` (a bid/ask pair of `Issue` values), generalized by `Asset` (which wraps `Issue`, `MPTIssue`, or XRP natively in a variant), and used extensively in amount arithmetic headers for type coercions. The `Concepts.h` file defines constraints like `IssueType` that `Issue` satisfies alongside `MPTIssue`, enabling generic algorithms to operate uniformly over both token types without virtual dispatch. The `isXRP(Issue const&)` free function defined here is a thin wrapper over `issue.native()`, providing a consistent naming convention used across the entire codebase for XRP detection at all abstraction levels.