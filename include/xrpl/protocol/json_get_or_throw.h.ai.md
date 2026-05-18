# `json_get_or_throw.h` — Typed JSON Extraction with Protocol-Field Keys

This header provides a small but important safety layer over the raw `Json::Value` API: typed extraction functions that throw structured exceptions instead of silently returning default values or crashing on type mismatches. It lives in `include/xrpl/protocol/` because its interface is anchored to `xrpl::SField` — the XRPL protocol's typed field descriptor — rather than arbitrary string keys.

## Why This Exists

`Json::Value` is a dynamically typed container. Accessing a missing key returns a null value; accessing a value as the wrong type silently coerces or silently returns zero. In serialization/deserialization code — particularly for security-sensitive structures like cross-chain attestations — silent failures are dangerous. This header replaces that permissiveness with an explicit contract: either the key is present with the right type, or an exception is thrown with a clear diagnosis.

## Error Types

Two exception structs are defined in the `Json` namespace:

- `JsonMissingKeyError` — carries the field name as `char const*` and is thrown when the key is entirely absent from the JSON object.
- `JsonTypeMismatchError` — carries the field name and an `expectedType` string, thrown when the key is present but the value's runtime type doesn't match the requested `T`.

Both inherit from `std::exception` and implement `what()` with lazy message construction: the `msg` field is `mutable std::string` and only populated on the first call to `what()`. This avoids a string allocation at the throw site — the string is only built if a caller actually reads the exception message, which is the common case only in error handlers.

## `getOrThrow<T>` — Explicitly Specialization-Only Template

The primary template:

```cpp
template <class T>
T getOrThrow(Json::Value const& v, xrpl::SField const& field);
```

contains `static_assert(sizeof(T) == -1, ...)` in its body. Since `sizeof(T)` is never `-1`, instantiating any non-specialized `T` is a compile-time error, not a runtime surprise. This pattern is intentional: the function is only valid for types that have an explicit specialization, and the compiler enforces it.

Specializations are provided for four types:

**`std::string`** — Checks `isMember`, then requires `isString()`. No coercion.

**`bool`** — Accepts a native JSON boolean (`isBool()`) or any integral value, where non-zero maps to `true`. This mirrors a common XRPL convention where boolean-semantic fields like `WasLockingChainSend` are encoded as `0`/`1` integers in JSON, not as JSON `true`/`false`.

**`std::uint64_t`** — The most permissive specialization, because 64-bit integers exceed JavaScript's safe integer range (`2^53 - 1`). It accepts:
  1. Native JSON unsigned integer (`isUInt()`).
  2. Signed JSON integer that is non-negative (guards against negative values with an explicit range check).
  3. A hex-encoded string, parsed via `std::from_chars` in base 16. The parse validates that every character was consumed (`p == s.data() + s.size()`) — a partial match is treated as a type error.

**`xrpl::Buffer`** — Delegates to `getOrThrow<std::string>` to extract the raw hex string, then calls `strUnHex` to decode it to bytes. A decode failure (invalid hex) throws `JsonTypeMismatchError`. A TODO comment notes a conceptual mismatch between `Buffer` and `STBlob`/blob types in the broader XRPL type system.

## `getOptional<T>` — Exception-to-Optional Adapter

```cpp
template <class T>
std::optional<T> getOptional(Json::Value const& v, xrpl::SField const& field);
```

This wraps `getOrThrow<T>` in a blanket `catch(...)` and returns `std::nullopt` on any exception. The function is explicitly documented as usable by external projects such as the witness server — a cross-chain bridge component that runs outside the rippled process and needs to parse XRPL JSON payloads. The catch-all is intentional: the caller's only question is whether the field is present and valid, not which specific error occurred.

## `SField` as the Key Type

Rather than accepting `std::string` or `const char*` keys, both functions require an `xrpl::SField` reference. The actual JSON key is retrieved via `field.getJsonName()`, which returns a `Json::StaticString` — a compile-time string wrapper that avoids dynamic allocation in the JSON library's hash map. This design ensures all key lookups are tied to declared XRPL protocol fields (`sfAttestationSignerAccount`, `sfPublicKey`, `sfAmount`, etc.), eliminating magic string literals and making typos a compile error.

## Primary Consumer: Cross-Chain Attestations

`XChainAttestations.cpp` is the dominant user, calling `getOrThrow` in constructor initializer lists to deserialize attestation objects received from witness servers:

```cpp
attestationSignerAccount{Json::getOrThrow<AccountID>(v, sfAttestationSignerAccount)},
publicKey{Json::getOrThrow<PublicKey>(v, sfPublicKey)},
signature{Json::getOrThrow<Buffer>(v, sfSignature)},
wasLockingChainSend{Json::getOrThrow<bool>(v, sfWasLockingChainSend)},
```

This pattern is clean and exception-safe: if any field is missing or malformed, the constructor throws before the object is constructed, preventing partially-initialized attestation objects from reaching validation logic. Additional specializations for `AccountID`, `PublicKey`, and `STAmount` are defined closer to those types but follow the same pattern established here.