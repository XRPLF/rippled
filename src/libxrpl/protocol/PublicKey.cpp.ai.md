# `PublicKey.cpp` — Public Key Implementation for XRPL Cryptography

This file implements the core cryptographic infrastructure for XRPL public keys: construction and validation of the `PublicKey` value type, signature canonicality enforcement, signature verification for both supported elliptic curve systems, and node identity derivation. It is the implementation counterpart to `include/xrpl/protocol/PublicKey.h`.

## The `PublicKey` Value Type

`PublicKey` stores exactly 33 bytes in a fixed inline buffer `buf_[33]`. This is not accidental — it is a deliberate encoding decision that unifies two incompatible key formats into a single uniform type. A secp256k1 compressed public key is natively 33 bytes (a sign byte `0x02` or `0x03` followed by the X coordinate). An Ed25519 public key is 32 bytes of key material, but XRPL prefixes it with a constant `0xED` byte, also giving it 33 bytes. The leading byte therefore acts as a self-describing type tag: `0x02`/`0x03` means secp256k1, `0xED` means Ed25519.

The `publicKeyType()` free function encodes this discriminator logic. Its existence as a free function — rather than a member method — is important because it is also used to *validate* raw byte slices before constructing a `PublicKey`. The constructor enforces this as a precondition via `LogicError`, which terminates the process rather than returning an error. This is appropriate because receiving a malformed `PublicKey` would indicate a programming error (e.g., bypassed deserialization), not a recoverable runtime condition.

Default construction is deleted. There is no such thing as an "empty" or "uninitialized" `PublicKey`, which eliminates a whole class of use-after-construction bugs where callers might forget to populate the key.

Copy construction and copy assignment use `std::memcpy` directly rather than the compiler-generated copy, which is safe because the storage is a plain `uint8_t` array and avoids any potential overhead from element-wise copying.

## Signature Canonicality — The Malleability Defense

The most architecturally significant logic in this file is the dual-tier canonicality system for ECDSA signatures. XRPL's `ECDSACanonicality` enum has two values: `canonical` and `fullyCanonical`. Understanding why both exist requires understanding transaction malleability.

For any signed message, an ECDSA signature `(R, S)` has a mathematical equivalent `(R, G-S)` where G is the secp256k1 curve order. Both are valid signatures, but they produce different serializations — and thus different transaction hashes. This was exploited historically to mutate a transaction's ID without invalidating the signature, breaking systems that tracked transactions by ID. XRPL's response is to mandate that valid signatures use only the *lower* value of S (i.e., `S ≤ G/2`), which is the `fullyCanonical` form. Old signatures with `S > G/2` are classified as merely `canonical` — structurally valid but not fully canonical.

`ecdsaCanonicality()` implements this check. It first parses the DER-encoded structure by calling the `sigPart()` helper twice to extract R and S as byte slices. `sigPart()` is a stateful parser that advances its `Slice` argument in place, consuming input as it validates DER's `0x02 <len> <value>` integer encoding. It rejects negatives (high bit set), zero values, and unnecessary zero-padding, all of which are real DER malformations seen in practice.

The comparison of R and S against the curve order G requires big-integer arithmetic. The values are only available as raw byte strings, so `sliceToHex()` converts each to a hex literal string that `boost::multiprecision::number` can parse. The type alias `uint264` uses a 264-bit signed integer (33 bytes), which is one byte wider than the 32-byte curve order, to safely hold values up to G without overflow during the `G - S` computation.

`ed25519Canonical()` performs the analogous check for Ed25519: the second 32 bytes of a signature encode the scalar S, which must be less than the Ed25519 subgroup order. The bytes arrive in little-endian order (per the Ed25519 spec), so `std::reverse_copy` produces a big-endian representation for `std::lexicographical_compare` against the hard-coded big-endian order constant.

## Signature Verification

`verify()` is the general-purpose verification entry point. It dispatches on key type:

- **secp256k1**: Hashes the message with SHA-512 Half (truncated to 256 bits), then calls `verifyDigest()`. The indirection through `sha512Half` before hashing is an XRPL-wide convention — raw secp256k1 verification happens only against digests, never directly against messages.
- **Ed25519**: Checks canonicality first, then strips the `0xED` prefix byte (`publicKey.data() + 1`) before calling the external `ed25519_sign_open()` library function. The prefix is purely an XRPL encoding artifact; the underlying Ed25519 library has no knowledge of it.

`verifyDigest()` handles the secp256k1 path in detail. If the signature is merely `canonical` rather than `fullyCanonical`, it calls `secp256k1_ecdsa_signature_normalize()` to convert S to its low form before verifying. This means the ledger will accept old-style non-fully-canonical signatures by normalizing them, rather than simply rejecting them — a deliberate backward-compatibility choice.

The `secp256k1Context()` helper in `detail/secp256k1.h` manages the libsecp256k1 context as a function-local static with a RAII wrapper, initializing it once on first call with both `SECP256K1_CONTEXT_VERIFY` and `SECP256K1_CONTEXT_SIGN` flags. This avoids global constructor ordering issues while ensuring a single shared context across all callers.

## Node Identity Derivation

`calcNodeID()` maps a validator's public key to its 160-bit network identity by applying RIPEMD-160(SHA-256(pubkey)) — the same `ripesha_hasher` used to derive XRPL account IDs from public keys. Node IDs are used in the peer-to-peer layer for routing and identification, which is why a validator's node public key and its network address together form a fingerprint (see `getFingerprint()` in the header).

## `parseBase58` Specialization

The template specialization of `parseBase58<PublicKey>` decodes a Base58Check-encoded public key from a string, validating first the Base58 framing (token type prefix) and then the decoded bytes via `publicKeyType`. It returns `std::nullopt` for any malformed input, making it safe to call on untrusted data. The `toBase58` free function in the header is its inverse, encoding a `PublicKey` for human-readable display (e.g., in configuration files or the JSON API).