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

namespace ripple {

LedgerProposal::LedgerProposal (uint256 const& pLgr, std::uint32_t seq,
                                uint256 const& tx, std::uint32_t closeTime,
                                const RippleAddress& naPeerPublic, uint256 const& suppression) :
    mPreviousLedger (pLgr), mCurrentHash (tx), mSuppression (suppression), mCloseTime (closeTime),
    mProposeSeq (seq), mPublicKey (naPeerPublic)
{
    // XXX Validate key.
    // if (!mKey->SetPubKey(pubKey))
    // throw std::runtime_error("Invalid public key in proposal");

    mPeerID         = mPublicKey.getNodeID ();
    mTime           = boost::posix_time::second_clock::universal_time ();
}


LedgerProposal::LedgerProposal (const RippleAddress& naPub, const RippleAddress& naPriv,
                                uint256 const& prevLgr, uint256 const& position,
                                std::uint32_t closeTime) :
    mPreviousLedger (prevLgr), mCurrentHash (position), mCloseTime (closeTime), mProposeSeq (0),
    mPublicKey (naPub), mPrivateKey (naPriv)
{
    mPeerID      = mPublicKey.getNodeID ();
    mTime        = boost::posix_time::second_clock::universal_time ();
}

LedgerProposal::LedgerProposal (uint256 const& prevLgr, uint256 const& position,
                                std::uint32_t closeTime) :
    mPreviousLedger (prevLgr), mCurrentHash (position), mCloseTime (closeTime), mProposeSeq (0)
{
    mTime       = boost::posix_time::second_clock::universal_time ();
}

uint256 LedgerProposal::getSigningHash () const
{
    Serializer s ((32 + 32 + 32 + 256 + 256) / 8);

    s.add32 (getConfig ().SIGN_PROPOSAL);
    s.add32 (mProposeSeq);
    s.add32 (mCloseTime);
    s.add256 (mPreviousLedger);
    s.add256 (mCurrentHash);

    return s.getSHA512Half ();
}

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

bool LedgerProposal::checkSign (const std::string& signature, uint256 const& signingHash)
{
    return mPublicKey.verifyNodePublic (signingHash, signature, ECDSA::not_strict);
}

bool LedgerProposal::changePosition (uint256 const& newPosition, std::uint32_t closeTime)
{
    if (mProposeSeq == seqLeave)
        return false;

    mCurrentHash    = newPosition;
    mCloseTime      = closeTime;
    mTime           = boost::posix_time::second_clock::universal_time ();
    ++mProposeSeq;
    return true;
}

void LedgerProposal::bowOut ()
{
    mTime           = boost::posix_time::second_clock::universal_time ();
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
    ret["previous_ledger"] = to_string (mPreviousLedger);

    if (mProposeSeq != seqLeave)
    {
        ret["transaction_hash"] = to_string (mCurrentHash);
        ret["propose_seq"] = mProposeSeq;
    }

    ret["close_time"] = mCloseTime;

    if (mPublicKey.isValid ())
        ret["peer_id"] = mPublicKey.humanNodePublic ();

    return ret;
}

} // ripple
