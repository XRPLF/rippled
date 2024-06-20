//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2022 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <ripple/protocol/XChainAttestations.h>

#include <ripple/basics/Expected.h>
#include <ripple/basics/Log.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/protocol/AccountID.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/PublicKey.h>
#include <ripple/protocol/SField.h>
#include <ripple/protocol/STAccount.h>
#include <ripple/protocol/STAmount.h>
#include <ripple/protocol/STArray.h>
#include <ripple/protocol/STObject.h>
#include <ripple/protocol/Serializer.h>
#include <ripple/protocol/XChainAttestations.h>
#include <ripple/protocol/json_get_or_throw.h>
#include <ripple/protocol/jss.h>

#include <algorithm>
#include <optional>

namespace ripple {
namespace Attestations {

AttestationBase::AttestationBase(
    AccountID attestationSignerAccount_,
    PublicKey const& publicKey_,
    Buffer signature_,
    AccountID const& sendingAccount_,
    STAmount const& sendingAmount_,
    AccountID const& rewardAccount_,
    bool wasLockingChainSend_)
    : attestationSignerAccount{attestationSignerAccount_}
    , publicKey{publicKey_}
    , signature{std::move(signature_)}
    , sendingAccount{sendingAccount_}
    , sendingAmount{sendingAmount_}
    , rewardAccount{rewardAccount_}
    , wasLockingChainSend{wasLockingChainSend_}
{
}

bool
AttestationBase::equalHelper(
    AttestationBase const& lhs,
    AttestationBase const& rhs)
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
AttestationBase::sameEventHelper(
    AttestationBase const& lhs,
    AttestationBase const& rhs)
{
    return std::tie(
               lhs.sendingAccount,
               lhs.sendingAmount,
               lhs.wasLockingChainSend) ==
        std::tie(
               rhs.sendingAccount, rhs.sendingAmount, rhs.wasLockingChainSend);
}

bool
AttestationBase::verify(STXChainBridge const& bridge) const
{
    std::vector<std::uint8_t> msg = message(bridge);
    return ripple::verify(publicKey, makeSlice(msg), signature);
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

AttestationBase::AttestationBase(Json::Value const& v)
    : attestationSignerAccount{Json::getOrThrow<AccountID>(
          v,
          sfAttestationSignerAccount)}
    , publicKey{Json::getOrThrow<PublicKey>(v, sfPublicKey)}
    , signature{Json::getOrThrow<Buffer>(v, sfSignature)}
    , sendingAccount{Json::getOrThrow<AccountID>(v, sfAccount)}
    , sendingAmount{Json::getOrThrow<STAmount>(v, sfAmount)}
    , rewardAccount{Json::getOrThrow<AccountID>(v, sfAttestationRewardAccount)}
    , wasLockingChainSend{Json::getOrThrow<bool>(v, sfWasLockingChainSend)}
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
    AccountID attestationSignerAccount_,
    PublicKey const& publicKey_,
    Buffer signature_,
    AccountID const& sendingAccount_,
    STAmount const& sendingAmount_,
    AccountID const& rewardAccount_,
    bool wasLockingChainSend_,
    std::uint64_t claimID_,
    std::optional<AccountID> const& dst_)
    : AttestationBase(
          attestationSignerAccount_,
          publicKey_,
          std::move(signature_),
          sendingAccount_,
          sendingAmount_,
          rewardAccount_,
          wasLockingChainSend_)
    , claimID{claimID_}
    , dst{dst_}
{
}

AttestationClaim::AttestationClaim(
    STXChainBridge const& bridge,
    AccountID attestationSignerAccount_,
    PublicKey const& publicKey_,
    SecretKey const& secretKey_,
    AccountID const& sendingAccount_,
    STAmount const& sendingAmount_,
    AccountID const& rewardAccount_,
    bool wasLockingChainSend_,
    std::uint64_t claimID_,
    std::optional<AccountID> const& dst_)
    : AttestationClaim{
          attestationSignerAccount_,
          publicKey_,
          Buffer{},
          sendingAccount_,
          sendingAmount_,
          rewardAccount_,
          wasLockingChainSend_,
          claimID_,
          dst_}
{
    auto const toSign = message(bridge);
    signature = sign(publicKey_, secretKey_, makeSlice(toSign));
}

AttestationClaim::AttestationClaim(STObject const& o)
    : AttestationBase(o), claimID{o[sfXChainClaimID]}, dst{o[~sfDestination]}
{
}

AttestationClaim::AttestationClaim(Json::Value const& v)
    : AttestationBase{v}
    , claimID{Json::getOrThrow<std::uint64_t>(v, sfXChainClaimID)}
{
    if (v.isMember(sfDestination.getJsonName()))
        dst = Json::getOrThrow<AccountID>(v, sfDestination);
}

STObject
AttestationClaim::toSTObject() const
{
    STObject o{sfXChainClaimAttestationCollectionElement};
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
    STObject o{sfGeneric};
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
        bridge,
        sendingAccount,
        sendingAmount,
        rewardAccount,
        wasLockingChainSend,
        claimID,
        dst);
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

AttestationCreateAccount::AttestationCreateAccount(Json::Value const& v)
    : AttestationBase{v}
    , createCount{Json::getOrThrow<std::uint64_t>(
          v,
          sfXChainAccountCreateCount)}
    , toCreate{Json::getOrThrow<AccountID>(v, sfDestination)}
    , rewardAmount{Json::getOrThrow<STAmount>(v, sfSignatureReward)}
{
}

AttestationCreateAccount::AttestationCreateAccount(
    AccountID attestationSignerAccount_,
    PublicKey const& publicKey_,
    Buffer signature_,
    AccountID const& sendingAccount_,
    STAmount const& sendingAmount_,
    STAmount const& rewardAmount_,
    AccountID const& rewardAccount_,
    bool wasLockingChainSend_,
    std::uint64_t createCount_,
    AccountID const& toCreate_)
    : AttestationBase(
          attestationSignerAccount_,
          publicKey_,
          std::move(signature_),
          sendingAccount_,
          sendingAmount_,
          rewardAccount_,
          wasLockingChainSend_)
    , createCount{createCount_}
    , toCreate{toCreate_}
    , rewardAmount{rewardAmount_}
{
}

AttestationCreateAccount::AttestationCreateAccount(
    STXChainBridge const& bridge,
    AccountID attestationSignerAccount_,
    PublicKey const& publicKey_,
    SecretKey const& secretKey_,
    AccountID const& sendingAccount_,
    STAmount const& sendingAmount_,
    STAmount const& rewardAmount_,
    AccountID const& rewardAccount_,
    bool wasLockingChainSend_,
    std::uint64_t createCount_,
    AccountID const& toCreate_)
    : AttestationCreateAccount{
          attestationSignerAccount_,
          publicKey_,
          Buffer{},
          sendingAccount_,
          sendingAmount_,
          rewardAmount_,
          rewardAccount_,
          wasLockingChainSend_,
          createCount_,
          toCreate_}
{
    auto const toSign = message(bridge);
    signature = sign(publicKey_, secretKey_, makeSlice(toSign));
}

STObject
AttestationCreateAccount::toSTObject() const
{
    STObject o{sfXChainCreateAccountAttestationCollectionElement};
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
    STObject o{sfGeneric};
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
operator==(
    AttestationCreateAccount const& lhs,
    AttestationCreateAccount const& rhs)
{
    return AttestationCreateAccount::equalHelper(lhs, rhs) &&
        std::tie(lhs.createCount, lhs.toCreate, lhs.rewardAmount) ==
        std::tie(rhs.createCount, rhs.toCreate, rhs.rewardAmount);
}

}  // namespace Attestations

SField const& XChainClaimAttestation::ArrayFieldName{sfXChainClaimAttestations};
SField const& XChainCreateAccountAttestation::ArrayFieldName{
    sfXChainCreateAccountAttestations};

XChainClaimAttestation::XChainClaimAttestation(
    AccountID const& keyAccount_,
    PublicKey const& publicKey_,
    STAmount const& amount_,
    AccountID const& rewardAccount_,
    bool wasLockingChainSend_,
    std::optional<AccountID> const& dst_)
    : keyAccount(keyAccount_)
    , publicKey(publicKey_)
    , amount(sfAmount, amount_)
    , rewardAccount(rewardAccount_)
    , wasLockingChainSend(wasLockingChainSend_)
    , dst(dst_)
{
}

XChainClaimAttestation::XChainClaimAttestation(
    STAccount const& keyAccount_,
    PublicKey const& publicKey_,
    STAmount const& amount_,
    STAccount const& rewardAccount_,
    bool wasLockingChainSend_,
    std::optional<STAccount> const& dst_)
    : XChainClaimAttestation{
          keyAccount_.value(),
          publicKey_,
          amount_,
          rewardAccount_.value(),
          wasLockingChainSend_,
          dst_ ? std::optional<AccountID>{dst_->value()} : std::nullopt}
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

XChainClaimAttestation::XChainClaimAttestation(Json::Value const& v)
    : XChainClaimAttestation{
          Json::getOrThrow<AccountID>(v, sfAttestationSignerAccount),
          Json::getOrThrow<PublicKey>(v, sfPublicKey),
          Json::getOrThrow<STAmount>(v, sfAmount),
          Json::getOrThrow<AccountID>(v, sfAttestationRewardAccount),
          Json::getOrThrow<bool>(v, sfWasLockingChainSend),
          std::nullopt}
{
    if (v.isMember(sfDestination.getJsonName()))
        dst = Json::getOrThrow<AccountID>(v, sfDestination);
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
    STObject o{sfXChainClaimProofSig};
    o[sfAttestationSignerAccount] =
        STAccount{sfAttestationSignerAccount, keyAccount};
    o[sfPublicKey] = publicKey;
    o[sfAmount] = STAmount{sfAmount, amount};
    o[sfAttestationRewardAccount] =
        STAccount{sfAttestationRewardAccount, rewardAccount};
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
    : amount{att.sendingAmount}
    , wasLockingChainSend{att.wasLockingChainSend}
    , dst{att.dst}
{
}

AttestationMatch
XChainClaimAttestation::match(
    XChainClaimAttestation::MatchFields const& rhs) const
{
    if (std::tie(amount, wasLockingChainSend) !=
        std::tie(rhs.amount, rhs.wasLockingChainSend))
        return AttestationMatch::nonDstMismatch;
    if (dst != rhs.dst)
        return AttestationMatch::matchExceptDst;
    return AttestationMatch::match;
}

//------------------------------------------------------------------------------

XChainCreateAccountAttestation::XChainCreateAccountAttestation(
    AccountID const& keyAccount_,
    PublicKey const& publicKey_,
    STAmount const& amount_,
    STAmount const& rewardAmount_,
    AccountID const& rewardAccount_,
    bool wasLockingChainSend_,
    AccountID const& dst_)
    : keyAccount(keyAccount_)
    , publicKey(publicKey_)
    , amount(sfAmount, amount_)
    , rewardAmount(sfSignatureReward, rewardAmount_)
    , rewardAccount(rewardAccount_)
    , wasLockingChainSend(wasLockingChainSend_)
    , dst(dst_)
{
}

XChainCreateAccountAttestation::XChainCreateAccountAttestation(
    STObject const& o)
    : XChainCreateAccountAttestation{
          o[sfAttestationSignerAccount],
          PublicKey{o[sfPublicKey]},
          o[sfAmount],
          o[sfSignatureReward],
          o[sfAttestationRewardAccount],
          o[sfWasLockingChainSend] != 0,
          o[sfDestination]} {};

XChainCreateAccountAttestation ::XChainCreateAccountAttestation(
    Json::Value const& v)
    : XChainCreateAccountAttestation{
          Json::getOrThrow<AccountID>(v, sfAttestationSignerAccount),
          Json::getOrThrow<PublicKey>(v, sfPublicKey),
          Json::getOrThrow<STAmount>(v, sfAmount),
          Json::getOrThrow<STAmount>(v, sfSignatureReward),
          Json::getOrThrow<AccountID>(v, sfAttestationRewardAccount),
          Json::getOrThrow<bool>(v, sfWasLockingChainSend),
          Json::getOrThrow<AccountID>(v, sfDestination)}
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
    STObject o{sfXChainCreateAccountProofSig};

    o[sfAttestationSignerAccount] =
        STAccount{sfAttestationSignerAccount, keyAccount};
    o[sfPublicKey] = publicKey;
    o[sfAmount] = STAmount{sfAmount, amount};
    o[sfSignatureReward] = STAmount{sfSignatureReward, rewardAmount};
    o[sfAttestationRewardAccount] =
        STAccount{sfAttestationRewardAccount, rewardAccount};
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
XChainCreateAccountAttestation::match(
    XChainCreateAccountAttestation::MatchFields const& rhs) const
{
    if (std::tie(amount, rewardAmount, wasLockingChainSend) !=
        std::tie(rhs.amount, rhs.rewardAmount, rhs.wasLockingChainSend))
        return AttestationMatch::nonDstMismatch;
    if (dst != rhs.dst)
        return AttestationMatch::matchExceptDst;
    return AttestationMatch::match;
}

bool
operator==(
    XChainCreateAccountAttestation const& lhs,
    XChainCreateAccountAttestation const& rhs)
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
XChainAttestationsBase<TAttestation>::XChainAttestationsBase(
    Json::Value const& v)
{
    if (!v.isObject())
    {
        Throw<std::runtime_error>(
            "XChainAttestationsBase can only be specified with an 'object' "
            "Json value");
    }

    attestations_ = [&] {
        auto const jAtts = v[jss::attestations];

        if (jAtts.size() > maxAttestations)
            Throw<std::runtime_error>(
                "XChainAttestationsBase exceeded max number of attestations");

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
    if (arr.size() > maxAttestations)
        Throw<std::runtime_error>(
            "XChainAttestationsBase exceeded max number of attestations");

    attestations_.reserve(arr.size());
    for (auto const& o : arr)
        attestations_.emplace_back(o);
}

template <class TAttestation>
STArray
XChainAttestationsBase<TAttestation>::toSTArray() const
{
    STArray r{TAttestation::ArrayFieldName, attestations_.size()};
    for (auto const& e : attestations_)
        r.emplace_back(e.toSTObject());
    return r;
}

template class XChainAttestationsBase<XChainClaimAttestation>;
template class XChainAttestationsBase<XChainCreateAccountAttestation>;

}  // namespace ripple
