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

#ifndef RIPPLE_APP_LEDGER_LEDGERCONSENSUS_H_INCLUDED
#define RIPPLE_APP_LEDGER_LEDGERCONSENSUS_H_INCLUDED

#include <ripple/app/ledger/Ledger.h>
#include <ripple/app/ledger/LedgerProposal.h>
#include <ripple/app/ledger/InboundTransactions.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/CanonicalTXSet.h>
#include <ripple/app/misc/FeeVote.h>
#include <ripple/json/json_value.h>
#include <ripple/overlay/Peer.h>
#include <ripple/protocol/RippleLedgerHash.h>
#include <chrono>

namespace ripple {

/** Manager for achieving consensus on the next ledger.

    This object is created when the consensus process starts, and
    is destroyed when the process is complete.
*/
class LedgerConsensus
{
public:
    virtual ~LedgerConsensus() = default;

    virtual Json::Value getJson (bool full) = 0;

    virtual uint256 getLCL () = 0;

    virtual void gotMap (
        std::shared_ptr<SHAMap> const& map) = 0;

    virtual void timerEntry () = 0;

    virtual bool peerPosition (LedgerProposal::ref) = 0;

    virtual void startRound (
        LedgerHash const& prevLCLHash,
        std::shared_ptr<Ledger const> const& prevLedger,
        NetClock::time_point closeTime,
        int previousProposers,
        std::chrono::milliseconds previousConvergeTime) = 0;

    /** Simulate the consensus process without any network traffic.

        The end result, is that consensus begins and completes as if everyone
        had agreed with whatever we propose.

        This function is only called from the rpc "ledger_accept" path with the
        server in standalone mode and SHOULD NOT be used during the normal
        consensus process.
    */
    virtual void simulate (
        boost::optional<std::chrono::milliseconds> consensusDelay) = 0;
};

} // ripple

#endif
