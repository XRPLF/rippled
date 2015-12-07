//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#include <BeastConfig.h>
#include <ripple/app/ledger/LedgerProposal.h>
#include <ripple/protocol/digest.h>
#include <ripple/core/Config.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/protocol/HashPrefix.h>
#include <ripple/protocol/Serializer.h>

namespace ripple {

// Used to construct received proposals
LedgerProposal::LedgerProposal (
        uint256 const& pLgr,
        std::uint32_t seq,
        uint256 const& tx,
        NetClock::time_point closeTime,
        RippleAddress const& publicKey,
        PublicKey const& pk,
        uint256 const& suppression)
    : mPreviousLedger (pLgr)
    , mCurrentHash (tx)
    , mSuppression (suppression)
    , mCloseTime (closeTime)
    , mProposeSeq (seq)
    , mPublicKey (publicKey)
    , publicKey_ (pk)
{
    mPeerID = mPublicKey.getNodeID ();
    mTime = std::chrono::steady_clock::now ();
}

// Used to construct local proposals
// CAUTION: publicKey_ not set
LedgerProposal::LedgerProposal (
        RippleAddress const& publicKey,
        uint256 const& prevLgr,
        uint256 const& position,
        NetClock::time_point closeTime)
    : mPreviousLedger (prevLgr)
    , mCurrentHash (position)
    , mCloseTime (closeTime)
    , mProposeSeq (seqJoin)
    , mPublicKey (publicKey)
{
    if (mPublicKey.isValid ())
        mPeerID = mPublicKey.getNodeID ();

    mTime = std::chrono::steady_clock::now ();
}

uint256 LedgerProposal::getSigningHash () const
{
    return sha512Half(
        HashPrefix::proposal,
        std::uint32_t(mProposeSeq),
        mCloseTime.time_since_epoch().count(),
        mPreviousLedger,
        mCurrentHash);
}

bool LedgerProposal::checkSign () const
{
    return mPublicKey.verifyNodePublic(
        getSigningHash(), signature_, ECDSA::not_strict);
}

bool LedgerProposal::changePosition (
    uint256 const& newPosition,
    NetClock::time_point closeTime)
{
    if (mProposeSeq == seqLeave)
        return false;

    mCurrentHash    = newPosition;
    mCloseTime      = closeTime;
    mTime           = std::chrono::steady_clock::now ();
    ++mProposeSeq;
    return true;
}

void LedgerProposal::bowOut ()
{
    mTime           = std::chrono::steady_clock::now ();
    mProposeSeq     = seqLeave;
}

Blob const& LedgerProposal::sign (RippleAddress const& privateKey)
{
    privateKey.signNodePrivate (getSigningHash (), signature_);
    mSuppression = proposalUniqueId (mCurrentHash, mPreviousLedger, mProposeSeq,
        mCloseTime, mPublicKey.getNodePublic (), signature_);
    return signature_;
}

Json::Value LedgerProposal::getJson () const
{
    Json::Value ret = Json::objectValue;
    ret[jss::previous_ledger] = to_string (mPreviousLedger);

    if (mProposeSeq != seqLeave)
    {
        ret[jss::transaction_hash] = to_string (mCurrentHash);
        ret[jss::propose_seq] = mProposeSeq;
    }

    ret[jss::close_time] = mCloseTime.time_since_epoch().count();

    if (mPublicKey.isValid ())
        ret[jss::peer_id] = mPublicKey.humanNodePublic ();

    return ret;
}

uint256 proposalUniqueId (
    uint256 const& proposeHash,
    uint256 const& previousLedger,
    std::uint32_t proposeSeq,
    NetClock::time_point closeTime,
    Blob const& pubKey,
    Blob const& signature)
{

    Serializer s (512);
    s.add256 (proposeHash);
    s.add256 (previousLedger);
    s.add32 (proposeSeq);
    s.add32 (closeTime.time_since_epoch().count());
    s.addVL (pubKey);
    s.addVL (signature);

    return s.getSHA512Half ();
}

} // ripple
