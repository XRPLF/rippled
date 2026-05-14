/** @file
 *  Defines `STValidation`, the wire-format object for a single ledger
 *  validation message in the XRPL consensus protocol.
 *
 *  Validators broadcast one of these objects each consensus round to signal
 *  agreement on a specific closed ledger. Peers deserialize inbound messages
 *  into `STValidation` instances, verify signatures, and count them toward
 *  quorum. The class therefore has two distinct construction paths: one for
 *  creation-and-signing by the local validator, one for deserialization of a
 *  peer's message. See the two constructors for details.
 *
 *  `STValidation` is owned via `std::shared_ptr` and wrapped by `RCLValidation`
 *  in the consensus machinery; that adapter provides the concept interface
 *  expected by the generic quorum-counting engine without coupling this class
 *  to consensus-specific logic.
 */
#pragma once

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Units.h>

#include <cstdint>
#include <optional>
#include <sstream>

namespace xrpl {

// --- Wire flag constants (stored in sfFlags; part of the signed payload) ---

/** Bit flag indicating a full (as opposed to a partial) validation.
 *
 *  A partial validation signals participation in the consensus round without
 *  fully endorsing a specific ledger hash. Validators set this flag when they
 *  have applied the consensus transaction set and validated the resulting
 *  ledger. Read via `isFull()`.
 */
constexpr std::uint32_t kVF_FULL_VALIDATION = 0x00000001;

/** Bit flag indicating that the DER-encoded signature uses the low-S canonical form.
 *
 *  XRPL requires low-S ECDSA signatures to prevent signature malleability.
 *  The signing constructor always sets this flag, and `isValid()` passes it
 *  to `verifyDigest()` to enforce canonicality on inbound messages. Because
 *  this value is stored in `sfFlags` inside the signed payload, it cannot be
 *  toggled without invalidating the signature.
 */
constexpr std::uint32_t kVF_FULLY_CANONICAL_SIG = 0x80000000;

/** Wire-format representation of a ledger validation message in XRPL consensus.
 *
 *  Inherits from `STObject` for typed-field serialization (the same system
 *  used by transactions and ledger entries) and from `CountedObject` for
 *  live-instance tracking in a long-running `rippled` process.
 *
 *  The class maintains two separate concepts that must not be conflated:
 *  - **Validity** (`valid_`): whether the cryptographic signature is correct.
 *    Lazily evaluated and cached on first call to `isValid()`.
 *  - **Trust** (`trusted_`): whether the issuing validator is on this node's
 *    current Unique Node List (UNL). Set via `setTrusted()`/`setUntrusted()`;
 *    can change at runtime as the UNL evolves.
 *
 *  @note Only `secp256k1` signing keys are accepted. Passing an `Ed25519`
 *      public key to either constructor throws at construction time.
 *  @see RCLValidation — the adapter that exposes this object to the generic
 *      consensus engine.
 */
class STValidation final : public STObject, public CountedObject<STValidation>
{
    bool trusted_ = false;

    // Determines the validity of the signature in this validation; unseated
    // optional if we haven't yet checked it, a boolean otherwise.
    mutable std::optional<bool> valid_;

    // The public key associated with the key used to sign this validation
    PublicKey const signingPubKey_;

    // The ID of the validator that issued this validation. For validators
    // that use manifests this will be derived from the master public key.
    NodeID const nodeID_;

    NetClock::time_point seenTime_;

public:
    /** Deserialize a validation received from a peer.
     *
     *  Parses the binary payload via `STObject`, then extracts the signing
     *  public key from `sfSigningPubKey`. The `lookupNodeID` callable
     *  translates the ephemeral signing key to the validator's stable master
     *  `NodeID` (which may differ when the validator has rotated its ephemeral
     *  key via the manifest mechanism).
     *
     *  @tparam LookupNodeID Callable with signature `NodeID(PublicKey const&)`.
     *      For manifest-based validators this should resolve to the master key's
     *      `NodeID`; for static-key validators it is typically
     *      `calcNodeID(pk)`.
     *  @param sit Iterator over the raw serialized validation bytes.
     *  @param lookupNodeID Invocable that maps the signing `PublicKey` to a
     *      stable `NodeID` used for UNL membership checks.
     *  @param checkSignature If `true`, verifies the signature immediately and
     *      throws on failure. Pass `false` to defer verification to the first
     *      call of `isValid()` (the pattern used by `PeerImp` to avoid
     *      synchronous cryptographic work on the peer-message path).
     *  @throws std::runtime_error if the serialized data is malformed, if the
     *      signing public key is absent or not a `secp256k1` key, or if
     *      `checkSignature` is `true` and the signature does not verify.
     *  @note After construction `seenTime_` is zero; callers must call
     *      `setSeen()` to record local receipt time before storing the object.
     */
    template <class LookupNodeID>
    STValidation(SerialIter& sit, LookupNodeID&& lookupNodeID, bool checkSignature);

    /** Construct, sign, and trust a new validation issued by the local node.
     *
     *  Sets mandatory bookkeeping fields (`sfSigningPubKey`, `sfSigningTime`),
     *  invokes the filler callback `f(*this)` so the caller can attach
     *  optional fields (ledger hash, consensus hash, fee votes, amendment
     *  bits, server version), then signs the result with `signDigest` and
     *  marks the object as trusted. The `kVF_FULLY_CANONICAL_SIG` flag is
     *  always set, enforcing low-S ECDSA on the produced signature.
     *
     *  After `f` returns a format-validation sweep checks that all
     *  `SoeRequired` fields are present; a missing required field is a
     *  programming error and triggers `logicError`.
     *
     *  `seenTime_` is initialized to `signTime`, making sign time and seen
     *  time identical for locally created validations.
     *
     *  @tparam F Callable with signature `void(STValidation&)`.
     *  @param signTime The time at which the validation is being signed;
     *      stored in `sfSigningTime` and used as the initial `seenTime_`.
     *  @param pk The validator's current ephemeral signing public key.
     *      Must be a `secp256k1` key; passing any other type calls
     *      `logicError`.
     *  @param sk The secret key matching `pk`, used to produce `sfSignature`.
     *  @param nodeID The stable master-key `NodeID` of this validator.
     *  @param f Callback invoked after mandatory fields are set but before
     *      signing. Use it to populate `sfLedgerHash`, `sfLedgerSequence`,
     *      `sfConsensusHash`, `sfFlags`, and any optional advisory fields.
     *  @note The resulting object is immediately marked as trusted and
     *      `valid_` is set to `true` without re-verifying, since the node
     *      just produced the signature.
     */
    template <typename F>
    STValidation(
        NetClock::time_point signTime,
        PublicKey const& pk,
        SecretKey const& sk,
        NodeID const& nodeID,
        F&& f);

    /** Return the hash of the ledger this validation endorses.
     *
     *  @return Value of the `sfLedgerHash` field.
     */
    uint256
    getLedgerHash() const;

    /** Return the hash of the consensus transaction set that produced the validated ledger.
     *
     *  @return Value of the `sfConsensusHash` field. Returns a zero hash if
     *      the field is absent (the field is optional in the schema).
     */
    uint256
    getConsensusHash() const;

    /** Return the time at which the validator claims to have signed this validation.
     *
     *  Reads `sfSigningTime` from the serialized payload; because that field
     *  is part of the signed content it cannot be forged without invalidating
     *  the signature. Note that this is the validator's own clock time and
     *  may differ from `getSeenTime()`, which is when the *local* node
     *  received the message.
     *
     *  @return The signing instant as a `NetClock::time_point`.
     */
    NetClock::time_point
    getSignTime() const;

    /** Return the local time at which this node received or created the validation.
     *
     *  For peer-sourced validations this is set via `setSeen()` after receipt.
     *  For self-issued validations the constructor initializes it to `signTime`.
     *  This value is never serialized or sent over the wire.
     *
     *  @return The local receipt time as a `NetClock::time_point`.
     */
    NetClock::time_point
    getSeenTime() const noexcept;

    /** Return the ephemeral public key that signed this validation.
     *
     *  May differ from the validator's stable master key when the validator
     *  has rotated its signing key via the manifest mechanism.
     *
     *  @return A reference to the immutable `signingPubKey_`.
     */
    PublicKey const&
    getSignerPublic() const noexcept;

    /** Return the stable master-key `NodeID` of the issuing validator.
     *
     *  For manifest-based validators this is derived from the master public
     *  key rather than the ephemeral signing key. It is the identity used
     *  for UNL membership checks and quorum counting.
     *
     *  @return A reference to the immutable `nodeID_`.
     */
    NodeID const&
    getNodeID() const noexcept;

    /** Verify the cryptographic signature, caching the result for future calls.
     *
     *  On the first call, verifies the ECDSA signature over `getSigningHash()`
     *  using `signingPubKey_`. The `kVF_FULLY_CANONICAL_SIG` flag is consulted
     *  to enforce low-S canonicality. The result is stored in `valid_` and
     *  returned on all subsequent calls without re-computing.
     *
     *  For self-issued validations the signing constructor pre-sets
     *  `valid_ = true`, so this method never performs cryptographic work.
     *
     *  @return `true` if the signature is valid; `false` otherwise.
     */
    bool
    isValid() const noexcept;

    /** Return whether this is a full (as opposed to partial) validation.
     *
     *  A full validation endorses a specific ledger hash. A partial validation
     *  only signals that the validator participated in the round.
     *
     *  @return `true` if the `kVF_FULL_VALIDATION` bit is set in `sfFlags`.
     */
    bool
    isFull() const noexcept;

    /** Return whether this validation is marked as trusted by the local node.
     *
     *  Trust reflects whether the issuing validator is on this node's current
     *  UNL and is independent of cryptographic validity. Self-issued
     *  validations are always trusted from construction.
     *
     *  @return The current value of the `trusted_` flag.
     */
    bool
    isTrusted() const noexcept;

    /** Compute the domain-separated hash that was (or will be) signed.
     *
     *  Prepends `HashPrefix::Validation` (`'V','A','L',0x00`) to the canonical
     *  serialization of all signed fields, then applies SHA-512-Half. The
     *  prefix prevents a validation hash from colliding with any other signed
     *  payload type (transactions, proposals, etc.).
     *
     *  @return The 256-bit signing digest.
     */
    uint256
    getSigningHash() const;

    /** Mark this validation as trusted.
     *
     *  Called when the issuing validator is confirmed to be on this node's
     *  current UNL. May be called multiple times; subsequent calls are no-ops.
     */
    void
    setTrusted();

    /** Mark this validation as untrusted.
     *
     *  Called when the issuing validator is removed from this node's current
     *  UNL, or when the validation is being re-evaluated. Does not affect the
     *  cryptographic `valid_` cache.
     */
    void
    setUntrusted();

    /** Record the local time at which this node received the validation.
     *
     *  Should be called immediately after constructing a peer-sourced
     *  validation, before the object is stored or forwarded. For self-issued
     *  validations the signing constructor sets this to `signTime`
     *  automatically.
     *
     *  @param s The local receipt time.
     */
    void
    setSeen(NetClock::time_point s);

    /** Serialize this validation to its complete binary wire format.
     *
     *  The returned bytes include all fields, including `sfSignature`, and are
     *  suitable for network transmission or deduplication hashing. To suppress
     *  relay of a duplicate message, callers typically hash this output with
     *  `sha512Half`.
     *
     *  @return A `Blob` containing the complete serialized validation.
     */
    Blob
    getSerialized() const;

    /** Return the raw DER-encoded ECDSA signature from the serialized payload.
     *
     *  @return Value of the `sfSignature` field as a `Blob`.
     */
    Blob
    getSignature() const;

    /** Produce a human-readable summary of this validation for logging.
     *
     *  Renders all major fields (ledger hash, consensus hash, sign/seen times,
     *  signer public key, node ID, validity, fullness, trust status, signing
     *  hash, and Base58-encoded public key) into a single-line string.
     *
     *  @return A diagnostic string; not suitable for machine parsing or
     *      network transmission.
     */
    std::string
    render() const
    {
        std::stringstream ss;
        ss << "validation: " << " ledger_hash: " << getLedgerHash()
           << " consensus_hash: " << getConsensusHash()
           << " sign_time: " << to_string(getSignTime())
           << " seen_time: " << to_string(getSeenTime())
           << " signer_public_key: " << getSignerPublic() << " node_id: " << getNodeID()
           << " is_valid: " << isValid() << " is_full: " << isFull()
           << " is_trusted: " << isTrusted() << " signing_hash: " << getSigningHash()
           << " base58: " << toBase58(TokenType::NodePublic, getSignerPublic());
        return ss.str();
    }

private:
    /** Return the field schema for `STValidation` objects.
     *
     *  Function-local static to guarantee that all `SField` singletons are
     *  initialized before the `SOTemplate` is constructed (C++ provides no
     *  cross-translation-unit initialization order for namespace-scope statics).
     */
    static SOTemplate const&
    validationFormat();

    STBase*
    copy(std::size_t n, void* buf) const override;
    STBase*
    move(std::size_t n, void* buf) override;

    friend class detail::STVar;
};

template <class LookupNodeID>
STValidation::STValidation(SerialIter& sit, LookupNodeID&& lookupNodeID, bool checkSignature)
    : STObject(validationFormat(), sit, sfValidation)
    , signingPubKey_([this]() {
        auto const spk = getFieldVL(sfSigningPubKey);

        if (publicKeyType(makeSlice(spk)) != KeyType::Secp256k1)
            Throw<std::runtime_error>("Invalid public key in validation");

        return PublicKey{makeSlice(spk)};
    }())
    , nodeID_(lookupNodeID(signingPubKey_))
{
    if (checkSignature && !isValid())
    {
        JLOG(debugLog().error()) << "Invalid signature in validation: "
                                 << getJson(JsonOptions::Values::None);
        Throw<std::runtime_error>("Invalid signature in validation");
    }

    XRPL_ASSERT(nodeID_.isNonZero(), "xrpl::STValidation::STValidation(SerialIter) : nonzero node");
}

template <typename F>
STValidation::STValidation(
    NetClock::time_point signTime,
    PublicKey const& pk,
    SecretKey const& sk,
    NodeID const& nodeID,
    F&& f)
    : STObject(validationFormat(), sfValidation)
    , signingPubKey_(pk)
    , nodeID_(nodeID)
    , seenTime_(signTime)
{
    XRPL_ASSERT(
        nodeID_.isNonZero(),
        "xrpl::STValidation::STValidation(PublicKey, SecretKey) : nonzero "
        "node");

    // First, set our own public key:
    if (publicKeyType(pk) != KeyType::Secp256k1)
        logicError("We can only use secp256k1 keys for signing validations");

    setFieldVL(sfSigningPubKey, pk.slice());
    setFieldU32(sfSigningTime, signTime.time_since_epoch().count());

    // Perform additional initialization
    f(*this);

    // Finally, sign the validation and mark it as trusted:
    setFlag(kVF_FULLY_CANONICAL_SIG);
    setFieldVL(sfSignature, signDigest(pk, sk, getSigningHash()));
    setTrusted();

    // Check to ensure that all required fields are present.
    for (auto const& e : validationFormat())
    {
        if (e.style() == SoeRequired && !isFieldPresent(e.sField()))
            logicError("Required field '" + e.sField().getName() + "' missing from validation.");
    }

    // We just signed this, so it should be valid.
    valid_ = true;
}

inline PublicKey const&
STValidation::getSignerPublic() const noexcept
{
    return signingPubKey_;
}

inline NodeID const&
STValidation::getNodeID() const noexcept
{
    return nodeID_;
}

inline bool
STValidation::isTrusted() const noexcept
{
    return trusted_;
}

inline void
STValidation::setTrusted()
{
    trusted_ = true;
}

inline void
STValidation::setUntrusted()
{
    trusted_ = false;
}

inline void
STValidation::setSeen(NetClock::time_point s)
{
    seenTime_ = s;
}

}  // namespace xrpl
