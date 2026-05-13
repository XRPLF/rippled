# Cryptography

XRPL supports secp256k1 (ECDSA) and ed25519 key types. All crypto uses OpenSSL + dedicated libs (libsecp256k1, ed25519-donna).

## Key Invariants

- `SecretKey` destructor calls `secure_erase` on internal buffer; any code handling secret keys must follow this pattern
- ed25519 public keys are prefixed with `0xED` (33 bytes total); secp256k1 keys are 33-byte compressed
- `sha512Half` (first 32 bytes of SHA-512) is the standard hash used throughout XRPL for node hashing, signing, etc.
- `RIPEMD-160(SHA-256(x))` is used for account ID derivation (`ripesha_hasher`)
- Base58 encoding includes a type byte prefix and 4-byte checksum (double SHA-256)

## Common Bug Patterns

- Mixing up key types: secp256k1 signing hashes the message with sha512Half first, ed25519 signs the raw message
- `signDigest` only works with secp256k1; calling it with ed25519 throws a logic error
- Signature canonicality: ed25519 `verify` checks signature canonicality before calling `ed25519_sign_open`; non-canonical signatures are rejected
- Overlay handshake uses `signDigest` to sign the session fingerprint (`sharedValue`); the signature binds the TLS session to the node identity

## Review Checklist

- New crypto code must use `crypto_prng()` singleton for randomness, never raw `rand()`
- Secret key buffers must be `secure_erase`d after use
- Verify that key type dispatch handles both secp256k1 and ed25519 (or explicitly rejects one with a clear error)

## Key Patterns

### Secure Erasure
```cpp
// REQUIRED: destructor must erase secret material
SecretKey::~SecretKey()
{
    secure_erase(buf_, sizeof(buf_));
}

// REQUIRED: erase intermediate buffers after use
beast::rngfill(buf, sizeof(buf), crypto_prng());
SecretKey sk(Slice{buf, sizeof(buf)});
secure_erase(buf, sizeof(buf));  // MUST erase raw buffer
```

### Key Type Dispatch
```cpp
// REQUIRED: handle both key types or explicitly reject
if (type == KeyType::ed25519)
{   /* ed25519 path */ }
else if (type == KeyType::secp256k1)
{   /* secp256k1 path */ }
else
    LogicError("unknown key type");  // MUST NOT fall through silently
```

## Key Files

- `include/xrpl/protocol/SecretKey.h` / `PublicKey.h` - key types
- `src/libxrpl/protocol/SecretKey.cpp` - signing, key generation
- `src/libxrpl/protocol/PublicKey.cpp` - verification
- `include/xrpl/protocol/digest.h` - hash functions
- `src/xrpld/overlay/detail/Handshake.cpp` - overlay handshake crypto
