# `src/libxrpl/protocol/TER.cpp` — Transaction Engine Result Registry

This file is the single authoritative registry that maps every Transaction Engine Result (TER) code to its string token and human-readable description. It is a purely runtime-lookup companion to the compile-time enum definitions in `TER.h`, providing the string data that logging, RPC responses, and reverse-parsing tools need.

## The TER Code System

TER codes encode the outcome of every transaction attempted on the XRP Ledger. Rather than a flat enumeration, the codebase uses six distinct C-style enums, each occupying a non-overlapping numeric range. This range structure is not cosmetic — it carries semantic meaning:

| Prefix | Range | Meaning |
|--------|-------|---------|
| `tel`  | −399 .. −300 | Local node error; not forwarded, no fee check |
| `tem`  | −299 .. −200 | Malformed transaction; can never succeed |
| `tef`  | −199 .. −100 | Failure due to ledger state or internal error |
| `ter`  | −99 .. −1 | Retry; may succeed after other transactions |
| `tes`  | 0 | Success |
| `tec`  | 100+ | Applied with fee claimed; stored in ledger metadata |

The range-based predicates `isTelLocal()`, `isTemMalformed()`, `isTefFailure()`, `isTerRetry()`, `isTesSuccess()`, and `isTecClaim()` (all defined in `TER.h`) exploit this layout directly by comparing numeric ranges rather than matching against a list of known codes — enabling new codes within a category to be classified automatically without changing the predicates.

The `tec` codes and `tesSUCCESS` have explicitly pinned integer values because they are serialized into ledger metadata for historical transactions. The `tel`, `tem`, `tef`, and `ter` codes rely on sequential enum assignment from anchor values (e.g., `telLOCAL_ERROR = -399`) and are stable in the sense that binary codec definitions in `ripple-binary-codec` also reference them by integer — the header comments warn about this explicitly.

## Type-Safe Wrappers: `TER` and `NotTEC`

`TER.h` wraps the raw integer in a template class `TERSubset<Trait>`, where a trait controls which `TE*codes` enum types are accepted by the constructor and assignment operator. Two instantiations are exported:

- `TER` — accepts all six code families including `TECcodes`. Used as the general return type from transaction application.
- `NotTEC` — excludes `TECcodes`. Used for preflight return values. This matters because preflight runs before signature verification; if a preflight function could return a `tec` code, a malicious submitter could trigger a fee charge without a valid signature.

All comparison operators (`==`, `!=`, `<`, etc.) are templated with `enable_if` guards that restrict them to pairs where both sides support `TERtoInt()`. This prevents accidental comparisons between unrelated integer types while still allowing cross-family comparisons like `ter == tec` (which are meaningful when checking whether two `TER` values are equal).

## `transResults()` — The Primary Registry

The implementation's central feature is `transResults()`, which returns a reference to a function-local static `unordered_map<TERUnderlyingType, pair<char const*, char const*>>`. Every TER code maps to a pair: the symbolic token string and the English description.

The `MAKE_ERROR` macro is the key insight:

```cpp
#define MAKE_ERROR(code, desc) { code, { #code, desc } }
```

The preprocessor stringification operator `#code` captures the C++ identifier (e.g., `tecNO_DST`) as a string literal at compile time. This eliminates any possibility of the token string diverging from the actual enum name — they are literally the same identifier, one as a value and one as a string. The macro is `#undef`'d after the map initialization to prevent leakage.

The static local initialization follows the Meyers singleton pattern: it is initialized on first call, is guaranteed thread-safe by the C++11 standard, and the `const` qualifier on both the map and its value strings ensures no mutation after construction.

## Lookup Functions

`transResultInfo()` is the base lookup: it calls `TERtoInt(code)` to extract the underlying integer, then does a hash-map lookup. On success it populates two out-parameters (the token and the text) and returns `true`; on failure it returns `false`. Both `transToken()` and `transHuman()` are thin wrappers that delegate to `transResultInfo()` and return `"-"` for unknown codes rather than throwing.

`transCode()` provides the reverse direction: given a string like `"tecNO_DST"`, it returns the corresponding `TER` wrapped in `std::optional`. The reverse map is also built as a function-local static, but it is constructed lazily via a lambda that runs once:

```cpp
static auto const results = [] {
    auto& byTer = transResults();
    auto range = boost::make_iterator_range(byTer.begin(), byTer.end());
    auto tRange = boost::adaptors::transform(
        range, [](auto const& r) { return std::make_pair(r.second.first, r.first); });
    std::unordered_map<std::string, TERUnderlyingType> const byToken(
        tRange.begin(), tRange.end());
    return byToken;
}();
```

The Boost range adaptor `transformed` flips each `{integer → (token, text)}` entry into a `{token → integer}` entry, which is then used to construct a new `unordered_map`. This approach avoids manually maintaining a second map and guarantees the two maps stay in sync — the reverse map is derived entirely from the primary one. The final reconstruction calls `TER::fromInt()` to wrap the raw integer back into a `TER` value, bypassing the type-checking constructors since the integer comes from a previously validated source.

## Relationship to the Rest of the Protocol

`transToken()` and `transHuman()` are called throughout the codebase wherever transaction results need to appear in logs, JSON-RPC responses, or error messages. `transCode()` is used in test harnesses and RPC parsing to convert string tokens back to `TER` values. The registry in `transResults()` is also accessible directly for use cases that need to iterate all known codes, such as documentation generators or conformance checkers.

Two codes in the registry are worth noting as sentinels: `temUNCERTAIN` and `temUNKNOWN` are described as internal intermediate results that "should never be returned" to callers — they exist to represent the state of a result before determination is complete, and their presence in the registry ensures they produce readable output if they leak into logs during debugging.