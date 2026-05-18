# `include/xrpl/protocol/PublicKey.h`

## Role and Purpose

This header defines the `PublicKey` class — the canonical representation of a public key anywhere in the XRPL protocol stack. Because XRPL supports two distinct elliptic curve cryptosystems (secp256k1 and Ed25519), every public key must carry its algorithm identity alongside the raw bytes. This file provides the type-safe wrapper that enforces that invariant, plus the full suite of operations that operate on public keys: algorithm detection, signature verification, ECDSA canonicality analysis, and derivation of both network node identities and on-ledger account identities.

## The `PublicKey` Class

`PublicKey` is an immutable, fixed-size value type holding exactly 33 bytes. The size is not arbitrary — secp256k1 compressed public keys are 33 bytes (a sign byte `0x02`/`0x03` followed by the 32-byte X coordinate), and Ed25519 keys are padded to 33 bytes by prepending the constant prefix `0xED`. This uniform size is a deliberate encoding decision: it makes the algorithm self-describing from the lead byte alone, enabling `publicKeyType()` to detect the cryptosystem in O(1) with no external metadata.

The default constructor is explicitly deleted. `PublicKey` can only be constructed from a `Slice`, and the constructor validates the input by calling `publicKeyType()` before copying bytes into the internal buffer — if the slice does not match a known key format, it calls `LogicError`, which terminates under normal build configurations. This means any live `PublicKey` object is always a well-formed, algorithm-identified key; there is no "empty" or "default" state. Copy construction and assignment are provided (both using `std::memcpy` over the fixed 33-byte buffer) with an explicit self-assignment guard.

The class exposes a `data()`/`size()` pair, iterator range via `begin()`/`end()`, and an implicit conversion to `Slice`. The implicit `Slice` conversion is intentional: it allows `PublicKey` to flow naturally into any API expecting raw byte ranges, including the serialization and hashing infrastructure, without explicit casting at every call site.

## Algorithm Detection via `publicKeyType()`

The free function `publicKeyType(Slice const&)` is the gateway for all algorithm detection. It checks three conditions: the slice is exactly 33 bytes, and the lead byte is `0xED` (Ed25519), `0x02`, or `0x03` (secp256k1 compressed forms). Anything else returns `std::nullopt`. The overload accepting `PublicKey const&` simply forwards to the slice overload — importantly, the `[[nodiscard]]` attribute on both ensures callers cannot silently ignore a failed type check.

## ECDSA Signature Canonicality

The `ECDSACanonicality` enum and `ecdsaCanonicality()` function address a well-known property of ECDSA: for any valid signature `(R, S)`, the tuple `(R, G-S)` is equally valid, where G is the curve group order. This means a transaction signed once actually has two valid signatures, enabling transaction malleability attacks where an adversary modifies the signature bytes while preserving cryptographic validity.

`ecdsaCanonicality()` first validates the DER-encoded structure of the signature using the internal `sigPart()` helper, which checks the `0x30`/`0x02` DER framing, enforces length bounds (8–72 bytes total), rejects negative-encoded integers, and rejects redundant zero padding. It then extracts R and S as `boost::multiprecision` 264-bit integers (slightly wider than the 256-bit curve, to accommodate the big-endian representation safely) and compares against the curve order G. A signature is *canonical* if both R and S are in `[1, G)`. It is *fully canonical* if additionally `S ≤ G-S` — meaning S lies in the lower half of the group order, making the signature unique. The XRPL by default requires `fullyCanonical` for new transactions, though `verifyDigest()` exposes a `mustBeFullyCanonical` flag to allow relaxed verification in legacy contexts.

## Signature Verification

Two verification functions are provided with complementary levels of abstraction.

`verifyDigest()` accepts a `uint256` pre-hashed digest and is secp256k1-only. It guards with a `LogicError` if called with an Ed25519 key. After checking canonicality, it parses the public key and signature through libsecp256k1's DER parser and calls `secp256k1_ecdsa_verify`. If the signature is only canonical (not fully canonical) and `mustBeFullyCanonical` is false, it normalizes the signature via `secp256k1_ecdsa_signature_normalize` before verifying — this handles the case where the lower-S form was not used. Both functions are marked `noexcept`; errors return `false` rather than throw.

`verify()` is the higher-level dispatch: it calls `publicKeyType()` to branch on the cryptosystem. For secp256k1, it hashes the message with SHA512-Half and delegates to `verifyDigest()`. For Ed25519, it first checks `ed25519Canonical()` — which validates the S component of the signature against the Ed25519 subgroup order by byte-reversing the little-endian S value to big-endian for comparison — and then calls `ed25519_sign_open()` with `publicKey.data() + 1`, stripping the `0xED` prefix that XRPL adds for key-type tagging but that the underlying Ed25519 library does not understand.

## Serialization Integration via `STExchange`

The header provides a full specialization of `STExchange<STBlob, PublicKey>`. This fits into XRPL's typed serialization framework: `STExchange` is the bridge between C++ value types and their serialized `ST*` representations. The specialization allows `PublicKey` to be read from and written into `STBlob` fields in serialized ledger objects and transactions without any conversion boilerplate at call sites using `get<>` and `set<>` from `STExchange.h`.

## Identity Derivation

Two functions derive protocol-level identities from a public key:

- `calcNodeID()` computes a 160-bit `NodeID` using RIPESHA (RIPEMD160 over SHA256 of the raw key bytes) — this is the identifier used for peer-to-peer network routing and consensus tracking.
- `calcAccountID()` derives the on-ledger account address. The implementation lives in `AccountID.cpp` rather than `PublicKey.cpp`; a comment in the header acknowledges this placement is a workaround for header dependency ordering, not a design preference.

## Base58 Encoding and JSON Parsing

`toBase58()` encodes the raw key bytes using XRPL's custom Base58Check alphabet with a caller-supplied `TokenType` prefix (typically `NodePublic` for validators or `AccountPublic` for signing keys). The `parseBase58<PublicKey>` template specialization validates the decoded bytes through `publicKeyType()` before construction.

The `Json::getOrThrow<PublicKey>` specialization in the `Json` namespace provides flexible deserialization from JSON field values: it first attempts raw hex decoding, then falls back to trying `NodePublic` and `AccountPublic` Base58 encodings in order. This handles the variety of formats that appear in RPC requests and configuration files.

`getFingerprint()` is a logging utility that formats a human-readable string combining a peer's IP address, optional node public key (as NodePublic Base58), and an optional session ID — used for diagnostic and audit logging of network peer connections.