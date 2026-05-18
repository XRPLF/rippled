# Sign.cpp — Protocol-Level Signing and Verification for XRPL Objects

`Sign.cpp` is the thin but critical bridge between XRPL's raw cryptographic primitives (`SecretKey`/`PublicKey` signing) and the serialized ledger object layer (`STObject`). It answers the question: *given an arbitrary XRPL protocol object, how do you produce and verify a canonical signature over it?* The file contains four short functions whose design choices carry significant protocol-security weight.

## Serialization before Signing

Both `sign()` and `verify()` follow the same three-step pattern: build a `Serializer`, add the `HashPrefix`, then call `STObject::addWithoutSigningFields()`. The `addWithoutSigningFields()` call is the key detail: it serializes every field of the object *except* the signature fields themselves. This breaks the obvious circularity — you cannot include a signature in the data you sign — and ensures that the signing serialization is deterministic and unambiguous.

The `HashPrefix` prepended to the serializer is a 4-byte domain-separation tag defined as a three-character ASCII string with a trailing null byte (e.g. `"STX\0"` for `txSign`, `"SMT\0"` for `txMultiSign`). Every distinct use of a signature in the protocol gets its own prefix. This makes it impossible to replay a valid signature from one context (say, a transaction authorization) as a valid signature in another (say, a ledger validation). The `HashPrefix` enum spans at least ten such domains — transactions, inner nodes, validations, proposals, manifests, payment channel claims, and more.

## `sign()` and `verify()`

The `sign()` overload is four lines: serialize the object, call the lower-level `sign(type, sk, slice)` from `SecretKey.h`, then `set()` the result into `sigField`. The default `sigField` is `sfSignature`, the standard transaction signature field, but the parameter allows the same logic to serve multi-signer contexts where the signature lives in a nested `Signer` entry.

The `verify()` overload first calls `get(st, sigField)` and returns `false` immediately if the field is absent — a clean, no-exception guard. It then rebuilds the identical serialized blob and delegates to the cryptographic `verify(pk, message, signature)`. There is an important asymmetry: `sign()` takes a `KeyType` alongside the `SecretKey` (needed to distinguish secp256k1 from ed25519 at signing time), while `verify()` only needs the `PublicKey`, because the key type is encoded in the public key's first byte in the XRPL wire format.

## Multi-Signature Data Construction

The multi-signing functions reveal a deliberate performance trade-off. `buildMultiSigningData()` is the straightforward form: serialize the full object under `HashPrefix::txMultiSign`, then append the `signingID`. But the header also exposes a two-part split via `startMultiSigningData()` and `finishMultiSigningData()`.

The rationale is batch verification efficiency. When a transaction carries multiple signers, the object serialization (the large part) is identical for all of them. Rather than reserializing the entire object once per signer, a validator calls `startMultiSigningData()` once, then calls `finishMultiSigningData(signerID, s)` for each signer in sequence, replacing only the small signer-specific tail. `finishMultiSigningData()` is an inline function in the header that appends the `AccountID` bit-string to the shared serializer.

## Why the Signing Account ID Must Be Included

The code contains an unusually detailed comment — attributed to David Schwartz — explaining why the signer's `AccountID` is appended to the multi-signing blob. Without it, an attacker who controls an entry in a `SignerList` could substitute *any other* signer who also holds a RegularKey pointing to the same third-party key. This kind of shared-RegularKey scenario is realistic for custodial services and exchange operators. Including the `AccountID` in the signing data makes each signer's authorization cryptographically specific to that account: you cannot transfer a signature from one signer slot to another.

The comment also foreshadows future protocol evolution: if XRPL ever supports *nested* multi-signing (Carol signs for Bob who signs for Alice), the intermediate "signing-for" account IDs would also need to be incorporated into the data blob. The current two-level design (signer account + transaction account) is already in place, with the transaction's `Account` field naturally present in the serialized `STObject`, and the signer's identity added by `finishMultiSigningData()`.

## Relationship to Surrounding Code

This file sits at the intersection of three layers. Below it: `SecretKey.h` provides the raw `sign(KeyType, SecretKey, Slice)` and `verify(PublicKey, Slice, Slice)` functions that do actual elliptic-curve or EdDSA operations. Above it: transaction processing code calls these `sign()`/`verify()` wrappers directly on `STTx` (a subclass of `STObject`) without needing to know anything about serialization details. Alongside it: `STObject::addWithoutSigningFields()` implements the field-exclusion logic that makes the serialization canonical, and `HashPrefix` ensures every signature domain remains cryptographically isolated.

The design is tightly minimal — less than 90 lines total — because each concern is delegated to the right layer. The protocol glue here is just the composition: prefix + non-signing serialization + signer identity, with the cryptographic heavy lifting pushed entirely into `SecretKey.cpp`.