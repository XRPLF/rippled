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
#include <ripple/core/Config.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/protocol/HashPrefix.h>
#include <ripple/protocol/Serializer.h>

namespace ripple {

LedgerProposal::LedgerProposal (uint256 const& pLgr, std::uint32_t seq,
                                uint256 const& tx, std::uint32_t closeTime,
                                RippleAddress const& naPeerPublic, uint256 const& suppression) :
    mPreviousLedger (pLgr), mCurrentHash (tx), mSuppression (suppression), mCloseTime (closeTime),
    mProposeSeq (seq), mPublicKey (naPeerPublic)
{
    // XXX Validate key.
    // if (!mKey->SetPubKey(pubKey))
    // throw std::runtime_error("Invalid public key in proposal");

    mPeerID         = mPublicKey.getNodeID ();
    mTime           = std::chrono::steady_clock::now ();
}

LedgerProposal::LedgerProposal (RippleAddress const& naPub, RippleAddress const& naPriv,
                                uint256 const& prevLgr, uint256 const& position,
                                std::uint32_t closeTime) :
    mPreviousLedger (prevLgr), mCurrentHash (position), mCloseTime (closeTime), mProposeSeq (0),
    mPublicKey (naPub), mPrivateKey (naPriv)
{
    mPeerID      = mPublicKey.getNodeID ();
    mTime        = std::chrono::steady_clock::now ();
}

LedgerProposal::LedgerProposal (uint256 const& prevLgr, uint256 const& position,
                                std::uint32_t closeTime) :
    mPreviousLedger (prevLgr), mCurrentHash (position), mCloseTime (closeTime), mProposeSeq (0)
{
    mTime       = std::chrono::steady_clock::now ();
}

uint256 LedgerProposal::getSigningHash () const
{
    Serializer s ((32 + 32 + 32 + 256 + 256) / 8);

    s.add32 (HashPrefix::proposal);
    s.add32 (mProposeSeq);
    s.add32 (mCloseTime);
    s.add256 (mPreviousLedger);
    s.add256 (mCurrentHash);

    return s.getSHA512Half ();
}

/*
The "id" is a unique value computed on all fields that contribute to
the signature, and including the signature. There is one caveat, the
"last closed ledger" field may be omitted. However, the signer still
computes the signature as if this field was present. Recipients of
the proposal need to inject the last closed ledger in order to
validate the signature. If the last closed ledger is left out, then
it is considered as all zeroes for the purposes of signing.
*/
// Compute a unique identifier for this signed proposal
uint256 LedgerProposal::computeSuppressionID (
    uint256 const& proposeHash,
    uint256 const& previousLedger,
    std::uint32_t proposeSeq,
    std::uint32_t closeTime,
    Blob const& pubKey,
    Blob const& signature)
{

    Serializer s (512);
    s.add256 (proposeHash);
    s.add256 (previousLedger);
    s.add32 (proposeSeq);
    s.add32 (closeTime);
    s.addVL (pubKey);
    s.addVL (signature);

    return s.getSHA512Half ();
}

bool LedgerProposal::checkSign (std::string const& signature, uint256 const& signingHash)
{
    return mPublicKey.verifyNodePublic (signingHash, signature, ECDSA::not_strict);
}

bool LedgerProposal::changePosition (uint256 const& newPosition, std::uint32_t closeTime)
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

Blob LedgerProposal::sign (void)
{
    Blob ret;

    mPrivateKey.signNodePrivate (getSigningHash (), ret);
    // XXX If this can fail, find out sooner.
    // if (!mPrivateKey.signNodePrivate(getSigningHash(), ret))
    //  throw std::runtime_error("unable to sign proposal");

    mSuppression = computeSuppressionID (mCurrentHash, mPreviousLedger, mProposeSeq,
        mCloseTime, mPublicKey.getNodePublic (), ret);

    return ret;
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

    ret[jss::close_time] = mCloseTime;

    if (mPublicKey.isValid ())
        ret[jss::peer_id] = mPublicKey.humanNodePublic ();

    return ret;
}

} // ripple
