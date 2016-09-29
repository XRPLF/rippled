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

#ifndef RIPPLE_APP_LEDGER_CONSENSUS_H_INCLUDED
#define RIPPLE_APP_LEDGER_CONSENSUS_H_INCLUDED

#include <ripple/app/ledger/LedgerConsensus.h>
#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/ledger/InboundTransactions.h>
#include <ripple/app/consensus/RCLCxTraits.h>
#include <ripple/app/main/Application.h>
#include <ripple/basics/Log.h>
#include <ripple/core/Config.h>

#include <memory>

namespace ripple {

class LocalTxs;

/** Implements the consensus process and provides inter-round state. */
class Consensus
{
public:
    using Proposals = hash_map <NodeID, std::deque<LedgerProposal::pointer>>;

    virtual
    ~Consensus () = default;

    /** Returns whether we are issuing proposals currently. */
    virtual
    bool
    isProposing () const = 0;

    /** Returns whether we are issuing validations currently. */
    virtual
    bool
    isValidating () const = 0;

    /** Returns the number of unique proposers we observed for the LCL. */
    virtual
    int
    getLastCloseProposers () const = 0;

    /** Returns the time (in milliseconds) that the last close took. */
    virtual
    std::chrono::milliseconds
    getLastCloseDuration () const = 0;

    /** Called to create a LedgerConsensus instance */
    virtual
    std::shared_ptr<LedgerConsensus<RCLCxTraits>>
    makeLedgerConsensus (
        Application& app,
        InboundTransactions& inboundTransactions,
        LedgerMaster& ledgerMaster,
        LocalTxs& localTxs) = 0;

    /** Called when a new round of consensus is about to begin */
    virtual
    void
    startRound (
        NetClock::time_point now,
        LedgerConsensus<RCLCxTraits>& consensus,
        LedgerHash const &prevLCLHash,
        std::shared_ptr<Ledger const> const& prevLedger) = 0;

    /** Specified the network time when the last ledger closed */
    virtual
    void
    setLastCloseTime (NetClock::time_point t) = 0;

    virtual
    void
    storeProposal (
        LedgerProposal::ref proposal,
        NodeID const& nodeID) = 0;
};

std::unique_ptr<Consensus>
make_Consensus (Config const& config, Logs& logs);

}

#endif
