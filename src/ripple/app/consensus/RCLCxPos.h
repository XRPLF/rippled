//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2016 Ripple Labs Inc.

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

#ifndef RIPPLE_APP_CONSENSUS_RCLCXPOSITION_H_INCLUDED
#define RIPPLE_APP_CONSENSUS_RCLCXPOSITION_H_INCLUDED

#include <ripple/app/ledger/LedgerProposal.h>
#include <ripple/json/json_value.h>
#include <ripple/basics/chrono.h>
#include <ripple/protocol/UintTypes.h>

namespace ripple {

class RCLCxPos
{

public:

    static std::uint32_t constexpr seqInitial = 0;
    static std::uint32_t constexpr seqLeave = 0xffffffff;

    RCLCxPos (LedgerProposal const& prop) :
        proposal_ (prop)
    { }

    std::uint32_t getSequence() const
    {
        return proposal_.getProposeSeq();
    }

    NetClock::time_point getCloseTime () const
    {
        return proposal_.getCloseTime();
    }

    NetClock::time_point getSeenTime() const
    {
        return proposal_.getSeenTime();
    }

    bool isStale (NetClock::time_point lastValid) const
    {
        return getSeenTime() < lastValid;
    }

    NodeID const& getNodeID() const
    {
        return proposal_.getPeerID();
    }

    LedgerHash const& getPosition() const
    {
        return proposal_.getCurrentHash();
    }

    LedgerHash const& getPrevLedger() const
    {
        return proposal_.getPrevLedger();
    }

    bool changePosition (
        LedgerHash const& position,
        NetClock::time_point closeTime,
        NetClock::time_point now)
    {
        return proposal_.changePosition (position, closeTime, now);
    }

    bool bowOut (NetClock::time_point now)
    {
        if (isBowOut ())
            return false;

        proposal_.bowOut (now);
        return true;
    }

    Json::Value getJson() const
    {
        return proposal_.getJson();
    }

    bool isInitial () const
    {
        return getSequence() == seqInitial;
    }

    bool isBowOut() const
    {
        return getSequence() == seqLeave;
    }

    // These three functions will be removed. New code
    // should use getPosition, getSequence and getNodeID
    LedgerHash const& getCurrentHash() const
    {
        return getPosition();
    }
    NodeID const& getPeerID() const
    {
        return getNodeID();
    }
    std::uint32_t getProposeSeq() const
    {
        return getSequence();
    }

    LedgerProposal const& peek() const
    {
        return proposal_;
    }

    LedgerProposal& peek()
    {
        return proposal_;
    }

protected:

    LedgerProposal proposal_;

};

}
#endif
