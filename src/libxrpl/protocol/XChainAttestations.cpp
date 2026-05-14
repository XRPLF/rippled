/** @file
 *  Implements the attestation type system for XRPL's cross-chain bridge
 *  protocol.
 *
 *  Two parallel hierarchies exist: `Attestations::AttestationClaim` and
 *  `Attestations::AttestationCreateAccount` carry full witness-submitted
 *  proofs including raw signatures; `XChainClaimAttestation` and
 *  `XChainCreateAccountAttestation` are the stripped, ledger-stored variants
 *  that retain only the key identity and event fields.  Conversion from the
 *  signing side to the storage side is a one-step projection in the
 *  `TSignedAttestation` constructors.
 *
 *  Template bodies for `XChainAttestationsBase<TAttestation>` are kept here
 *  (not in the header) and explicitly instantiated at the bottom of the file
 *  for the two concrete types, limiting compile-time overhead.
 */
#include <xrpl/protocol/XChainAttestations.h>

#include <xrpl/basics/Buffer.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/contract.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAccount.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/json_get_or_throw.h>
#include <xrpl/protocol/jss.h>

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

namespace xrpl {
namespace Attestations {

/** Construct from individual field values supplied by the witness server. */
AttestationBase::AttestationBase(
    AccountID attestationSignerAccount,
    PublicKey const& publicKey,
    Buffer signature,
    AccountID const& sendingAccount,
    STAmount sendingAmount,
    AccountID const& rewardAccount,
    bool wasLockingChainSend)
    : attestationSignerAccount{attestationSignerAccount}
    , publicKey{publicKey}
    , signature{std::move(signature)}
    , sendingAccount{sendingAccount}
    , sendingAmount{std::move(sendingAmount)}
    , rewardAccount{rewardAccount}
    , wasLockingChainSend{wasLockingChainSend}
{
}

/** Compare all fields of two `AttestationBase` instances, including signer
 *  identity and raw signature bytes.
 *
 *  Used by subclass `operator==` to test whether two attestations are
 *  identical in every respect.  Compare with `sameEventHelper`, which
 *  intentionally excludes the signer fields.
 *
 *  @param lhs Left-hand attestation.
 *  @param rhs Right-hand attestation.
 *  @return `true` if every base field matches.
 */
bool
AttestationBase::equalHelper(AttestationBase const& lhs, AttestationBase const& rhs)
{
    return std::tie(
               lhs.attestationSignerAccount,
               lhs.publicKey,
               lhs.signature,
               lhs.sendingAccount,
               lhs.sendingAmount,
               lhs.rewardAccount,
               lhs.wasLockingChainSend) ==
        std::tie(
               rhs.attestationSignerAccount,
               rhs.publicKey,
               rhs.signature,
               rhs.sendingAccount,
               rhs.sendingAmount,
               rhs.rewardAccount,
               rhs.wasLockingChainSend);
}

/** Check whether two attestations witness the same cross-chain event,
 *  ignoring signer identity.
 *
 *  Two attestations from different witnesses for the same transfer share
 *  identical `sendingAccount`, `sendingAmount`, and `wasLockingChainSend`
 *  values.  Signer fields (`attestationSignerAccount`, `publicKey`,
 *  `signature`) are deliberately excluded so that distinct witnesses
 *  corroborating the same event can be aggregated toward quorum.
 *
 *  @param lhs Left-hand attestation.
 *  @param rhs Right-hand attestation.
 *  @return `true` if the event-identity fields match.
 */
bool
AttestationBase::sameEventHelper(AttestationBase const& lhs, AttestationBase const& rhs)
{
    return std::tie(lhs.sendingAccount, lhs.sendingAmount, lhs.wasLockingChainSend) ==
        std::tie(rhs.sendingAccount, rhs.sendingAmount, rhs.wasLockingChainSend);
}

/** Cryptographically verify the witness signature against the stored fields.
 *
 *  Re-derives the canonical message bytes via the virtual `message()` call,
 *  then checks them against `publicKey` and `signature`.  Called during
 *  transaction preflight (`attestationPreflight` in `XChainBridge.cpp`); a
 *  failure here returns `temXCHAIN_BAD_PROOF` before any ledger state is
 *  modified.
 *
 *  @param bridge The bridge the attestation relates to; included in the
 *      signed payload to scope the proof to a specific bridge instance.
 *  @return `true` if the signature is valid.
 */
bool
AttestationBase::verify(STXChainBridge const& bridge) const
{
    std::vector<std::uint8_t> const msg = message(bridge);
    return xrpl::verify(publicKey, makeSlice(msg), signature);
}

/** Deserialize from a ledger `STObject`. */
AttestationBase::AttestationBase(STObject const& o)
    : attestationSignerAccount{o[sfAttestationSignerAccount]}
    , publicKey{o[sfPublicKey]}
    , signature{o[sfSignature]}
    , sendingAccount{o[sfAccount]}
    , sendingAmount{o[sfAmount]}
    , rewardAccount{o[sfAttestationRewardAccount]}
    , wasLockingChainSend{bool(o[sfWasLockingChainSend])}
{
}

/** Deserialize from a JSON value.
 *
 *  @throws std::runtime_error if any required field is missing or has the
 *      wrong type (via `json::getOrThrow`).
 */
AttestationBase::AttestationBase(json::Value const& v)
    : attestationSignerAccount{json::getOrThrow<AccountID>(v, sfAttestationSignerAccount)}
    , publicKey{json::getOrThrow<PublicKey>(v, sfPublicKey)}
    , signature{json::getOrThrow<Buffer>(v, sfSignature)}
    , sendingAccount{json::getOrThrow<AccountID>(v, sfAccount)}
    , sendingAmount{json::getOrThrow<STAmount>(v, sfAmount)}
    , rewardAccount{json::getOrThrow<AccountID>(v, sfAttestationRewardAccount)}
    , wasLockingChainSend{json::getOrThrow<bool>(v, sfWasLockingChainSend)}
{
}

/** Populate `o` with the base attestation fields shared by both claim types.
 *
 *  Subclass `toSTObject()` implementations call this before setting their
 *  own type-specific fields.
 *
 *  @param o The `STObject` to populate.
 */
void
AttestationBase::addHelper(STObject& o) const
{
    o[sfAttestationSignerAccount] = attestationSignerAccount;
    o[sfPublicKey] = publicKey;
    o[sfSignature] = signature;
    o[sfAmount] = sendingAmount;
    o[sfAccount] = sendingAccount;
    o[sfAttestationRewardAccount] = rewardAccount;
    o[sfWasLockingChainSend] = wasLockingChainSend;
}

/** Construct from individual fields with a pre-computed signature. */
AttestationClaim::AttestationClaim(
    AccountID attestationSignerAccount,
    PublicKey const& publicKey,
    Buffer signature,
    AccountID const& sendingAccount,
    STAmount const& sendingAmount,
    AccountID const& rewardAccount,
    bool wasLockingChainSend,
    std::uint64_t claimId,
    std::optional<AccountID> const& dst)
    : AttestationBase(
          attestationSignerAccount,
          publicKey,
          std::move(signature),
          sendingAccount,
          sendingAmount,
          rewardAccount,
          wasLockingChainSend)
    , claimID{claimId}
    , dst{dst}
{
}

/** Construct and immediately sign.
 *
 *  Derives the canonical message bytes from the supplied fields and `bridge`,
 *  then signs them with `secretKey`.  Intended for witness servers and test
 *  harnesses that generate attestations from scratch.
 *
 *  @param bridge Bridge context included in the signed payload.
 *  @param secretKey Signing key; the resulting signature is stored in
 *      `AttestationBase::signature`.
 */
AttestationClaim::AttestationClaim(
    STXChainBridge const& bridge,
    AccountID attestationSignerAccount,
    PublicKey const& publicKey,
    SecretKey const& secretKey,
    AccountID const& sendingAccount,
    STAmount const& sendingAmount,
    AccountID const& rewardAccount,
    bool wasLockingChainSend,
    std::uint64_t claimId,
    std::optional<AccountID> const& dst)
    : AttestationClaim{
          attestationSignerAccount,
          publicKey,
          Buffer{},
          sendingAccount,
          sendingAmount,
          rewardAccount,
          wasLockingChainSend,
          claimId,
          dst}
{
    auto const toSign = message(bridge);
    signature = sign(publicKey, secretKey, makeSlice(toSign));
}

/** Deserialize from a ledger `STObject`. */
AttestationClaim::AttestationClaim(STObject const& o)
    : AttestationBase(o), claimID{o[sfXChainClaimID]}, dst{o[~sfDestination]}
{
}

/** Deserialize from a JSON value.
 *
 *  @throws std::runtime_error if any required field is missing or has the
 *      wrong type (via `json::getOrThrow`).
 */
AttestationClaim::AttestationClaim(json::Value const& v)
    : AttestationBase{v}, claimID{json::getOrThrow<std::uint64_t>(v, sfXChainClaimID)}
{
    if (v.isMember(sfDestination.getJsonName()))
        dst = json::getOrThrow<AccountID>(v, sfDestination);
}

/** Serialize this attestation to an `STObject` for inclusion in a transaction
 *  or `STArray`.
 *
 *  @return An inner object tagged `sfXChainClaimAttestationCollectionElement`
 *      containing all claim attestation fields.
 */
STObject
AttestationClaim::toSTObject() const
{
    STObject o = STObject::makeInnerObject(sfXChainClaimAttestationCollectionElement);
    addHelper(o);
    o[sfXChainClaimID] = claimID;
    if (dst)
        o[sfDestination] = *dst;
    return o;
}

/** Produce the canonical bytes that a witness signs for a claim attestation.
 *
 *  Builds an `STObject{sfGeneric}` populated with all claim fields and
 *  serializes it via `Serializer::add()`.  Fields are inserted in `SField`
 *  sort order to ensure independent serializers (e.g., Python witness
 *  implementations) produce byte-for-byte identical output.
 *
 *  @param bridge  Bridge context scoping the proof.
 *  @param sendingAccount  Source account on the sending chain.
 *  @param sendingAmount   Amount transferred on the sending chain.
 *  @param rewardAccount   Destination-chain account receiving the reward share.
 *  @param wasLockingChainSend  `true` if the transfer originated on the
 *      locking chain.
 *  @param claimID  Monotonic counter from the bridge that prevents replay.
 *  @param dst  Optional destination override on the issuing chain.
 *  @return Serialized bytes suitable for signing or verification.
 */
std::vector<std::uint8_t>
AttestationClaim::message(
    STXChainBridge const& bridge,
    AccountID const& sendingAccount,
    STAmount const& sendingAmount,
    AccountID const& rewardAccount,
    bool wasLockingChainSend,
    std::uint64_t claimID,
    std::optional<AccountID> const& dst)
{
    STObject o{kSF_GENERIC};
    // Serialize in SField order to make python serializers easier to write
    o[sfXChainClaimID] = claimID;
    o[sfAmount] = sendingAmount;
    if (dst)
        o[sfDestination] = *dst;
    o[sfOtherChainSource] = sendingAccount;
    o[sfAttestationRewardAccount] = rewardAccount;
    o[sfWasLockingChainSend] = wasLockingChainSend ? 1 : 0;
    o[sfXChainBridge] = bridge;

    Serializer s;
    o.add(s);

    return std::move(s.modData());
}

/** Instance overload delegating to the static form using stored field values.
 *
 *  Called by `AttestationBase::verify()` to regenerate the signed payload
 *  for signature checking.
 *
 *  @param bridge Bridge context scoping the proof.
 *  @return Serialized bytes identical to those that were originally signed.
 */
std::vector<std::uint8_t>
AttestationClaim::message(STXChainBridge const& bridge) const
{
    return AttestationClaim::message(
        bridge, sendingAccount, sendingAmount, rewardAccount, wasLockingChainSend, claimID, dst);
}

/** Check that `sendingAmount` is a legal network amount.
 *
 *  @return `true` if the amount is valid for wire transmission.
 */
bool
AttestationClaim::validAmounts() const
{
    return isLegalNet(sendingAmount);
}

/** Check whether `rhs` witnesses the same cross-chain claim event, ignoring
 *  signer identity fields.
 *
 *  Two attestations for the same event may differ only in their
 *  `attestationSignerAccount`, `publicKey`, and `signature` (i.e., they
 *  come from different witnesses).  Both the base event fields and the
 *  claim-specific `claimID` and `dst` must agree.
 *
 *  @param rhs The attestation to compare against.
 *  @return `true` if both attestations describe the same claim event.
 */
bool
AttestationClaim::sameEvent(AttestationClaim const& rhs) const
{
    return AttestationClaim::sameEventHelper(*this, rhs) &&
        tie(claimID, dst) == tie(rhs.claimID, rhs.dst);
}

/** Test full equality of two `AttestationClaim` values, including signer
 *  identity and raw signature.
 */
bool
operator==(AttestationClaim const& lhs, AttestationClaim const& rhs)
{
    return AttestationClaim::equalHelper(lhs, rhs) &&
        tie(lhs.claimID, lhs.dst) == tie(rhs.claimID, rhs.dst);
}

/** Deserialize from a ledger `STObject`. */
AttestationCreateAccount::AttestationCreateAccount(STObject const& o)
    : AttestationBase(o)
    , createCount{o[sfXChainAccountCreateCount]}
    , toCreate{o[sfDestination]}
    , rewardAmount{o[sfSignatureReward]}
{
}

/** Deserialize from a JSON value.
 *
 *  @throws std::runtime_error if any required field is missing or has the
 *      wrong type (via `json::getOrThrow`).
 */
AttestationCreateAccount::AttestationCreateAccount(json::Value const& v)
    : AttestationBase{v}
    , createCount{json::getOrThrow<std::uint64_t>(v, sfXChainAccountCreateCount)}
    , toCreate{json::getOrThrow<AccountID>(v, sfDestination)}
    , rewardAmount{json::getOrThrow<STAmount>(v, sfSignatureReward)}
{
}

/** Construct from individual fields with a pre-computed signature. */
AttestationCreateAccount::AttestationCreateAccount(
    AccountID attestationSignerAccount,
    PublicKey const& publicKey,
    Buffer signature,
    AccountID const& sendingAccount,
    STAmount const& sendingAmount,
    STAmount rewardAmount,
    AccountID const& rewardAccount,
    bool wasLockingChainSend,
    std::uint64_t createCount,
    AccountID const& toCreate)
    : AttestationBase(
          attestationSignerAccount,
          publicKey,
          std::move(signature),
          sendingAccount,
          sendingAmount,
          rewardAccount,
          wasLockingChainSend)
    , createCount{createCount}
    , toCreate{toCreate}
    , rewardAmount{std::move(rewardAmount)}
{
}

/** Construct and immediately sign.
 *
 *  Derives the canonical message bytes from the supplied fields and `bridge`,
 *  then signs them with `secretKey`.  Intended for witness servers and test
 *  harnesses that generate account-creation attestations from scratch.
 *
 *  @param bridge Bridge context included in the signed payload.
 *  @param secretKey Signing key; the resulting signature is stored in
 *      `AttestationBase::signature`.
 */
AttestationCreateAccount::AttestationCreateAccount(
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
    AccountID const& toCreate)
    : AttestationCreateAccount{
          attestationSignerAccount,
          publicKey,
          Buffer{},
          sendingAccount,
          sendingAmount,
          rewardAmount,
          rewardAccount,
          wasLockingChainSend,
          createCount,
          toCreate}
{
    auto const toSign = message(bridge);
    signature = sign(publicKey, secretKey, makeSlice(toSign));
}

/** Serialize this attestation to an `STObject` for inclusion in a transaction
 *  or `STArray`.
 *
 *  @return An inner object tagged
 *      `sfXChainCreateAccountAttestationCollectionElement` containing all
 *      account-creation attestation fields.
 */
STObject
AttestationCreateAccount::toSTObject() const
{
    STObject o = STObject::makeInnerObject(sfXChainCreateAccountAttestationCollectionElement);
    addHelper(o);

    o[sfXChainAccountCreateCount] = createCount;
    o[sfDestination] = toCreate;
    o[sfSignatureReward] = rewardAmount;

    return o;
}

/** Produce the canonical bytes that a witness signs for an account-creation
 *  attestation.
 *
 *  Builds an `STObject{sfGeneric}` with all account-creation fields and
 *  serializes it via `Serializer::add()`.  Fields are inserted in `SField`
 *  sort order to ensure cross-language serializers produce byte-for-byte
 *  identical output.
 *
 *  @param bridge  Bridge context scoping the proof.
 *  @param sendingAccount  Source account on the sending chain.
 *  @param sendingAmount   Amount transferred on the sending chain.
 *  @param rewardAmount    Total size of the witness-reward pool for this event.
 *  @param rewardAccount   Destination-chain account receiving this witness's
 *      reward share.
 *  @param wasLockingChainSend  `true` if the transfer originated on the
 *      locking chain.
 *  @param createCount  Value of `XChainAccountCreateCount` on the sending-
 *      chain bridge at the time of the event; prevents replay.
 *  @param dst  Account to create on the destination chain.
 *  @return Serialized bytes suitable for signing or verification.
 */
std::vector<std::uint8_t>
AttestationCreateAccount::message(
    STXChainBridge const& bridge,
    AccountID const& sendingAccount,
    STAmount const& sendingAmount,
    STAmount const& rewardAmount,
    AccountID const& rewardAccount,
    bool wasLockingChainSend,
    std::uint64_t createCount,
    AccountID const& dst)
{
    STObject o{kSF_GENERIC};
    // Serialize in SField order to make python serializers easier to write
    o[sfXChainAccountCreateCount] = createCount;
    o[sfAmount] = sendingAmount;
    o[sfSignatureReward] = rewardAmount;
    o[sfDestination] = dst;
    o[sfOtherChainSource] = sendingAccount;
    o[sfAttestationRewardAccount] = rewardAccount;
    o[sfWasLockingChainSend] = wasLockingChainSend ? 1 : 0;
    o[sfXChainBridge] = bridge;

    Serializer s;
    o.add(s);

    return std::move(s.modData());
}

/** Instance overload delegating to the static form using stored field values.
 *
 *  Called by `AttestationBase::verify()` to regenerate the signed payload
 *  for signature checking.
 *
 *  @param bridge Bridge context scoping the proof.
 *  @return Serialized bytes identical to those that were originally signed.
 */
std::vector<std::uint8_t>
AttestationCreateAccount::message(STXChainBridge const& bridge) const
{
    return AttestationCreateAccount::message(
        bridge,
        sendingAccount,
        sendingAmount,
        rewardAmount,
        rewardAccount,
        wasLockingChainSend,
        createCount,
        toCreate);
}

/** Check that both `sendingAmount` and `rewardAmount` are legal network amounts.
 *
 *  @return `true` if both amounts are valid for wire transmission.
 */
bool
AttestationCreateAccount::validAmounts() const
{
    return isLegalNet(rewardAmount) && isLegalNet(sendingAmount);
}

/** Check whether `rhs` witnesses the same cross-chain account-creation event,
 *  ignoring signer identity fields.
 *
 *  The base event fields plus the create-specific `createCount`, `toCreate`,
 *  and `rewardAmount` must all agree.
 *
 *  @param rhs The attestation to compare against.
 *  @return `true` if both attestations describe the same account-creation event.
 */
bool
AttestationCreateAccount::sameEvent(AttestationCreateAccount const& rhs) const
{
    return AttestationCreateAccount::sameEventHelper(*this, rhs) &&
        std::tie(createCount, toCreate, rewardAmount) ==
        std::tie(rhs.createCount, rhs.toCreate, rhs.rewardAmount);
}

/** Test full equality of two `AttestationCreateAccount` values, including
 *  signer identity and raw signature.
 */
bool
operator==(AttestationCreateAccount const& lhs, AttestationCreateAccount const& rhs)
{
    return AttestationCreateAccount::equalHelper(lhs, rhs) &&
        std::tie(lhs.createCount, lhs.toCreate, lhs.rewardAmount) ==
        std::tie(rhs.createCount, rhs.toCreate, rhs.rewardAmount);
}

}  // namespace Attestations

/** `SField` used to name the `STArray` containing claim attestations in
 *  ledger objects.
 */
SField const& XChainClaimAttestation::arrayFieldName{sfXChainClaimAttestations};

/** `SField` used to name the `STArray` containing account-creation
 *  attestations in ledger objects.
 */
SField const& XChainCreateAccountAttestation::arrayFieldName{sfXChainCreateAccountAttestations};

/** Construct from individual field values, used when promoting a ledger-stored
 *  entry or creating a test fixture.
 */
XChainClaimAttestation::XChainClaimAttestation(
    AccountID const& keyAccount,
    PublicKey const& publicKey,
    STAmount const& amount,
    AccountID const& rewardAccount,
    bool wasLockingChainSend,
    std::optional<AccountID> const& dst)
    : keyAccount(keyAccount)
    , publicKey(publicKey)
    , amount(sfAmount, amount)
    , rewardAccount(rewardAccount)
    , wasLockingChainSend(wasLockingChainSend)
    , dst(dst)
{
}

/** Construct from `STAccount` fields, unwrapping the `AccountID` values.
 *
 *  Convenience overload used when deserializing from an `STObject` whose
 *  account fields are already wrapped in `STAccount`.
 */
XChainClaimAttestation::XChainClaimAttestation(
    STAccount const& keyAccount,
    PublicKey const& publicKey,
    STAmount const& amount,
    STAccount const& rewardAccount,
    bool wasLockingChainSend,
    std::optional<STAccount> const& dst)
    : XChainClaimAttestation{
          keyAccount.value(),
          publicKey,
          amount,
          rewardAccount.value(),
          wasLockingChainSend,
          dst ? std::optional<AccountID>{dst->value()} : std::nullopt}
{
}

/** Deserialize from a ledger `STObject`. */
XChainClaimAttestation::XChainClaimAttestation(STObject const& o)
    : XChainClaimAttestation{
          o[sfAttestationSignerAccount],
          PublicKey{o[sfPublicKey]},
          o[sfAmount],
          o[sfAttestationRewardAccount],
          o[sfWasLockingChainSend] != 0,
          o[~sfDestination]} {};

/** Deserialize from a JSON value.
 *
 *  @throws std::runtime_error if any required field is missing or has the
 *      wrong type (via `json::getOrThrow`).
 */
XChainClaimAttestation::XChainClaimAttestation(json::Value const& v)
    : XChainClaimAttestation{
          json::getOrThrow<AccountID>(v, sfAttestationSignerAccount),
          json::getOrThrow<PublicKey>(v, sfPublicKey),
          json::getOrThrow<STAmount>(v, sfAmount),
          json::getOrThrow<AccountID>(v, sfAttestationRewardAccount),
          json::getOrThrow<bool>(v, sfWasLockingChainSend),
          std::nullopt}
{
    if (v.isMember(sfDestination.getJsonName()))
        dst = json::getOrThrow<AccountID>(v, sfDestination);
};

/** Project a signing-side `AttestationClaim` into its ledger-storage form,
 *  dropping the raw signature.
 *
 *  The raw `signature` and signer-account fields are stripped; only the
 *  event-identity and key-account fields that need to persist on-ledger are
 *  retained.
 *
 *  @param claimAtt The full witness-submitted attestation to convert.
 */
XChainClaimAttestation::XChainClaimAttestation(
    XChainClaimAttestation::TSignedAttestation const& claimAtt)
    : XChainClaimAttestation{
          claimAtt.attestationSignerAccount,
          claimAtt.publicKey,
          claimAtt.sendingAmount,
          claimAtt.rewardAccount,
          claimAtt.wasLockingChainSend,
          claimAtt.dst}
{
}

/** Serialize this ledger-stored attestation to an `STObject`.
 *
 *  @return An inner object tagged `sfXChainClaimProofSig` containing the
 *      ledger-side claim attestation fields (no raw signature).
 */
STObject
XChainClaimAttestation::toSTObject() const
{
    STObject o = STObject::makeInnerObject(sfXChainClaimProofSig);
    o[sfAttestationSignerAccount] = STAccount{sfAttestationSignerAccount, keyAccount};
    o[sfPublicKey] = publicKey;
    o[sfAmount] = STAmount{sfAmount, amount};
    o[sfAttestationRewardAccount] = STAccount{sfAttestationRewardAccount, rewardAccount};
    o[sfWasLockingChainSend] = wasLockingChainSend;
    if (dst)
        o[sfDestination] = STAccount{sfDestination, *dst};
    return o;
}

/** Test equality of two ledger-stored claim attestations. */
bool
operator==(XChainClaimAttestation const& lhs, XChainClaimAttestation const& rhs)
{
    return std::tie(
               lhs.keyAccount,
               lhs.publicKey,
               lhs.amount,
               lhs.rewardAccount,
               lhs.wasLockingChainSend,
               lhs.dst) ==
        std::tie(
               rhs.keyAccount,
               rhs.publicKey,
               rhs.amount,
               rhs.rewardAccount,
               rhs.wasLockingChainSend,
               rhs.dst);
}

/** Construct `MatchFields` from the signing-side representation, projecting
 *  only the fields used for quorum matching.
 *
 *  @param att The full witness-submitted attestation to extract match fields from.
 */
XChainClaimAttestation::MatchFields::MatchFields(
    XChainClaimAttestation::TSignedAttestation const& att)
    : amount{att.sendingAmount}, wasLockingChainSend{att.wasLockingChainSend}, dst{att.dst}
{
}

/** Determine how closely this stored attestation matches the supplied fields.
 *
 *  The three-state result lets callers distinguish a fully matching
 *  attestation from one that matches except for the destination:
 *
 *  - `XChainAddClaimAttestation` transactions require `Match` — all witnesses
 *    must agree on the destination.
 *  - `XChainClaim` transactions specify their own destination, so
 *    `MatchExceptDst` is also accepted (the user overrides the dst).
 *
 *  @param rhs The fields from the incoming attestation or claim request.
 *  @return `Match`, `MatchExceptDst`, or `NonDstMismatch`.
 */
AttestationMatch
XChainClaimAttestation::match(XChainClaimAttestation::MatchFields const& rhs) const
{
    if (std::tie(amount, wasLockingChainSend) != std::tie(rhs.amount, rhs.wasLockingChainSend))
        return AttestationMatch::NonDstMismatch;
    if (dst != rhs.dst)
        return AttestationMatch::MatchExceptDst;
    return AttestationMatch::Match;
}

//------------------------------------------------------------------------------

/** Construct from individual field values. */
XChainCreateAccountAttestation::XChainCreateAccountAttestation(
    AccountID const& keyAccount,
    PublicKey const& publicKey,
    STAmount const& amount,
    STAmount const& rewardAmount,
    AccountID const& rewardAccount,
    bool wasLockingChainSend,
    AccountID const& dst)
    : keyAccount(keyAccount)
    , publicKey(publicKey)
    , amount(sfAmount, amount)
    , rewardAmount(sfSignatureReward, rewardAmount)
    , rewardAccount(rewardAccount)
    , wasLockingChainSend(wasLockingChainSend)
    , dst(dst)
{
}

/** Deserialize from a ledger `STObject`. */
XChainCreateAccountAttestation::XChainCreateAccountAttestation(STObject const& o)
    : XChainCreateAccountAttestation{
          o[sfAttestationSignerAccount],
          PublicKey{o[sfPublicKey]},
          o[sfAmount],
          o[sfSignatureReward],
          o[sfAttestationRewardAccount],
          o[sfWasLockingChainSend] != 0,
          o[sfDestination]} {};

/** Deserialize from a JSON value.
 *
 *  @throws std::runtime_error if any required field is missing or has the
 *      wrong type (via `json::getOrThrow`).
 */
XChainCreateAccountAttestation ::XChainCreateAccountAttestation(json::Value const& v)
    : XChainCreateAccountAttestation{
          json::getOrThrow<AccountID>(v, sfAttestationSignerAccount),
          json::getOrThrow<PublicKey>(v, sfPublicKey),
          json::getOrThrow<STAmount>(v, sfAmount),
          json::getOrThrow<STAmount>(v, sfSignatureReward),
          json::getOrThrow<AccountID>(v, sfAttestationRewardAccount),
          json::getOrThrow<bool>(v, sfWasLockingChainSend),
          json::getOrThrow<AccountID>(v, sfDestination)}
{
}

/** Project a signing-side `AttestationCreateAccount` into its ledger-storage
 *  form, dropping the raw signature.
 *
 *  @param createAtt The full witness-submitted attestation to convert.
 */
XChainCreateAccountAttestation::XChainCreateAccountAttestation(
    XChainCreateAccountAttestation::TSignedAttestation const& createAtt)
    : XChainCreateAccountAttestation{
          createAtt.attestationSignerAccount,
          createAtt.publicKey,
          createAtt.sendingAmount,
          createAtt.rewardAmount,
          createAtt.rewardAccount,
          createAtt.wasLockingChainSend,
          createAtt.toCreate}
{
}

/** Serialize this ledger-stored attestation to an `STObject`.
 *
 *  @return An inner object tagged `sfXChainCreateAccountProofSig` containing
 *      the ledger-side account-creation attestation fields (no raw signature).
 */
STObject
XChainCreateAccountAttestation::toSTObject() const
{
    STObject o = STObject::makeInnerObject(sfXChainCreateAccountProofSig);

    o[sfAttestationSignerAccount] = STAccount{sfAttestationSignerAccount, keyAccount};
    o[sfPublicKey] = publicKey;
    o[sfAmount] = STAmount{sfAmount, amount};
    o[sfSignatureReward] = STAmount{sfSignatureReward, rewardAmount};
    o[sfAttestationRewardAccount] = STAccount{sfAttestationRewardAccount, rewardAccount};
    o[sfWasLockingChainSend] = wasLockingChainSend;
    o[sfDestination] = STAccount{sfDestination, dst};

    return o;
}

/** Construct `MatchFields` from the signing-side representation, projecting
 *  only the fields used for quorum matching.
 *
 *  @param att The full witness-submitted attestation to extract match fields from.
 */
XChainCreateAccountAttestation::MatchFields::MatchFields(
    XChainCreateAccountAttestation::TSignedAttestation const& att)
    : amount{att.sendingAmount}
    , rewardAmount(att.rewardAmount)
    , wasLockingChainSend{att.wasLockingChainSend}
    , dst{att.toCreate}
{
}

/** Determine how closely this stored attestation matches the supplied fields.
 *
 *  Returns the same three-state `AttestationMatch` as the claim variant;
 *  for account-creation, `amount`, `rewardAmount`, and `wasLockingChainSend`
 *  must all agree before the destination is considered.
 *
 *  @param rhs The fields from the incoming attestation or claim request.
 *  @return `Match`, `MatchExceptDst`, or `NonDstMismatch`.
 */
AttestationMatch
XChainCreateAccountAttestation::match(XChainCreateAccountAttestation::MatchFields const& rhs) const
{
    if (std::tie(amount, rewardAmount, wasLockingChainSend) !=
        std::tie(rhs.amount, rhs.rewardAmount, rhs.wasLockingChainSend))
        return AttestationMatch::NonDstMismatch;
    if (dst != rhs.dst)
        return AttestationMatch::MatchExceptDst;
    return AttestationMatch::Match;
}

/** Test equality of two ledger-stored account-creation attestations. */
bool
operator==(XChainCreateAccountAttestation const& lhs, XChainCreateAccountAttestation const& rhs)
{
    return std::tie(
               lhs.keyAccount,
               lhs.publicKey,
               lhs.amount,
               lhs.rewardAmount,
               lhs.rewardAccount,
               lhs.wasLockingChainSend,
               lhs.dst) ==
        std::tie(
               rhs.keyAccount,
               rhs.publicKey,
               rhs.amount,
               rhs.rewardAmount,
               rhs.rewardAccount,
               rhs.wasLockingChainSend,
               rhs.dst);
}

//------------------------------------------------------------------------------

/** Construct from a pre-built collection.
 *
 *  @param atts Attestation vector to take ownership of.
 */
template <class TAttestation>
XChainAttestationsBase<TAttestation>::XChainAttestationsBase(
    XChainAttestationsBase<TAttestation>::AttCollection&& atts)
    : attestations_{std::move(atts)}
{
}

template <class TAttestation>
typename XChainAttestationsBase<TAttestation>::AttCollection::const_iterator
XChainAttestationsBase<TAttestation>::begin() const
{
    return attestations_.begin();
}

template <class TAttestation>
typename XChainAttestationsBase<TAttestation>::AttCollection::const_iterator
XChainAttestationsBase<TAttestation>::end() const
{
    return attestations_.end();
}

template <class TAttestation>
typename XChainAttestationsBase<TAttestation>::AttCollection::iterator
XChainAttestationsBase<TAttestation>::begin()
{
    return attestations_.begin();
}

template <class TAttestation>
typename XChainAttestationsBase<TAttestation>::AttCollection::iterator
XChainAttestationsBase<TAttestation>::end()
{
    return attestations_.end();
}

/** Deserialize from a JSON value containing an `"attestations"` array.
 *
 *  @throws std::runtime_error if `v` is not a JSON object, if the array
 *      exceeds `kMAX_ATTESTATIONS` (256), or if any element fails to
 *      deserialize.
 */
template <class TAttestation>
XChainAttestationsBase<TAttestation>::XChainAttestationsBase(json::Value const& v)
{
    if (!v.isObject())
    {
        Throw<std::runtime_error>(
            "XChainAttestationsBase can only be specified with an 'object' "
            "Json value");
    }

    attestations_ = [&] {
        auto const jAtts = v[jss::attestations];

        if (jAtts.size() > kMAX_ATTESTATIONS)
            Throw<std::runtime_error>("XChainAttestationsBase exceeded max number of attestations");

        std::vector<TAttestation> r;
        r.reserve(jAtts.size());
        for (auto const& a : jAtts)
            r.emplace_back(a);
        return r;
    }();
}

/** Deserialize from an `STArray` read out of a ledger object.
 *
 *  @throws std::runtime_error if `arr` contains more than `kMAX_ATTESTATIONS`
 *      (256) elements.
 */
template <class TAttestation>
XChainAttestationsBase<TAttestation>::XChainAttestationsBase(STArray const& arr)
{
    if (arr.size() > kMAX_ATTESTATIONS)
        Throw<std::runtime_error>("XChainAttestationsBase exceeded max number of attestations");

    attestations_.reserve(arr.size());
    for (auto const& o : arr)
        attestations_.emplace_back(o);
}

/** Serialize the collection to an `STArray` for storage in a ledger object.
 *
 *  @return An `STArray` tagged with `TAttestation::arrayFieldName` whose
 *      elements are the `STObject` representations of each attestation.
 */
template <class TAttestation>
STArray
XChainAttestationsBase<TAttestation>::toSTArray() const
{
    STArray r{TAttestation::arrayFieldName, attestations_.size()};
    for (auto const& e : attestations_)
        r.emplaceBack(e.toSTObject());
    return r;
}

// Explicit instantiations keep template bodies in this translation unit,
// avoiding recompilation of the full implementation in every consumer.
template class XChainAttestationsBase<XChainClaimAttestation>;
template class XChainAttestationsBase<XChainCreateAccountAttestation>;

}  // namespace xrpl
