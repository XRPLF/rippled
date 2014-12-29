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

#ifndef RIPPLE_APP_LEDGERPROPOSAL_H_INCLUDED
#define RIPPLE_APP_LEDGERPROPOSAL_H_INCLUDED

#include <ripple/basics/CountedObject.h>
#include <ripple/basics/base_uint.h>
#include <ripple/json/json_value.h>
#include <ripple/protocol/RippleAddress.h>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <cstdint>
#include <string>

namespace ripple {

class LedgerProposal
    : public CountedObject <LedgerProposal>
{
public:
    static char const* getCountedObjectName () { return "LedgerProposal"; }

    static const std::uint32_t seqLeave = 0xffffffff; // leaving the consensus process

    typedef std::shared_ptr<LedgerProposal> pointer;
    typedef const pointer& ref;

    // proposal from peer
    LedgerProposal (uint256 const& prevLgr, std::uint32_t proposeSeq, uint256 const& propose,
                    std::uint32_t closeTime, const RippleAddress & naPeerPublic, uint256 const& suppress);

    // our first proposal
    LedgerProposal (const RippleAddress & pubKey, const RippleAddress & privKey,
                    uint256 const& prevLedger, uint256 const& position, std::uint32_t closeTime);

    // an unsigned "dummy" proposal for nodes not validating
    LedgerProposal (uint256 const& prevLedger, uint256 const& position, std::uint32_t closeTime);

    uint256 getSigningHash () const;
    bool checkSign (std::string const& signature, uint256 const& signingHash);
    bool checkSign (std::string const& signature)
    {
        return checkSign (signature, getSigningHash ());
    }
    bool checkSign ()
    {
        return checkSign (mSignature, getSigningHash ());
    }

    NodeID const& getPeerID () const
    {
        return mPeerID;
    }
    uint256 const& getCurrentHash () const
    {
        return mCurrentHash;
    }
    uint256 const& getPrevLedger () const
    {
        return mPreviousLedger;
    }
    uint256 const& getSuppressionID () const
    {
        return mSuppression;
    }
    std::uint32_t getProposeSeq () const
    {
        return mProposeSeq;
    }
    std::uint32_t getCloseTime () const
    {
        return mCloseTime;
    }
    RippleAddress const& peekPublic () const
    {
        return mPublicKey;
    }
    Blob getPubKey () const
    {
        return mPublicKey.getNodePublic ();
    }
    Blob sign ();

    void setPrevLedger (uint256 const& prevLedger)
    {
        mPreviousLedger = prevLedger;
    }
    void setSignature (std::string const& signature)
    {
        mSignature = signature;
    }
    bool hasSignature ()
    {
        return !mSignature.empty ();
    }
    bool isPrevLedger (uint256 const& pl)
    {
        return mPreviousLedger == pl;
    }
    bool isBowOut ()
    {
        return mProposeSeq == seqLeave;
    }

    const boost::posix_time::ptime getCreateTime ()
    {
        return mTime;
    }
    bool isStale (boost::posix_time::ptime cutoff)
    {
        return mTime <= cutoff;
    }

    bool changePosition (uint256 const& newPosition, std::uint32_t newCloseTime);
    void bowOut ();
    Json::Value getJson () const;

    static uint256 computeSuppressionID (
        uint256 const& proposeHash,
        uint256 const& previousLedger,
        std::uint32_t proposeSeq,
        std::uint32_t closeTime,
        Blob const& pubKey,
        Blob const& signature);

private:
    uint256 mPreviousLedger, mCurrentHash, mSuppression;
    std::uint32_t mCloseTime, mProposeSeq;

    NodeID         mPeerID;
    RippleAddress   mPublicKey;
    RippleAddress   mPrivateKey;    // If ours

    std::string                 mSignature; // set only if needed
    boost::posix_time::ptime    mTime;
};

} // ripple

#endif
