/** @file
 *  Type system foundation for XRPL's cross-chain bridge attestation protocol.
 *
 *  Witness servers attest to cross-chain transfer events by constructing and
 *  signing objects from the `Attestations` namespace, then submitting them to
 *  the destination chain.  The destination chain stores a stripped, signature-
 *  free form directly in ledger objects.  Two parallel hierarchies serve these
 *  roles:
 *
 *  - `Attestations::AttestationClaim` / `AttestationCreateAccount` — full
 *    witness-submitted proofs including raw signature bytes.
 *  - `XChainClaimAttestation` / `XChainCreateAccountAttestation` — on-ledger
 *    storage forms retaining only the signer key identity and event fields.
 *
 *  @see STXChainBridge
 */

#pragma once

#include <xrpl/basics/Buffer.h>
#include <xrpl/basics/Expected.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/STXChainBridge.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/TER.h>

#include <boost/container/flat_set.hpp>
#include <boost/container/vector.hpp>

#include <cstddef>
#include <utility>
#include <vector>

namespace xrpl {

namespace Attestations {

/** Common fields shared by all witness-server attestation types.
 *
 *  Holds the signer identity, raw signature, source account, amount, reward
 *  account, and transfer direction for a single cross-chain event attestation.
 *  Subclasses add the event-specific identifier (`claimID` or `createCount`)
 *  and supply the virtual `message()` implementation that determines the exact
 *  bytes that were signed.
 *
 *  @note This type carries the raw `signature` bytes and is only used in the
 *      witness-submission path.  On-ledger storage uses the signature-free
 *      `XChainClaimAttestation` / `XChainCreateAccountAttestation` types.
 */
struct AttestationBase
{
    /** Account ID associated with the signer's public key. */
    AccountID attestationSignerAccount;

    /** Public key used by the witness server to sign the attestation. */
    PublicKey publicKey;

    /** Raw cryptographic signature over the canonical message bytes. */
    Buffer signature;

    /** Account on the sending chain that originated the cross-chain transfer. */
    AccountID sendingAccount;

    /** Amount transferred on the sending chain. */
    STAmount sendingAmount;

    /** Destination-chain account that collects this witness's reward share. */
    AccountID rewardAccount;

    /** `true` if the transfer was initiated on the locking chain. */
    bool wasLockingChainSend;

    /** Construct from individual field values supplied by the witness server.
     *
     *  @param attestationSignerAccount Account ID for the signer's public key.
     *  @param publicKey Witness server's public key.
     *  @param signature Pre-computed signature over the canonical message.
     *  @param sendingAccount Source account on the sending chain.
     *  @param sendingAmount Amount transferred on the sending chain.
     *  @param rewardAccount Destination-chain account receiving the reward share.
     *  @param wasLockingChainSend `true` if the transfer originated on the
     *      locking chain.
     */
    explicit AttestationBase(
        AccountID attestationSignerAccount,
        PublicKey const& publicKey,
        Buffer signature,
        AccountID const& sendingAccount,
        STAmount sendingAmount,
        AccountID const& rewardAccount,
        bool wasLockingChainSend);

    AttestationBase(AttestationBase const&) = default;

    virtual ~AttestationBase() = default;

    AttestationBase&
    operator=(AttestationBase const&) = default;

    /** Cryptographically verify the witness signature against the stored fields.
     *
     *  Re-derives the canonical message bytes via the virtual `message()` call,
     *  then checks them against `publicKey` and `signature`.
     *
     *  @param bridge The bridge this attestation relates to; included in the
     *      signed payload to scope the proof to a specific bridge instance.
     *  @return `true` if the signature is valid.
     */
    [[nodiscard]] bool
    verify(STXChainBridge const& bridge) const;

protected:
    /** Deserialize from a ledger `STObject`. */
    explicit AttestationBase(STObject const& o);

    /** Deserialize from a JSON value.
     *
     *  @throws std::runtime_error if any required field is missing or has the
     *      wrong type.
     */
    explicit AttestationBase(json::Value const& v);

    /** Compare all fields of two `AttestationBase` instances, including signer
     *  identity and raw signature bytes.
     *
     *  Used by subclass `operator==` to test whether two attestations are
     *  identical in every respect.  Compare with `sameEventHelper`, which
     *  intentionally excludes signer fields.
     *
     *  @param lhs Left-hand attestation.
     *  @param rhs Right-hand attestation.
     *  @return `true` if every base field matches.
     */
    [[nodiscard]] static bool
    equalHelper(AttestationBase const& lhs, AttestationBase const& rhs);

    /** Check whether two attestations witness the same cross-chain event,
     *  ignoring signer identity.
     *
     *  Two attestations from different witnesses for the same transfer share
     *  identical `sendingAccount`, `sendingAmount`, and `wasLockingChainSend`
     *  values.  Signer fields are excluded so that distinct witnesses
     *  corroborating the same event can be counted toward quorum.
     *
     *  @param lhs Left-hand attestation.
     *  @param rhs Right-hand attestation.
     *  @return `true` if the event-identity fields match.
     */
    [[nodiscard]] static bool
    sameEventHelper(AttestationBase const& lhs, AttestationBase const& rhs);

    /** Populate `o` with the base attestation fields shared by both claim types.
     *
     *  Subclass `toSTObject()` implementations call this before setting their
     *  own type-specific fields.
     *
     *  @param o The `STObject` to populate.
     */
    void
    addHelper(STObject& o) const;

private:
    /** Produce the canonical byte sequence that the witness signed.
     *
     *  Pure virtual: each subclass encodes a different set of fields to prevent
     *  the base class from silently producing an incomplete message.  The static
     *  overloads on each subclass expose the same logic for callers that want to
     *  verify a signature without constructing a full attestation object.
     *
     *  @param bridge Bridge context bound into the signed payload.
     *  @return Serialized bytes suitable for passing to `xrpl::verify()`.
     */
    [[nodiscard]] virtual std::vector<std::uint8_t>
    message(STXChainBridge const& bridge) const = 0;
};

/** Full witness-server attestation for a regular cross-chain transfer.
 *
 *  Extends `AttestationBase` with the claim-specific `claimID` (a monotonic
 *  counter on the bridge that prevents replay) and an optional destination
 *  account override on the receiving chain.
 *
 *  Two constructors support the two call sites: one accepts a pre-computed
 *  `Buffer signature` (used on the destination chain when verifying incoming
 *  attestations), and one accepts a `SecretKey` and signs inline (used by
 *  witness servers and test harnesses generating attestations from scratch).
 */
struct AttestationClaim : AttestationBase
{
    /** Monotonic bridge counter that scopes this claim to a specific transfer,
     *  preventing replay across different events on the same bridge. */
    std::uint64_t claimID;

    /** Optional destination account on the receiving chain.  Witnesses may
     *  attest to the same transfer with different `dst` opinions; the
     *  three-state `AttestationMatch` distinguishes these cases during quorum
     *  collection. */
    std::optional<AccountID> dst;

    /** Construct from individual field values with a pre-computed signature.
     *
     *  Used on the destination chain when receiving and verifying attestations
     *  submitted by witness servers.
     *
     *  @param attestationSignerAccount Account ID for the signer's public key.
     *  @param publicKey Witness server's public key.
     *  @param signature Pre-computed signature over the canonical message.
     *  @param sendingAccount Source account on the sending chain.
     *  @param sendingAmount Amount transferred on the sending chain.
     *  @param rewardAccount Destination-chain account receiving the reward share.
     *  @param wasLockingChainSend `true` if the transfer originated on the
     *      locking chain.
     *  @param claimId Bridge claim counter for this transfer.
     *  @param dst Optional destination account override on the receiving chain.
     */
    explicit AttestationClaim(
        AccountID attestationSignerAccount,
        PublicKey const& publicKey,
        Buffer signature,
        AccountID const& sendingAccount,
        STAmount const& sendingAmount,
        AccountID const& rewardAccount,
        bool wasLockingChainSend,
        std::uint64_t claimId,
        std::optional<AccountID> const& dst);

    /** Construct and immediately sign using `secretKey`.
     *
     *  Derives the canonical message bytes from all supplied fields and
     *  `bridge`, then signs them.  Intended for witness servers and test
     *  harnesses that generate attestations from scratch.
     *
     *  @param bridge Bridge context included in the signed payload; binds the
     *      attestation to a specific bridge instance.
     *  @param attestationSignerAccount Account ID for the signer's public key.
     *  @param publicKey Witness server's public key.
     *  @param secretKey Signing key; the resulting signature is stored in
     *      `AttestationBase::signature`.
     *  @param sendingAccount Source account on the sending chain.
     *  @param sendingAmount Amount transferred on the sending chain.
     *  @param rewardAccount Destination-chain account receiving the reward share.
     *  @param wasLockingChainSend `true` if the transfer originated on the
     *      locking chain.
     *  @param claimId Bridge claim counter for this transfer.
     *  @param dst Optional destination account override on the receiving chain.
     */
    explicit AttestationClaim(
        STXChainBridge const& bridge,
        AccountID attestationSignerAccount,
        PublicKey const& publicKey,
        SecretKey const& secretKey,
        AccountID const& sendingAccount,
        STAmount const& sendingAmount,
        AccountID const& rewardAccount,
        bool wasLockingChainSend,
        std::uint64_t claimId,
        std::optional<AccountID> const& dst);

    /** Deserialize from a ledger `STObject`. */
    explicit AttestationClaim(STObject const& o);

    /** Deserialize from a JSON value.
     *
     *  @throws std::runtime_error if any required field is missing or has the
     *      wrong type.
     */
    explicit AttestationClaim(json::Value const& v);

    /** Serialize this attestation to an `STObject` for inclusion in a
     *  transaction or `STArray`.
     *
     *  @return An inner object tagged
     *      `sfXChainClaimAttestationCollectionElement` containing all claim
     *      attestation fields.
     */
    [[nodiscard]] STObject
    toSTObject() const;

    /** Check whether `rhs` witnesses the same cross-chain claim event,
     *  ignoring signer identity fields.
     *
     *  Both the base event fields and the claim-specific `claimID` and `dst`
     *  must agree.  Two attestations satisfying this condition came from
     *  different witnesses for the same transfer and each count toward quorum.
     *
     *  @param rhs The attestation to compare against.
     *  @return `true` if both attestations describe the same claim event.
     */
    [[nodiscard]] bool
    sameEvent(AttestationClaim const& rhs) const;

    /** Produce the canonical bytes that a witness signs for a claim attestation.
     *
     *  Fields are serialized in `SField` sort order so that independent
     *  implementations (e.g., Python witness servers) produce byte-for-byte
     *  identical output.  The `bridge` is embedded in the payload, scoping
     *  the proof to a specific bridge instance so it cannot be replayed on
     *  another.
     *
     *  @param bridge Bridge context scoping the proof.
     *  @param sendingAccount Source account on the sending chain.
     *  @param sendingAmount Amount transferred on the sending chain.
     *  @param rewardAccount Destination-chain account receiving the reward share.
     *  @param wasLockingChainSend `true` if the transfer originated on the
     *      locking chain.
     *  @param claimID Monotonic counter from the bridge that prevents replay.
     *  @param dst Optional destination override on the issuing chain.
     *  @return Serialized bytes suitable for signing or verification.
     */
    [[nodiscard]] static std::vector<std::uint8_t>
    message(
        STXChainBridge const& bridge,
        AccountID const& sendingAccount,
        STAmount const& sendingAmount,
        AccountID const& rewardAccount,
        bool wasLockingChainSend,
        std::uint64_t claimID,
        std::optional<AccountID> const& dst);

    /** Check that `sendingAmount` is a legal network amount.
     *
     *  @return `true` if the amount is valid for wire transmission.
     */
    [[nodiscard]] bool
    validAmounts() const;

private:
    [[nodiscard]] std::vector<std::uint8_t>
    message(STXChainBridge const& bridge) const override;

    friend bool
    operator==(AttestationClaim const& lhs, AttestationClaim const& rhs);
};

/** Comparator that orders `AttestationClaim` objects by their `claimID`.
 *
 *  Used to sort or binary-search a collection of claim attestations by the
 *  bridge's monotonic sequence counter.
 */
struct CmpByClaimID
{
    bool
    operator()(AttestationClaim const& lhs, AttestationClaim const& rhs) const
    {
        return lhs.claimID < rhs.claimID;
    }
};

/** Full witness-server attestation for a cross-chain account-creation transfer.
 *
 *  Extends `AttestationBase` with the account-creation–specific `createCount`
 *  (the value of `XChainAccountCreateCount` on the sending-chain bridge at
 *  event time), the destination account to create, and the total size of the
 *  witness reward pool.  Unlike `AttestationClaim`, the destination is not
 *  optional — account-creation attestations always specify the account to
 *  create on the receiving chain.
 *
 *  As with `AttestationClaim`, two constructors support pre-computed signatures
 *  (verification path) and inline signing (witness server / test harness path).
 */
struct AttestationCreateAccount : AttestationBase
{
    /** Value of `XChainAccountCreateCount` on the sending-chain bridge when
     *  the account-creation transaction was executed.  Scopes this attestation
     *  to a specific event and prevents replay. */
    std::uint64_t createCount;

    /** Account to create on the destination chain. */
    AccountID toCreate;

    /** Total amount of the witness reward pool for this event. */
    STAmount rewardAmount;

    /** Deserialize from a ledger `STObject`. */
    explicit AttestationCreateAccount(STObject const& o);

    /** Deserialize from a JSON value.
     *
     *  @throws std::runtime_error if any required field is missing or has the
     *      wrong type.
     */
    explicit AttestationCreateAccount(json::Value const& v);

    /** Construct from individual field values with a pre-computed signature.
     *
     *  Used on the destination chain when receiving and verifying attestations
     *  submitted by witness servers.
     *
     *  @param attestationSignerAccount Account ID for the signer's public key.
     *  @param publicKey Witness server's public key.
     *  @param signature Pre-computed signature over the canonical message.
     *  @param sendingAccount Source account on the sending chain.
     *  @param sendingAmount Amount transferred on the sending chain.
     *  @param rewardAmount Total witness reward pool for this event.
     *  @param rewardAccount Destination-chain account receiving the reward share.
     *  @param wasLockingChainSend `true` if the transfer originated on the
     *      locking chain.
     *  @param createCount Bridge account-creation counter for this event.
     *  @param toCreate Account to create on the destination chain.
     */
    explicit AttestationCreateAccount(
        AccountID attestationSignerAccount,
        PublicKey const& publicKey,
        Buffer signature,
        AccountID const& sendingAccount,
        STAmount const& sendingAmount,
        STAmount rewardAmount,
        AccountID const& rewardAccount,
        bool wasLockingChainSend,
        std::uint64_t createCount,
        AccountID const& toCreate);

    /** Construct and immediately sign using `secretKey`.
     *
     *  Derives the canonical message bytes from all supplied fields and
     *  `bridge`, then signs them.  Intended for witness servers and test
     *  harnesses that generate account-creation attestations from scratch.
     *
     *  @param bridge Bridge context included in the signed payload; binds the
     *      attestation to a specific bridge instance.
     *  @param attestationSignerAccount Account ID for the signer's public key.
     *  @param publicKey Witness server's public key.
     *  @param secretKey Signing key; the resulting signature is stored in
     *      `AttestationBase::signature`.
     *  @param sendingAccount Source account on the sending chain.
     *  @param sendingAmount Amount transferred on the sending chain.
     *  @param rewardAmount Total witness reward pool for this event.
     *  @param rewardAccount Destination-chain account receiving the reward share.
     *  @param wasLockingChainSend `true` if the transfer originated on the
     *      locking chain.
     *  @param createCount Bridge account-creation counter for this event.
     *  @param toCreate Account to create on the destination chain.
     */
    explicit AttestationCreateAccount(
        STXChainBridge const& bridge,
        AccountID attestationSignerAccount,
        PublicKey const& publicKey,
        SecretKey const& secretKey,
        AccountID const& sendingAccount,
        STAmount const& sendingAmount,
        STAmount const& rewardAmount,
        AccountID const& rewardAccount,
        bool wasLockingChainSend,
        std::uint64_t createCount,
        AccountID const& toCreate);

    /** Serialize this attestation to an `STObject` for inclusion in a
     *  transaction or `STArray`.
     *
     *  @return An inner object tagged
     *      `sfXChainCreateAccountAttestationCollectionElement` containing all
     *      account-creation attestation fields.
     */
    [[nodiscard]] STObject
    toSTObject() const;

    /** Check whether `rhs` witnesses the same cross-chain account-creation
     *  event, ignoring signer identity fields.
     *
     *  The base event fields plus `createCount`, `toCreate`, and `rewardAmount`
     *  must all agree.  Two attestations satisfying this condition came from
     *  different witnesses for the same transfer and each count toward quorum.
     *
     *  @param rhs The attestation to compare against.
     *  @return `true` if both attestations describe the same account-creation
     *      event.
     */
    [[nodiscard]] bool
    sameEvent(AttestationCreateAccount const& rhs) const;

    friend bool
    operator==(AttestationCreateAccount const& lhs, AttestationCreateAccount const& rhs);

    /** Produce the canonical bytes that a witness signs for an account-creation
     *  attestation.
     *
     *  Fields are serialized in `SField` sort order so that independent
     *  implementations (e.g., Python witness servers) produce byte-for-byte
     *  identical output.  The `bridge` is embedded in the payload, scoping
     *  the proof to a specific bridge instance.
     *
     *  @param bridge Bridge context scoping the proof.
     *  @param sendingAccount Source account on the sending chain.
     *  @param sendingAmount Amount transferred on the sending chain.
     *  @param rewardAmount Total size of the witness reward pool for this event.
     *  @param rewardAccount Destination-chain account receiving this witness's
     *      reward share.
     *  @param wasLockingChainSend `true` if the transfer originated on the
     *      locking chain.
     *  @param createCount Value of `XChainAccountCreateCount` on the sending-
     *      chain bridge at the time of the event; prevents replay.
     *  @param dst Account to create on the destination chain.
     *  @return Serialized bytes suitable for signing or verification.
     */
    [[nodiscard]] static std::vector<std::uint8_t>
    message(
        STXChainBridge const& bridge,
        AccountID const& sendingAccount,
        STAmount const& sendingAmount,
        STAmount const& rewardAmount,
        AccountID const& rewardAccount,
        bool wasLockingChainSend,
        std::uint64_t createCount,
        AccountID const& dst);

    /** Check that both `sendingAmount` and `rewardAmount` are legal network
     *  amounts.
     *
     *  @return `true` if both amounts are valid for wire transmission.
     */
    [[nodiscard]] bool
    validAmounts() const;

private:
    [[nodiscard]] std::vector<std::uint8_t>
    message(STXChainBridge const& bridge) const override;
};

/** Comparator that orders `AttestationCreateAccount` objects by `createCount`.
 *
 *  Used to sort or binary-search a collection of account-creation attestations
 *  by the bridge's monotonic account-creation sequence counter.
 */
struct CmpByCreateCount
{
    bool
    operator()(AttestationCreateAccount const& lhs, AttestationCreateAccount const& rhs) const
    {
        return lhs.createCount < rhs.createCount;
    }
};

};  // namespace Attestations

/** Three-state result of comparing a stored ledger attestation against incoming
 *  fields.
 *
 *  The extra granularity beyond a simple boolean is required because
 *  `XChainClaim` transactions allow the user to specify their own destination
 *  account, so the system must distinguish "everything matches" from "same
 *  transfer, different destination":
 *
 *  - `XChainAddClaimAttestation` requires `Match` — witnesses must agree on
 *    the destination.
 *  - `XChainClaim` (user-specified destination) also accepts `MatchExceptDst`.
 */
enum class AttestationMatch {
    /** At least one non-destination field differs; the attestations describe
     *  different events or amounts. */
    NonDstMismatch,
    /** All fields agree except the optional destination account. */
    MatchExceptDst,
    /** All fields, including the destination, agree completely. */
    Match
};

/** Ledger-stored form of a regular cross-chain transfer attestation.
 *
 *  This is the signature-free projection of `Attestations::AttestationClaim`
 *  that lives inside `XChainOwnedClaimID` ledger entries.  The raw `Buffer
 *  signature` and full `sendingAccount` are not persisted; only the signer's
 *  `keyAccount` and `publicKey`, the event amount, reward account, transfer
 *  direction, and optional destination are retained.
 *
 *  The `TSignedAttestation` typedef links this type back to its full
 *  counterpart.  The converting constructor from `TSignedAttestation` is the
 *  canonical way to project a submitted attestation into its on-ledger form.
 *
 *  @see AttestationMatch
 */
struct XChainClaimAttestation
{
    /** Full witness-submitted attestation type that this ledger form is derived
     *  from. */
    using TSignedAttestation = Attestations::AttestationClaim;

    /** `SField` naming the `STArray` that holds these entries in ledger
     *  objects (`sfXChainClaimAttestations`). */
    static SField const& arrayFieldName;

    /** Account ID corresponding to the signer's public key. */
    AccountID keyAccount;

    /** Witness server's public key. */
    PublicKey publicKey;

    /** Amount transferred on the sending chain. */
    STAmount amount;

    /** Destination-chain account that collects this witness's reward share. */
    AccountID rewardAccount;

    /** `true` if the transfer was initiated on the locking chain. */
    bool wasLockingChainSend;

    /** Optional destination account on the receiving chain. */
    std::optional<AccountID> dst;

    /** Fields used to compare a stored attestation against an incoming one
     *  during quorum matching.
     *
     *  Constructed from either a full `TSignedAttestation` or explicit values;
     *  holds only the event-describing fields that `match()` inspects.
     */
    struct MatchFields
    {
        /** Amount transferred on the sending chain. */
        STAmount amount;
        /** `true` if the transfer was initiated on the locking chain. */
        bool wasLockingChainSend;
        /** Optional destination account on the receiving chain. */
        std::optional<AccountID> dst;

        /** Project from a full signing-side attestation.
         *
         *  @param att Full witness-submitted attestation to extract from.
         */
        MatchFields(TSignedAttestation const& att);

        /** Construct from explicit values.
         *
         *  @param a Amount transferred on the sending chain.
         *  @param b `true` if the transfer originated on the locking chain.
         *  @param d Optional destination account.
         */
        MatchFields(STAmount a, bool b, std::optional<AccountID> const& d)
            : amount{std::move(a)}, wasLockingChainSend{b}, dst{d}
        {
        }
    };

    /** Construct from explicit field values.
     *
     *  @param keyAccount Account ID corresponding to the signer's public key.
     *  @param publicKey Witness server's public key.
     *  @param amount Amount transferred on the sending chain.
     *  @param rewardAccount Destination-chain account receiving the reward share.
     *  @param wasLockingChainSend `true` if the transfer originated on the
     *      locking chain.
     *  @param dst Optional destination account on the receiving chain.
     */
    explicit XChainClaimAttestation(
        AccountID const& keyAccount,
        PublicKey const& publicKey,
        STAmount const& amount,
        AccountID const& rewardAccount,
        bool wasLockingChainSend,
        std::optional<AccountID> const& dst);

    /** Construct from `STAccount`-wrapped field values, unwrapping the
     *  `AccountID` values.
     *
     *  Convenience overload used when deserializing from an `STObject` whose
     *  account fields are already typed as `STAccount`.
     *
     *  @param keyAccount Wrapped account ID for the signer's public key.
     *  @param publicKey Witness server's public key.
     *  @param amount Amount transferred on the sending chain.
     *  @param rewardAccount Wrapped destination-chain reward account.
     *  @param wasLockingChainSend `true` if the transfer originated on the
     *      locking chain.
     *  @param dst Optional wrapped destination account.
     */
    explicit XChainClaimAttestation(
        STAccount const& keyAccount,
        PublicKey const& publicKey,
        STAmount const& amount,
        STAccount const& rewardAccount,
        bool wasLockingChainSend,
        std::optional<STAccount> const& dst);

    /** Project a full `AttestationClaim` into its ledger-storage form,
     *  dropping the raw signature.
     *
     *  @param claimAtt The full witness-submitted attestation to convert.
     */
    explicit XChainClaimAttestation(TSignedAttestation const& claimAtt);

    /** Deserialize from a ledger `STObject`. */
    explicit XChainClaimAttestation(STObject const& o);

    /** Deserialize from a JSON value.
     *
     *  @throws std::runtime_error if any required field is missing or has the
     *      wrong type.
     */
    explicit XChainClaimAttestation(json::Value const& v);

    /** Determine how closely this stored attestation matches the supplied fields.
     *
     *  Returns `Match` when all fields agree, `MatchExceptDst` when only the
     *  destination differs, and `NonDstMismatch` when a core event field
     *  (amount or direction) does not agree.
     *
     *  @param rhs The fields from the incoming attestation or claim request.
     *  @return `Match`, `MatchExceptDst`, or `NonDstMismatch`.
     */
    [[nodiscard]] AttestationMatch
    match(MatchFields const& rhs) const;

    /** Serialize this ledger-stored attestation to an `STObject`.
     *
     *  @return An inner object tagged `sfXChainClaimProofSig` containing the
     *      ledger-side claim attestation fields (no raw signature).
     */
    [[nodiscard]] STObject
    toSTObject() const;

    friend bool
    operator==(XChainClaimAttestation const& lhs, XChainClaimAttestation const& rhs);
};

/** Ledger-stored form of a cross-chain account-creation attestation.
 *
 *  This is the signature-free projection of
 *  `Attestations::AttestationCreateAccount` that lives inside
 *  `XChainOwnedCreateAccountClaimID` ledger entries.  Unlike the claim
 *  variant, the destination account (`dst`) is mandatory — account-creation
 *  attestations always specify the account to create on the receiving chain.
 *
 *  The `TSignedAttestation` typedef links this type back to its full
 *  counterpart.  The converting constructor from `TSignedAttestation` is the
 *  canonical way to project a submitted attestation into its on-ledger form.
 *
 *  @see AttestationMatch
 */
struct XChainCreateAccountAttestation
{
    /** Full witness-submitted attestation type that this ledger form is derived
     *  from. */
    using TSignedAttestation = Attestations::AttestationCreateAccount;

    /** `SField` naming the `STArray` that holds these entries in ledger
     *  objects (`sfXChainCreateAccountAttestations`). */
    static SField const& arrayFieldName;

    /** Account ID corresponding to the signer's public key. */
    AccountID keyAccount;

    /** Witness server's public key. */
    PublicKey publicKey;

    /** Amount transferred on the sending chain. */
    STAmount amount;

    /** Total witness reward pool for this account-creation event. */
    STAmount rewardAmount;

    /** Destination-chain account that collects this witness's reward share. */
    AccountID rewardAccount;

    /** `true` if the transfer was initiated on the locking chain. */
    bool wasLockingChainSend;

    /** Account to create on the destination chain. */
    AccountID dst;

    /** Fields used to compare a stored attestation against an incoming one
     *  during quorum matching.
     *
     *  For account-creation attestations, `amount`, `rewardAmount`, and
     *  `wasLockingChainSend` are all checked before the destination is
     *  compared.
     */
    struct MatchFields
    {
        /** Amount transferred on the sending chain. */
        STAmount amount;
        /** Total witness reward pool for this event. */
        STAmount rewardAmount;
        /** `true` if the transfer was initiated on the locking chain. */
        bool wasLockingChainSend;
        /** Account to create on the destination chain. */
        AccountID dst;

        /** Project from a full signing-side attestation.
         *
         *  @param att Full witness-submitted attestation to extract from.
         */
        MatchFields(TSignedAttestation const& att);
    };

    /** Construct from explicit field values.
     *
     *  @param keyAccount Account ID corresponding to the signer's public key.
     *  @param publicKey Witness server's public key.
     *  @param amount Amount transferred on the sending chain.
     *  @param rewardAmount Total witness reward pool for this event.
     *  @param rewardAccount Destination-chain account receiving the reward share.
     *  @param wasLockingChainSend `true` if the transfer originated on the
     *      locking chain.
     *  @param dst Account to create on the destination chain.
     */
    explicit XChainCreateAccountAttestation(
        AccountID const& keyAccount,
        PublicKey const& publicKey,
        STAmount const& amount,
        STAmount const& rewardAmount,
        AccountID const& rewardAccount,
        bool wasLockingChainSend,
        AccountID const& dst);

    /** Project a full `AttestationCreateAccount` into its ledger-storage form,
     *  dropping the raw signature.
     *
     *  @param claimAtt The full witness-submitted attestation to convert.
     */
    explicit XChainCreateAccountAttestation(TSignedAttestation const& claimAtt);

    /** Deserialize from a ledger `STObject`. */
    explicit XChainCreateAccountAttestation(STObject const& o);

    /** Deserialize from a JSON value.
     *
     *  @throws std::runtime_error if any required field is missing or has the
     *      wrong type.
     */
    explicit XChainCreateAccountAttestation(json::Value const& v);

    /** Serialize this ledger-stored attestation to an `STObject`.
     *
     *  @return An inner object tagged `sfXChainCreateAccountProofSig`
     *      containing the ledger-side account-creation attestation fields
     *      (no raw signature).
     */
    [[nodiscard]] STObject
    toSTObject() const;

    /** Determine how closely this stored attestation matches the supplied fields.
     *
     *  Returns `Match` when all fields agree, `MatchExceptDst` when only the
     *  destination account differs, and `NonDstMismatch` when a core event
     *  field (amount, reward amount, or direction) does not agree.
     *
     *  @param rhs The fields from the incoming attestation or claim request.
     *  @return `Match`, `MatchExceptDst`, or `NonDstMismatch`.
     */
    [[nodiscard]] AttestationMatch
    match(MatchFields const& rhs) const;

    friend bool
    operator==(
        XChainCreateAccountAttestation const& lhs,
        XChainCreateAccountAttestation const& rhs);
};

/** Container for a collection of witness-server attestations for a single
 *  bridge event.
 *
 *  Wraps a `std::vector<TAttestation>` and adds serialization to/from
 *  `STArray` and `Json::Value`.  At most `kMAX_ATTESTATIONS` (256) entries
 *  are accepted at either parse path; this cap prevents memory exhaustion from
 *  crafted oversized arrays while remaining far above any realistic quorum
 *  size.
 *
 *  The destructor is `protected` to prevent slicing: the two concrete
 *  subclasses (`XChainClaimAttestations` and `XChainCreateAccountAttestations`)
 *  add no virtual methods, so accidental deletion through a base pointer would
 *  silently be incorrect.
 *
 *  @tparam TAttestation Either `XChainClaimAttestation` or
 *      `XChainCreateAccountAttestation`.
 */
template <class TAttestation>
class XChainAttestationsBase
{
public:
    /** Underlying storage type for the attestation collection. */
    using AttCollection = std::vector<TAttestation>;

private:
    /** Maximum number of attestations accepted from either `STArray` or JSON
     *  input.  Far above practical quorum sizes; guards against memory
     *  exhaustion from maliciously crafted messages. */
    static constexpr std::uint32_t kMAX_ATTESTATIONS = 256;
    AttCollection attestations_;

protected:
    /** Protected destructor prevents slicing through a base-class pointer. */
    ~XChainAttestationsBase() = default;

public:
    XChainAttestationsBase() = default;
    XChainAttestationsBase(XChainAttestationsBase const& rhs) = default;
    XChainAttestationsBase&
    operator=(XChainAttestationsBase const& rhs) = default;

    /** Construct from a pre-built attestation vector, taking ownership.
     *
     *  @param sigs Attestation collection to move into this container.
     */
    explicit XChainAttestationsBase(AttCollection&& sigs);

    /** Deserialize from a JSON value containing an `"attestations"` array.
     *
     *  @throws std::runtime_error if `v` is not a JSON object, if the array
     *      exceeds `kMAX_ATTESTATIONS`, or if any element fails to
     *      deserialize.
     */
    explicit XChainAttestationsBase(json::Value const& v);

    /** Deserialize from an `STArray` read out of a ledger object.
     *
     *  @throws std::runtime_error if `arr` contains more than
     *      `kMAX_ATTESTATIONS` elements.
     */
    explicit XChainAttestationsBase(STArray const& arr);

    /** Serialize the collection to an `STArray` for storage in a ledger object.
     *
     *  @return An `STArray` tagged with `TAttestation::arrayFieldName` whose
     *      elements are the `STObject` representations of each attestation.
     */
    [[nodiscard]] STArray
    toSTArray() const;

    /** @return Const iterator to the first attestation. */
    [[nodiscard]] typename AttCollection::const_iterator
    begin() const;

    /** @return Const iterator past the last attestation. */
    [[nodiscard]] typename AttCollection::const_iterator
    end() const;

    /** @return Mutable iterator to the first attestation. */
    typename AttCollection::iterator
    begin();

    /** @return Mutable iterator past the last attestation. */
    typename AttCollection::iterator
    end();

    /** Remove all attestations satisfying predicate `f`.
     *
     *  @tparam F Unary predicate type; called with `TAttestation const&`.
     *  @param f Predicate; returns `true` for attestations to remove.
     *  @return Number of attestations removed.
     */
    template <class F>
    std::size_t
    eraseIf(F&& f);

    /** @return Number of attestations in the collection. */
    [[nodiscard]] std::size_t
    size() const;

    /** @return `true` if the collection contains no attestations. */
    [[nodiscard]] bool
    empty() const;

    /** @return Read-only reference to the underlying attestation vector. */
    [[nodiscard]] AttCollection const&
    attestations() const;

    /** Append an attestation to the collection.
     *
     *  @tparam T Must be convertible to `TAttestation`.
     *  @param att Attestation to append (forwarded).
     */
    template <class T>
    void
    emplaceBack(T&& att);
};

/** Compare two attestation collections for equality.
 *
 *  @return `true` if both containers hold identical attestations in the same
 *      order.
 */
template <class TAttestation>
[[nodiscard]] inline bool
operator==(
    XChainAttestationsBase<TAttestation> const& lhs,
    XChainAttestationsBase<TAttestation> const& rhs)
{
    return lhs.attestations() == rhs.attestations();
}

template <class TAttestation>
inline typename XChainAttestationsBase<TAttestation>::AttCollection const&
XChainAttestationsBase<TAttestation>::attestations() const
{
    return attestations_;
};

template <class TAttestation>
template <class T>
inline void
XChainAttestationsBase<TAttestation>::emplaceBack(T&& att)
{
    attestations_.emplace_back(std::forward<T>(att));
};

template <class TAttestation>
template <class F>
inline std::size_t
XChainAttestationsBase<TAttestation>::eraseIf(F&& f)
{
    return std::erase_if(attestations_, std::forward<F>(f));
}

template <class TAttestation>
inline std::size_t
XChainAttestationsBase<TAttestation>::size() const
{
    return attestations_.size();
}

template <class TAttestation>
inline bool
XChainAttestationsBase<TAttestation>::empty() const
{
    return attestations_.empty();
}

/** Typed container for ledger-stored claim attestations.
 *
 *  Thin named wrapper around `XChainAttestationsBase<XChainClaimAttestation>`
 *  that provides a distinct type for the type system without adding behavior.
 *  All constructors are inherited from the base.
 */
class XChainClaimAttestations final : public XChainAttestationsBase<XChainClaimAttestation>
{
    using TBase = XChainAttestationsBase<XChainClaimAttestation>;
    using TBase::TBase;
};

/** Typed container for ledger-stored account-creation attestations.
 *
 *  Thin named wrapper around
 *  `XChainAttestationsBase<XChainCreateAccountAttestation>` that provides a
 *  distinct type for the type system without adding behavior.  All constructors
 *  are inherited from the base.
 */
class XChainCreateAccountAttestations final
    : public XChainAttestationsBase<XChainCreateAccountAttestation>
{
    using TBase = XChainAttestationsBase<XChainCreateAccountAttestation>;
    using TBase::TBase;
};

}  // namespace xrpl
