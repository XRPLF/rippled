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

bool
AttestationBase::sameEventHelper(AttestationBase const& lhs, AttestationBase const& rhs)
{
    return std::tie(lhs.sendingAccount, lhs.sendingAmount, lhs.wasLockingChainSend) ==
        std::tie(rhs.sendingAccount, rhs.sendingAmount, rhs.wasLockingChainSend);
}

bool
AttestationBase::verify(STXChainBridge const& bridge) const
{
    std::vector<std::uint8_t> const msg = message(bridge);
    return xrpl::verify(publicKey, makeSlice(msg), signature);
}

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

AttestationClaim::AttestationClaim(STObject const& o)
    : AttestationBase(o), claimID{o[sfXChainClaimID]}, dst{o[~sfDestination]}
{
}

AttestationClaim::AttestationClaim(json::Value const& v)
    : AttestationBase{v}, claimID{json::getOrThrow<std::uint64_t>(v, sfXChainClaimID)}
{
    if (v.isMember(sfDestination.getJsonName()))
        dst = json::getOrThrow<AccountID>(v, sfDestination);
}

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
    STObject o{kSfGeneric};
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

std::vector<std::uint8_t>
AttestationClaim::message(STXChainBridge const& bridge) const
{
    return AttestationClaim::message(
        bridge, sendingAccount, sendingAmount, rewardAccount, wasLockingChainSend, claimID, dst);
}

bool
AttestationClaim::validAmounts() const
{
    return isLegalNet(sendingAmount);
}

bool
AttestationClaim::sameEvent(AttestationClaim const& rhs) const
{
    return AttestationClaim::sameEventHelper(*this, rhs) &&
        tie(claimID, dst) == tie(rhs.claimID, rhs.dst);
}

bool
operator==(AttestationClaim const& lhs, AttestationClaim const& rhs)
{
    return AttestationClaim::equalHelper(lhs, rhs) &&
        tie(lhs.claimID, lhs.dst) == tie(rhs.claimID, rhs.dst);
}

AttestationCreateAccount::AttestationCreateAccount(STObject const& o)
    : AttestationBase(o)
    , createCount{o[sfXChainAccountCreateCount]}
    , toCreate{o[sfDestination]}
    , rewardAmount{o[sfSignatureReward]}
{
}

AttestationCreateAccount::AttestationCreateAccount(json::Value const& v)
    : AttestationBase{v}
    , createCount{json::getOrThrow<std::uint64_t>(v, sfXChainAccountCreateCount)}
    , toCreate{json::getOrThrow<AccountID>(v, sfDestination)}
    , rewardAmount{json::getOrThrow<STAmount>(v, sfSignatureReward)}
{
}

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
    STObject o{kSfGeneric};
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

bool
AttestationCreateAccount::validAmounts() const
{
    return isLegalNet(rewardAmount) && isLegalNet(sendingAmount);
}

bool
AttestationCreateAccount::sameEvent(AttestationCreateAccount const& rhs) const
{
    return AttestationCreateAccount::sameEventHelper(*this, rhs) &&
        std::tie(createCount, toCreate, rewardAmount) ==
        std::tie(rhs.createCount, rhs.toCreate, rhs.rewardAmount);
}

bool
operator==(AttestationCreateAccount const& lhs, AttestationCreateAccount const& rhs)
{
    return AttestationCreateAccount::equalHelper(lhs, rhs) &&
        std::tie(lhs.createCount, lhs.toCreate, lhs.rewardAmount) ==
        std::tie(rhs.createCount, rhs.toCreate, rhs.rewardAmount);
}

}  // namespace Attestations

SField const& XChainClaimAttestation::arrayFieldName{sfXChainClaimAttestations};
SField const& XChainCreateAccountAttestation::arrayFieldName{sfXChainCreateAccountAttestations};

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

XChainClaimAttestation::XChainClaimAttestation(STObject const& o)
    : XChainClaimAttestation{
          o[sfAttestationSignerAccount],
          PublicKey{o[sfPublicKey]},
          o[sfAmount],
          o[sfAttestationRewardAccount],
          o[sfWasLockingChainSend] != 0,
          o[~sfDestination]} {};

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

XChainClaimAttestation::MatchFields::MatchFields(
    XChainClaimAttestation::TSignedAttestation const& att)
    : amount{att.sendingAmount}, wasLockingChainSend{att.wasLockingChainSend}, dst{att.dst}
{
}

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

XChainCreateAccountAttestation::XChainCreateAccountAttestation(STObject const& o)
    : XChainCreateAccountAttestation{
          o[sfAttestationSignerAccount],
          PublicKey{o[sfPublicKey]},
          o[sfAmount],
          o[sfSignatureReward],
          o[sfAttestationRewardAccount],
          o[sfWasLockingChainSend] != 0,
          o[sfDestination]} {};

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

XChainCreateAccountAttestation::MatchFields::MatchFields(
    XChainCreateAccountAttestation::TSignedAttestation const& att)
    : amount{att.sendingAmount}
    , rewardAmount(att.rewardAmount)
    , wasLockingChainSend{att.wasLockingChainSend}
    , dst{att.toCreate}
{
}

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
//
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

        if (jAtts.size() > kMaxAttestations)
            Throw<std::runtime_error>("XChainAttestationsBase exceeded max number of attestations");

        std::vector<TAttestation> r;
        r.reserve(jAtts.size());
        for (auto const& a : jAtts)
            r.emplace_back(a);
        return r;
    }();
}

template <class TAttestation>
XChainAttestationsBase<TAttestation>::XChainAttestationsBase(STArray const& arr)
{
    if (arr.size() > kMaxAttestations)
        Throw<std::runtime_error>("XChainAttestationsBase exceeded max number of attestations");

    attestations_.reserve(arr.size());
    for (auto const& o : arr)
        attestations_.emplace_back(o);
}

template <class TAttestation>
STArray
XChainAttestationsBase<TAttestation>::toSTArray() const
{
    STArray r{TAttestation::arrayFieldName, attestations_.size()};
    for (auto const& e : attestations_)
        r.emplaceBack(e.toSTObject());
    return r;
}

template class XChainAttestationsBase<XChainClaimAttestation>;
template class XChainAttestationsBase<XChainCreateAccountAttestation>;

}  // namespace xrpl
