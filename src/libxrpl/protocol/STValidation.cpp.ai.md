# `STValidation.cpp` — Consensus Validation Object Implementation

`STValidation` represents a single validator's signed assertion that it has agreed to close a specific ledger during the XRPL consensus process. This `.cpp` file implements the non-template, non-inline methods of that class; the constructors and several small accessor inlines live in `STValidation.h` because they are templated or hot-path enough to warrant inlining.

## Role in the Consensus Pipeline

Every participating XRPL validator broadcasts `STValidation` messages after each consensus round. A validation carries the hash of the ledger the validator agrees on, when it was signed, optional fee and amendment data the validator wants to advertise, and a cryptographic signature over all of that. The consensus engine accumulates validations from a quorum of trusted validators before it considers a ledger final. Because these objects flow across the network from untrusted peers, they must be verified before being acted on — and verified cheaply, because thousands may arrive per second.

## Field Schema via `validationFormat()`

`validationFormat()` returns a `SOTemplate` that declares every field a validation may contain, with its presence rule (`soeREQUIRED`, `soeOPTIONAL`, or `soeDEFAULT`). The template is a function-local static rather than a namespace-scope global. The comment explains why: the `SField` objects it references are themselves globals, and C++ gives no inter-translation-unit initialization order guarantee. A function-local static is initialized on first call, by which point all `SField` singletons are already alive.

The required fields — `sfFlags`, `sfLedgerHash`, `sfLedgerSequence`, `sfSigningTime`, `sfSigningPubKey`, `sfSignature` — form the non-negotiable nucleus. Optional fields such as `sfLoadFee`, `sfAmendments`, `sfBaseFee`, `sfReserveBase`, and `sfReserveIncrement` carry advisory network-state data validators may publish. Three fields tagged `// featureXRPFees` — `sfBaseFeeDrops`, `sfReserveBaseDrops`, `sfReserveIncrementDrops` — are newer `soeOPTIONAL` entries added by the XRPFees amendment; they coexist with the legacy fee fields to allow gradual adoption without breaking older validators.

`sfCookie` is declared `soeDEFAULT`, meaning it is always included in the serialized output but takes a default (zero) value if not explicitly set. This prevents fingerprinting validators that omit it while keeping the field unconditionally present for parsing.

## Lazy, Cached Signature Verification in `isValid()`

```cpp
mutable std::optional<bool> valid_;
```

The `valid_` member is unseated (`std::nullopt`) until the first call to `isValid()`. On that call the method verifies the signature and caches the result. Subsequent calls short-circuit to the cached value. This design is deliberate: constructing from a peer stream (`SerialIter`-based constructor) may optionally skip the signature check via the `checkSignature` flag, deferring the cost to the consumer. When the node constructs its own validation (the signing constructor), it sets `valid_ = true` immediately after calling `signDigest`, skipping a redundant round-trip through verification.

Inside `isValid()`, an `XRPL_ASSERT` first checks that the public key type is `secp256k1`. This is a guard against the key escaping the construction-time check in an unusual code path — if the assert fires, the caller has introduced a logic error. The actual cryptographic check is `verifyDigest()` from `PublicKey.h`, which operates on the 256-bit pre-hashed digest rather than the raw message, because `getSigningHash()` has already applied SHA-512-Half. The `vfFullyCanonicalSig` flag is passed to require strict low-S canonicality, preventing the ECDSA malleability vector.

## Two Notions of Time

`getSignTime()` reconstructs a `NetClock::time_point` from the serialized `sfSigningTime` field — this is the time at which the validator signed the message and is part of the validated data. `getSeenTime()` returns `seenTime_`, which is set locally by the receiving node when the validation arrives and is never serialized. This distinction matters for replay-protection and for measuring network latency without trusting the sender's clock.

## Full vs. Partial Validations

`isFull()` checks `vfFullValidation` in `sfFlags`. A *full* validation is the definitive affirmation that the validator has observed a complete, validated ledger. A *partial* (or *tentative*) validation may be broadcast during an in-progress consensus round to signal early support for a candidate ledger. The consensus engine treats these differently: only full validations count toward quorum for ledger finality.

## Signing Hash Namespace

`getSigningHash()` delegates to `STObject::getSigningHash(HashPrefix::validation)`. The `HashPrefix::validation` constant is the 4-byte big-endian encoding of `'V','A','L',0x00`, prepended to the serialized object before hashing. This domain-separation technique ensures that a byte sequence that is a valid transaction serialization cannot produce the same digest as a validation, and vice versa — a critical defense against cross-type signature reuse attacks.

## Polymorphic Copy/Move via `emplace()`

`copy()` and `move()` override pure virtuals from `STBase`. They delegate to the inherited `emplace()` helper, which placement-constructs the object into a caller-supplied buffer of `n` bytes. This pattern supports `STObject`'s value-semantic storage of heterogeneous `ST*` variants without heap allocation per element — a performance-sensitive concern when thousands of serialized objects are being parsed in the critical path.

## Construction Invariants (from the Header)

The peer-deserialization constructor (`SerialIter&`) builds the `STObject` from the template, then extracts and validates `sfSigningPubKey` in the initializer list. If the key type is not `secp256k1`, it throws immediately. This key-type check in the initializer list — before the body runs — ensures `signingPubKey_` is never in an invalid state and prevents any code from even reaching `isValid()` with a non-ECDSA key. Similarly, `nodeID_` is initialized with a `lookupNodeID` callable that resolves the ephemeral signing key to a stable master-key-derived NodeID (the manifest system).

The signing constructor closes the loop: after calling the user-provided fill callback `f(*this)`, it sets the `vfFullyCanonicalSig` flag, computes and stores the signature, marks itself trusted, and iterates over the `SOTemplate` to assert all required fields are present — a construction-time completeness check that catches omissions from the fill callback before the object leaves the constructor.