# XChainAttestations.cpp

This file implements the attestation type system for XRPL's cross-chain bridge protocol. Bridges connect a "locking chain" and an "issuing chain," with a set of independent witness servers observing events on each chain and cryptographically attesting that specific transfers occurred. `XChainAttestations.cpp` defines the data structures that carry those signed proofs both as wire format (for transactions submitted by witnesses) and as ledger state (for aggregated proofs stored in claim ID objects).

## Two Parallel Hierarchies

The design deliberately splits attestation representation into two distinct namespaces and class families:

**Signing-side types** live in `namespace Attestations` and represent a complete attestation as submitted by a witness server: they carry the raw `Buffer signature`, the signer's `AccountID`, a `PublicKey`, the source event details, and the reward routing information. `AttestationBase` holds all the fields common to both transfer types. `AttestationClaim` extends it with a `claimID` (the monotonic counter that prevents replay) and an optional `dst` (destination override). `AttestationCreateAccount` instead carries `createCount`, the mandatory `toCreate` account, and a `rewardAmount` — the additional field needed for the account-bootstrapping flow.

**Ledger-storage types** (`XChainClaimAttestation`, `XChainCreateAccountAttestation`) are what actually persists in the ledger's claim ID entries. They strip out the raw signature, reducing stored data to only the `keyAccount`, `publicKey`, amounts, and routing fields. The `TSignedAttestation` typedef inside each ledger type explicitly names the corresponding signing-side type, making the semantic link clear without runtime coupling. The conversion constructors from `TSignedAttestation` project the signing-side representation into the ledger representation in a single step.

## Message Serialization and Signing

Each `Attestations::` subtype exposes both a static `message()` overload accepting all fields explicitly, and an instance overload that delegates to it. The static form lets the test harness (`attester.cpp`) sign attestations without constructing an object, while the instance form supports `verify()` via the abstract virtual `message()` on `AttestationBase`.

The serialized message is built by populating an `STObject{sfGeneric}` and calling `Serializer::add()` on it — identical to how ledger objects are canonically serialized. A comment in both `AttestationClaim::message()` and `AttestationCreateAccount::message()` explains that fields are written in `SField` order to ease implementation of independent Python serializers, a cross-ecosystem compatibility concern. The resulting bytes are then signed with `xrpl::sign()` or verified with `xrpl::verify()`.

`AttestationBase::verify()` calls the virtual `message()` to regenerate the canonical bytes from the stored fields and tests them against the `publicKey`/`signature` pair. This is called during transaction preflight in `XChainBridge.cpp` (`attestationPreflight`) as the first rejection gate — if the witness's signature doesn't check out, the transaction returns `temXCHAIN_BAD_PROOF` before any state is touched.

## The AttestationMatch Three-State Result

The `match()` method on the ledger-storage types returns `AttestationMatch`, an enum with three values: `match`, `matchExceptDst`, and `nonDstMismatch`. This nuance reflects an intentional semantic difference between two transaction types:

- When a witness submits `XChainAddClaimAttestation`, the destination in the accumulated attestations must all agree (`match` required).
- When a user submits `XChainClaim` explicitly, they specify the destination themselves, so collected attestations with a different destination are still eligible; `matchExceptDst` is acceptable.

The `claimHelper` function in `XChainBridge.cpp` passes a `CheckDst` flag and branches on this result, accepting `matchExceptDst` when the user is claiming explicitly. Without the three-state enum this logic would require two separate matching passes.

## Container and Bounds Defense

`XChainAttestationsBase<TAttestation>` is a thin template wrapper around a `std::vector<TAttestation>` that enforces a hard cap of `maxAttestations = 256` at construction time from both `STArray` and `Json::Value` inputs. The comment notes this is far above any realistic witness-set size; the limit exists purely to bound memory allocation and processing time against malformed or malicious input. The protected destructor prevents external code from slicing instances of the concrete final subclasses `XChainClaimAttestations` and `XChainCreateAccountAttestations`.

The template is explicitly instantiated at the bottom of the `.cpp` for both concrete types. This keeps all the template method bodies in the `.cpp` translation unit rather than the header, which would otherwise require every translation unit including the header to see and compile the full implementation.

## sameEvent vs. Equality

`sameEvent()` on the `Attestations::` types checks whether two attestations witness the same cross-chain event — same `sendingAccount`, `sendingAmount`, `wasLockingChainSend`, and for claims the same `claimID` and `dst`. It deliberately ignores the signer identity fields (`attestationSignerAccount`, `publicKey`, `signature`). Full `operator==` requires all fields to match. The separation is used when processing incoming attestations: an existing attestation for the same event from a different witness should be counted toward quorum, not treated as a duplicate entry.