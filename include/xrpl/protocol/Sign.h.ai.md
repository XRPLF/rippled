# `include/xrpl/protocol/Sign.h`

## Purpose and Role

`Sign.h` defines the signing and verification API for serialized XRPL protocol objects. It sits at the intersection of the cryptographic layer (`PublicKey`, `SecretKey`, `KeyType`) and the serialization layer (`STObject`, `HashPrefix`), providing the thin interface that everything from transaction submission to multisignature validation depends on. The implementation lives in `src/libxrpl/protocol/Sign.cpp`.

## Core Signing Model

Every signable object in the XRPL goes through the same pipeline: serialize the object's non-signature fields, prepend a domain-separation prefix, and feed the resulting bytes into the asymmetric signing function. The `sign()` function encapsulates this:

```cpp
void sign(STObject& st, HashPrefix const& prefix, KeyType type,
          SecretKey const& sk, SF_VL const& sigField = sfSignature);
```

Internally, a `Serializer` is constructed, the 4-byte `HashPrefix` is written first, then `st.addWithoutSigningFields(ss)` appends the canonical binary encoding of the object *with all signing-related fields omitted*. This break of the circular dependency — you cannot include the signature in the data you are signing — is handled entirely by `STObject::addWithoutSigningFields`. The resulting byte slice is passed to the lower-level `sign(KeyType, SecretKey const&, Slice)` function from `SecretKey.h`, which applies SHA512-Half hashing followed by secp256k1 or Ed25519 signing depending on `type`. The produced signature buffer is stored back into `st` at `sigField`.

The `verify()` function mirrors this exactly: it reads the signature out of the object, regenerates the same serialized payload (prefix + fields-without-signatures), and calls the standalone `verify(PublicKey, Slice message, Slice sig)` function.

## HashPrefix: Domain Separation

The `HashPrefix` enum is a protocol-level invariant. Each prefix is a 4-byte big-endian value constructed from three ASCII characters, with the low byte fixed to zero (e.g., `txSign` = `"STX\0"`, `txMultiSign` = `"SMT\0"`). By injecting this prefix before the serialized payload, the protocol ensures that a valid signature over a transaction cannot be replayed as a valid signature over a ledger header, a validation, or a payment channel claim — even if they happen to share identical binary content after serialization. The `HashPrefix` enum is part of the wire protocol and must never be changed.

## Multi-Signing: Two-Phase Optimization

The protocol supports multi-signature transactions where a set of signers collectively authorize a transaction. Verifying N signers naively would require serializing the transaction body N times. The header exposes a split-phase API to avoid this:

```cpp
Serializer startMultiSigningData(STObject const& obj);   // serialize once
inline void finishMultiSigningData(AccountID const& signingID, Serializer& s); // append per-signer
```

`startMultiSigningData` prepends `HashPrefix::txMultiSign` and serializes the full transaction body without signing fields — the expensive shared work. `finishMultiSigningData` appends a single `AccountID` (32 bytes) to complete the per-signer payload. The convenience function `buildMultiSigningData` calls both in sequence for single-signer use.

The `.cpp` includes an unusually detailed comment explaining *why* the signer's own `AccountID` must be appended. Without it, an attacker could substitute one `Signer.Account` for another account that happens to share the same `RegularKey` — for example, when a third-party service provides a common `RegularKey` across many accounts. Including the `AccountID` in the signed data binds each signature to a specific account identity, making that entire class of substitution attacks impossible. The comment also anticipates future multi-level signing hierarchies (Carol signs for Bob who signs for Alice), where distinguishing which intermediate account a signer is authorizing would require incorporating the "signing for" chain.

## `SF_VL` and the `sfSignature` Default

`SF_VL` is a `TypedField<STBlob>` — a named field descriptor for a variable-length binary blob within an `STObject`. The default parameter `sfSignature` is the standard transaction signature field, but the API accepts any `SF_VL` to support alternative signing contexts such as multi-signer inner structures, manifest signatures, or payment channel claims. The field-parameterized design avoids needing separate functions for each signing context.

## SecretKey Safety Conventions

`SecretKey` deliberately deletes `operator==` and `operator!=` to prevent timing-channel comparisons and to discourage accidental key equality checks. It also omits `operator<<` to prevent secret material from leaking into log streams. These are not accidental omissions — the destructor zeroes the key buffer, making `SecretKey` a security-sensitive RAII type. `Sign.h` operates directly with `SecretKey const&`, never copying or persisting the key material beyond the duration of the `sign()` call.

## Relationship to Other Files

- **`HashPrefix.h`** — supplies the domain-separation enum; this header cannot function without it.
- **`SecretKey.h`** — provides both the `SecretKey` type and the underlying `sign(KeyType, SecretKey, Slice)` free function that `Sign.h`'s `sign()` delegates to.
- **`PublicKey.h`** — provides `PublicKey` and the corresponding `verify(PublicKey, Slice, Slice)` function.
- **`STObject.h`** — provides `addWithoutSigningFields`, which is the serialization primitive that makes the circular-free encoding work.