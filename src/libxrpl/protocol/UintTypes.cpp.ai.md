# `UintTypes.cpp` — Currency Code Serialization

This file implements the string-to-`Currency` and `Currency`-to-string conversion functions for the XRPL protocol. It is the single authoritative place where the on-wire binary representation of a currency code is mapped to and from a human-readable string, and where the protocol's sentinel currency values are defined.

## The `Currency` Type and Its Layout

`Currency` is a `base_uint<160, detail::CurrencyTag>` — a 160-bit (20-byte) opaque value. The XRPL [serialization spec](https://xrpl.org/serialization.html#currency-codes) carves this 20-byte field into regions: bytes 0–11 and 15–19 must be zero for an ISO-style currency, while bytes 12–14 hold the three-character ASCII code. The constants in `detail::` encode this layout:

```cpp
constexpr std::size_t isoCodeOffset = 12;   // byte offset of 3-char code
constexpr std::size_t isoCodeLength = 3;
```

The bitmask `sIsoBits` (`FFFFFFFFFFFFFFFFFFFFFFFF000000FFFFFFFFFF`) has zeros only at those three bytes. Anding it against a currency value and testing for zero confirms that every bit outside the ISO region is unset — the necessary condition before treating the value as an ISO code.

## Sentinel Currency Values

Three singleton `Currency` values represent special protocol states:

- **`xrpCurrency()`** — all zeros (`beast::zero`). This is XRP, the ledger's native asset. The zero value is the protocol's canonical representation; `isXRP()` checks this directly.
- **`noCurrency()`** — value `1`. A placeholder used internally when no currency is specified (e.g., in data structures that must hold *some* value).
- **`badCurrency()`** — value `0x5852500000000000`. A deliberately poisoned sentinel. The header comment explains the motivation: early developers would sometimes encode "XRP" as a three-letter ISO currency rather than using the all-zero canonical form. `badCurrency()` exists to be a distinct, recognizable error value that marks this misuse. Crucially, `to_currency()` may return it (via `parseHex`) and callers are warned by the header comments that this legacy behavior is preserved to avoid breaking existing call sites.

All three are function-local statics, ensuring they are lazily initialized once and never destroyed — a common XRPL idiom for protocol constants that must survive the entire process lifetime.

## `to_string()`: Decoding a 160-bit Value

The conversion from `Currency` to string applies a priority-ordered decision tree:

1. If the value is `beast::zero`, return `"XRP"` (via `systemCurrencyCode()`).
2. If the value is `noCurrency()`, return `"1"`.
3. Apply the `sIsoBits` mask. If all non-ISO bits are zero, extract the three bytes at offset 12 and validate them against `isoCharSet`. If valid *and* not equal to `"XRP"`, return the three-character string.
4. Fall through to `strHex(currency)` — a raw 40-character hex representation.

Step 3's guard against returning `"XRP"` is significant: it prevents any currency value with "XRP" in the ISO position (but non-zero bytes elsewhere, or the zero padding in the right place) from being mistakenly printed as the native currency string. If such a value exists in the ledger, it surfaces as hex, making the anomaly visible.

`isoCharSet` is deliberately broad — it includes uppercase and lowercase letters, digits, and a set of symbols (`<>(){}[]|?!@#$%^&*`). This is wider than strict ISO 4217 (which only allows `[A-Z]`), reflecting XRPL's extended custom-currency ecosystem.

## `to_currency()`: Parsing a String

The overload `to_currency(Currency&, std::string const&)` returns a `bool` and is the validating entry point:

- An empty string or `"XRP"` sets the currency to `beast::zero` and returns `true`.
- A three-character string whose characters are all in `isoCharSet` is accepted as an ISO code: the currency is zeroed, then the three bytes are copied into position at `isoCodeOffset`. Reject otherwise.
- Any other string is forwarded to `currency.parseHex(code)`, accepting 40-character hex strings representing arbitrary 160-bit values. This path can return `badCurrency()` if the hex happens to encode it.

The value-returning overload `to_currency(std::string const&)` wraps this, returning `noCurrency()` on failure rather than propagating a boolean. Callers that need to distinguish parse failure from "no currency" should use the reference-output overload and check the return value.

## Design Observations

The file contains no exceptions. All error conditions are signaled through return values (`bool` or sentinel values), consistent with the XRPL codebase's general preference for value-based error handling in protocol-layer code. The `detail::` namespace isolates the layout constants from the public API, keeping the header clean while making the encoding strategy self-documenting in the implementation file.

The round-trip property (`to_string(to_currency(s)) == s`) holds for well-formed ISO codes and hex strings, but breaks for the edge cases by design: `"XRP"` round-trips to the zero currency which prints as `"XRP"`, while a currency whose ISO region contains `"XRP"` with zero surroundings round-trips to hex — both consistent with the protocol's intent to keep the native currency unambiguous.