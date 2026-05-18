# `include/xrpl/protocol/KeyType.h`

## Role in the System

This header defines the foundational discriminant type for XRPL's dual-algorithm cryptographic infrastructure. The XRPL supports two independent signature schemes — the Bitcoin-lineage `secp256k1` elliptic curve and the modern `ed25519` Edwards curve — and `KeyType` is the enum that flows through every layer of key generation, signing, and verification to select between them. It is a deliberately minimal header: a single enum class and three inline conversion utilities, included by virtually every cryptographic interface in the protocol.

## The Enum Design

```cpp
enum class KeyType {
    secp256k1 = 0,
    ed25519 = 1,
};
```

The explicit integer assignments (`0` and `1`) are not accidental. Although `KeyType` itself is not serialized directly to the wire, the choice of algorithm manifests in serialized key material — secp256k1 public keys begin with a compressed-point prefix byte (`0x02` or `0x03`), while ed25519 public keys carry a sentinel byte `0xED`. The numeric values of the enum provide stable identifiers for any internal storage or configuration that maps an integer to a key type.

Using `enum class` rather than a plain `enum` enforces scope discipline: callers must write `KeyType::secp256k1` rather than a bare `secp256k1`, preventing symbol-namespace pollution across a large codebase where "secp256k1" would otherwise shadow or collide with library-level identifiers.

## Utility Functions

`keyTypeFromString()` returns `std::optional<KeyType>` rather than throwing on an unrecognised string. This is deliberate: it is called at configuration-parse time and at RPC-request boundaries where user-supplied strings are inherently untrusted. Returning an empty optional instead of raising an exception allows callers to compose the parse result with their own error-reporting logic without forcing exception handling into tight validation paths.

`to_string()` converts a `KeyType` back to a canonical C-string literal. It includes an explicit `"INVALID"` return path for values that match neither known enumerator — a defensive measure relevant because C++ allows any integer to be cast to an `enum class`, meaning a corrupt or maliciously crafted value can produce a `KeyType` that doesn't correspond to either variant. Returning `"INVALID"` instead of `nullptr` or undefined behaviour makes logging and diagnostics safe in those edge cases.

The stream insertion operator is templated on `Stream` rather than fixed to `std::ostream`:

```cpp
template <class Stream>
inline Stream& operator<<(Stream& s, KeyType type) {
    return s << to_string(type);
}
```

This generality allows the operator to work with Beast's logging streams, test harness formatters, and any other stream-like type the codebase employs, without coupling this header to a concrete stream hierarchy.

## Relationships to Sibling Headers

`KeyType` is a first-class parameter across the entire key-management surface. In `SecretKey.h`, functions such as `generateSecretKey(KeyType, Seed const&)`, `derivePublicKey(KeyType, SecretKey const&)`, `generateKeyPair(KeyType, Seed const&)`, and `randomKeyPair(KeyType)` accept it to select the appropriate key-derivation algorithm. The XRPL's secp256k1 derivation path is custom (seed → generator → key pair at ordinal 0), while ed25519 uses a simpler direct derivation, so the `KeyType` parameter at those sites does real algorithmic switching, not just labeling.

In `PublicKey.h`, the inverse direction is provided: `publicKeyType(Slice const&)` reads the lead byte(s) of a serialized public key and returns `std::optional<KeyType>` — so the type can be recovered from the wire format without any additional metadata. This self-describing encoding is why `KeyType` need not be stored separately alongside a public key. The function returning `std::nullopt` for an unrecognised lead byte mirrors the same safe-optional pattern as `keyTypeFromString()`.

In `Sign.h`, the `sign(STObject&, HashPrefix const&, KeyType, SecretKey const&, ...)` overload takes `KeyType` explicitly because it must derive the public key internally to write into the object's signature field — an operation that requires knowing which algorithm the secret key belongs to.

Together these headers form a coherent cryptographic key API: `KeyType.h` supplies the shared discriminant, `SecretKey.h` generates and uses keys, `PublicKey.h` verifies and identifies them, and `Sign.h` ties both together for serialized ledger objects.