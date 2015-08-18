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

#ifndef RIPPLE_APP_LEDGER_LEDGERPROPOSAL_H_INCLUDED
#define RIPPLE_APP_LEDGER_LEDGERPROPOSAL_H_INCLUDED

#include <ripple/basics/CountedObject.h>
#include <ripple/basics/base_uint.h>
#include <ripple/json/json_value.h>
#include <ripple/protocol/HashPrefix.h>
#include <ripple/protocol/PublicKey.h>
#include <ripple/protocol/RippleAddress.h>
#include <beast/hash/hash_append.h>
#include <chrono>
#include <cstdint>
#include <string>

namespace ripple {

class LedgerProposal
    : public CountedObject <LedgerProposal>
{
private:
    // A peer initial joins the consensus process
    static std::uint32_t const seqJoin = 0;

    // A peer wants to bow out and leave the consensus process
    static std::uint32_t const seqLeave = 0xffffffff;

public:
    static char const* getCountedObjectName () { return "LedgerProposal"; }

    using pointer = std::shared_ptr<LedgerProposal>;
    using ref = const pointer&;

    // proposal from peer
    LedgerProposal (
        uint256 const& prevLgr,
        std::uint32_t proposeSeq,
        uint256 const& propose,
        std::uint32_t closeTime,
        RippleAddress const& publicKey,
        PublicKey const& pk,
        uint256 const& suppress);

    // Our own proposal: the publicKey, if set, indicates we are a validating
    // node but does not indicate if we're validating in this consensus round.
    LedgerProposal (
        RippleAddress const& publicKey,
        uint256 const& prevLedger,
        uint256 const& position,
        std::uint32_t closeTime);

    uint256 getSigningHash () const;
    bool checkSign (std::string const& signature) const;

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

    Blob sign (RippleAddress const& privateKey);

    bool isPrevLedger (uint256 const& pl) const
    {
        return mPreviousLedger == pl;
    }
    bool isInitial () const
    {
        return mProposeSeq == seqJoin;
    }
    bool isBowOut () const
    {
        return mProposeSeq == seqLeave;
    }

    bool isStale (std::chrono::steady_clock::time_point cutoff) const
    {
        return mTime <= cutoff;
    }

    bool changePosition (
        uint256 const& newPosition, std::uint32_t newCloseTime);
    void bowOut ();
    Json::Value getJson () const;

private:
    template <class Hasher>
    void
    hash_append (Hasher& h) const
    {
        using beast::hash_append;
        hash_append(h, HashPrefix::proposal);
        hash_append(h, std::uint32_t(mProposeSeq));
        hash_append(h, std::uint32_t(mCloseTime));
        hash_append(h, mPreviousLedger);
        hash_append(h, mCurrentHash);
    }

    uint256 mPreviousLedger, mCurrentHash, mSuppression;
    std::uint32_t mCloseTime, mProposeSeq;

    NodeID          mPeerID;
    RippleAddress   mPublicKey;
    PublicKey publicKey_;

    std::chrono::steady_clock::time_point mTime;
};

/** Calculate a unique identifier for a signed proposal.

    The identifier is based on all the fields that contribute to the signature,
    as well as the signature itself. The "last closed ledger" field may be
    omitted, but the signer will compute the signature as if this
    field was present. Recipients of the proposal will inject the last closed
    ledger in order to validate the signature. If the last closed ledger is left
    out, then it is considered as all zeroes for the purposes of signing.
*/
uint256 proposalUniqueId (
        uint256 const& proposeHash,
        uint256 const& previousLedger,
        std::uint32_t proposeSeq,
        std::uint32_t closeTime,
        Blob const& pubKey,
        Blob const& signature);

} // ripple

#endif
