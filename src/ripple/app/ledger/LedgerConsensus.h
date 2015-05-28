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

#ifndef RIPPLE_APP_CONSENSUS_LEDGERCONSENSUS_H_INCLUDED
#define RIPPLE_APP_CONSENSUS_LEDGERCONSENSUS_H_INCLUDED

#include <ripple/app/ledger/Ledger.h>
#include <ripple/app/ledger/LedgerProposal.h>
#include <ripple/app/misc/CanonicalTXSet.h>
#include <ripple/app/misc/FeeVote.h>
#include <ripple/app/tx/LocalTxs.h>
#include <ripple/json/json_value.h>
#include <ripple/overlay/Peer.h>
#include <ripple/protocol/RippleLedgerHash.h>
#include <chrono>

namespace ripple {

class LoadFeeTrack;

/** Manager for achieving consensus on the next ledger.

    This object is created when the consensus process starts, and
    is destroyed when the process is complete.
*/
class LedgerConsensus
{
public:
    virtual ~LedgerConsensus() = 0;

    virtual int startup () = 0;

    virtual Json::Value getJson (bool full) = 0;

    virtual uint256 getLCL () = 0;

    virtual void mapComplete (uint256 const& hash,
        std::shared_ptr<SHAMap> const& map, bool acquired) = 0;

    virtual void timerEntry () = 0;

    virtual bool peerPosition (LedgerProposal::ref) = 0;

    // test/debug
    virtual void simulate () = 0;
};

using TxSet = std::vector <std::pair <uint256, STTx::pointer>>;

std::shared_ptr <LedgerConsensus>
make_LedgerConsensus (LocalTxs& localtx,
    LedgerHash const & prevLCLHash, Ledger::ref previousLedger,
        std::uint32_t closeTime, FeeVote& feeVote);

std::size_t countLedgerNodes(ripple::Ledger::pointer ledger);

TxSet buildTxSet(std::shared_ptr<SHAMap> const& set);

void
applyTransactions(TxSet const& set, Ledger::ref applyLedger,
                  Ledger::ref checkLedger,
                  CanonicalTXSet& retriableTransactions, bool openLgr);

// Used by tests only
void updateFeeTracking(ripple::Ledger::pointer openLedger, TxSet const& txSet,
    LoadFeeTrack& feeTrack, std::uint64_t refTxnCost, std::function<int()> const& msFunction);

} // ripple

#endif
