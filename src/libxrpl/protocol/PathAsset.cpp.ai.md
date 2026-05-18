# `src/libxrpl/protocol/PathAsset.cpp`

This file provides the non-inline serialization interface for `PathAsset` — specifically the `to_string()` free function and the `operator<<` stream overload. At nineteen lines, it is entirely a dispatch layer; all substantive logic lives in the header.

## What `PathAsset` Represents

`PathAsset` is purpose-built for `STPathElement`, the atom of a payment path in the XRPL protocol. A path element specifies a currency or token to route through, but it does not yet resolve an issuer — that happens during path-finding traversal. Accordingly, `PathAsset` holds a `std::variant<Currency, MPTID>` rather than the fuller `Asset` (`std::variant<Issue, MPTIssue>`). `Currency` covers both XRP (the zero currency sentinel) and classic IOU tokens; `MPTID` covers Multi-Purpose Token issuances introduced in later ledger versions.

This narrower type is enforced at compile time via the `ValidPathAsset` concept in `Concepts.h`:

```cpp
concept ValidPathAsset = (std::is_same_v<T, Currency> || std::is_same_v<T, MPTID>);
```

Template members like `holds<T>()` and `get<T>()` are constrained by this concept, so any attempt to query for an unsupported type fails to compile.

## Serialization via `std::visit`

`to_string(PathAsset const&)` uses `std::visit` to dispatch to whichever `to_string` overload matches the active alternative:

```cpp
return std::visit([&](auto const& issue) { return to_string(issue); }, asset.value());
```

`asset.value()` returns the raw `std::variant<Currency, MPTID>` reference, and the generic lambda relies on overload resolution to call either `to_string(Currency const&)` (declared in `UintTypes.h`) or `to_string(MPTID const&)`. This is the same pattern used throughout the XRPL codebase for `Asset` and `Issue`, keeping variant serialization uniform without requiring a hand-rolled `if`/`else` type check.

`operator<<` is a trivial forwarder to `to_string`, consistent with the rest of the protocol types (`Asset`, `Issue`, `MPTIssue` all follow the same pattern).

## Why This Exists as a `.cpp` File

Most `PathAsset` functionality — `holds<T>()`, `get<T>()`, `isXRP()`, `operator==`, `hash_append`, and the constructors — is implemented inline in the header. The `to_string` free function and `operator<<` are deliberately separated into a `.cpp` because they pull in `Indexes.h` (included here though not directly used by these two functions, likely for transitional or include-ordering reasons) and because non-template, non-`constexpr` functions benefit from a single translation-unit definition to avoid ODR concerns and reduce header-inclusion cost.

The file performs no validation, error handling, or resource management. Correctness is fully delegated to the active variant's own `to_string` implementation and to wherever `PathAsset` was constructed — typically deserialization of an `STPathElement` field.