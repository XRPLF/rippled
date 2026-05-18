# XChainAttestations.h

## Purpose and Context

This file is the type-system foundation for XRPL's cross-chain bridge protocol. When assets move between a locking chain and an issuing chain, a quorum of off-chain *witness servers* must independently attest to each event before the destination chain will act. This header defines the full type hierarchy for those attestations: how they are constructed, signed, serialized, compared, and stored.

There are two distinct cross-chain event kinds, each with its own attestation type: a regular transfer (identified by a monotonic `claimID`) and an account-creation transfer (identified by a `createCount` sequence). The design cleanly separates the "full" attestation objects used during transaction submission from the "stored" attestation objects that live in on-chain ledger entries.

## Two Namespaces, Two Representations

The header uses two levels deliberately. Inside `xrpl::Attestations` live `AttestationClaim` and `AttestationCreateAccount`, both inheriting from `AttestationBase`. These are the *full* attestations: they carry the raw cryptographic signature `Buffer`, the `sendingAccount` that originated the event, and all event details. A witness server constructs one of these, signs it, and submits it to the destination chain.

Directly in the `xrpl` namespace are `XChainClaimAttestation` and `XChainCreateAccountAttestation`. These are the *stored* attestations that get placed into ledger objects like `XChainOwnedClaimID`. They shed the raw signature bytes and the sending account, retaining only the `keyAccount` (the signer's account ID), `publicKey`, amount, reward fields, and direction flag. The `TSignedAttestation` typedef on each links it back to its full counterpart, and a converting constructor accepts the full type, extracting what the ledger needs to keep.

## Signature Verification and Message Construction

`AttestationBase::verify()` calls the pure virtual `message(STXChainBridge const&)` to reconstruct the exact byte sequence that was signed, then calls `xrpl::verify()` against the stored `publicKey` and `signature`. Making `message()` pure virtual is the right call: `AttestationClaim` and `AttestationCreateAccount` sign different fields, and forcing each subclass to supply its own byte construction prevents the base class from ever silently producing an incorrect or incomplete message.

Both concrete types also expose a `static` overload of `message()` taking explicit field values. This allows callers to verify a signature from raw data without first constructing an attestation object — useful for validation paths that receive untrusted input and want to check the signature before accepting any of it.

The static `message()` implementations serialize their fields into a `STObject` using canonical SField ordering before calling `Serializer`. The comment in the .cpp is direct: this ordering exists to make Python serializers easier to write. Witness servers in the ecosystem are not necessarily C++ processes, so the message format must be deterministic and independently reproducible in other languages.

## Constructor Overloads: Parsing vs. Signing

Both `AttestationClaim` and `AttestationCreateAccount` offer two "full" constructors beyond the deserialization ones. One takes a pre-computed `Buffer signature_` for attestations arriving over the network. The other takes a `SecretKey const& secretKey_` and computes the signature inline by calling `message()` and `sign()`. The signing constructor is primarily for test harnesses and the witness-server implementation itself; the non-signing constructor is for the validation path on the destination chain that receives attestations it must verify.

The signing constructor first delegates to the no-signature constructor (passing an empty `Buffer{}`), then overwrites the `signature` field immediately after construction. This avoids code duplication while ensuring the object is valid before signing.

## Equality and Event Matching

There is a meaningful distinction between `operator==` and `sameEvent()`. Full equality (`operator==`) checks every field including signer identity and raw signature bytes — two attestations are equal only if they came from the same witness signing the same data. `sameEvent()` checks only the event-describing fields (sending account, amount, direction, and the type-specific ID), ignoring who signed. This distinction is essential for quorum logic: the system needs to know whether two *different* signers attested to the *same* event to count towards consensus.

The `AttestationMatch` enum adds a third dimension for the stored attestations. `match()` returns `nonDstMismatch` if core amount or direction fields differ, `matchExceptDst` if everything matches except the optional destination address, or `match` for full agreement. The `dst` field on claim attestations is `std::optional<AccountID>`, reflecting that the destination on the receiving chain may or may not be specified. Witnesses can attest to the same transfer but with different `dst` opinions, so the code must distinguish "everything matches" from "same transfer, different destination".

## The `XChainAttestationsBase<TAttestation>` Container

The template `XChainAttestationsBase` wraps a `std::vector<TAttestation>` and adds serialization to/from `STArray` and `Json::Value`. The `maxAttestations = 256` cap is enforced at both `STArray` and JSON parse paths; without this, an attacker could craft an oversized array to allocate memory and slow consensus processing. The value is deliberately far above practical quorum sizes.

The destructor is `protected`, not `public`, to prevent slicing: neither `XChainClaimAttestations` nor `XChainCreateAccountAttestations` add virtual methods, so accidental deletion through a base pointer would silently be wrong. The two concrete classes are purely thin wrappers that inherit all constructors via `using TBase::TBase`, existing only to provide distinct named types for the type system.

`CmpByClaimID` and `CmpByCreateCount` are comparator structs for sorting attestation collections by their sequencing key, enabling ordered iteration and binary search when collecting a quorum across multiple submitted attestations.

## Relationship to Other Files

`STXChainBridge` (from `STXChainBridge.h`) is the bridge descriptor — it identifies the locking and issuing chain door accounts and asset types. Every `message()` call embeds the full bridge description in the signed payload, which means attestations are cryptographically bound to a specific bridge instance; a valid signature for one bridge cannot be replayed on another. The `wasLockingChainSend` boolean threads through from `STXChainBridge::srcChain()` / `dstChain()` helpers, keeping directionality consistent across the system.