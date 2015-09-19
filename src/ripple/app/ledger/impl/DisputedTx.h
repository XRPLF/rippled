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

#ifndef RIPPLE_APP_CONSENSUS_DISPUTEDTX_H_INCLUDED
#define RIPPLE_APP_CONSENSUS_DISPUTEDTX_H_INCLUDED

#include <ripple/protocol/UintTypes.h>
#include <ripple/protocol/Serializer.h>
#include <ripple/basics/base_uint.h>
#include <beast/utility/Journal.h>
#include <memory>

namespace ripple {

/** A transaction discovered to be in dispute during conensus.

    During consensus, a @ref DisputedTx is created when a transaction
    is discovered to be disputed. The object persists only as long as
    the dispute.

    Undisputed transactions have no corresponding @ref DisputedTx object.
*/
class DisputedTx
{
public:
    // VFALCO `Blob` is a poor choice of parameter
    DisputedTx (uint256 const& txID,
            Blob const& tx, bool ourVote, beast::Journal j)
        : mTransactionID (txID)
        , mYays (0)
        , mNays (0)
        , mOurVote (ourVote)
        , transaction (tx.data(), tx.size())
        , j_ (j)
    {
    }

    uint256 const& getTransactionID () const
    {
        return mTransactionID;
    }

    bool getOurVote () const
    {
        return mOurVote;
    }

    // VFALCO TODO make this const
    // VFALCO TODO Don't return a Serializer (doh)
    Serializer& peekTransaction ()
    {
        return transaction;
    }

    void setOurVote (bool o)
    {
        mOurVote = o;
    }

    // VFALCO NOTE its not really a peer, its the 160 bit hash of the
    // validator's public key.
    void setVote (NodeID const& peer, bool votesYes);
    void unVote (NodeID const& peer);

    bool updateVote (int percentTime, bool proposing);
    Json::Value getJson ();

private:
    uint256 mTransactionID;
    int mYays;
    int mNays;
    bool mOurVote;
    // VFALCO Why is this being stored as a Serializer?
    Serializer transaction;

    hash_map <NodeID, bool> mVotes;
    beast::Journal j_;
};

} // ripple

#endif
