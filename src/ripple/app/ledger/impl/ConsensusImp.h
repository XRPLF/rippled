
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

#ifndef RIPPLE_APP_LEDGER_IMPL_CONSENSUSIMP_H_INCLUDED
#define RIPPLE_APP_LEDGER_IMPL_CONSENSUSIMP_H_INCLUDED

#include <BeastConfig.h>
#include <ripple/app/ledger/Consensus.h>
#include <ripple/app/ledger/LedgerConsensus.h>
#include <ripple/app/misc/FeeVote.h>
#include <ripple/basics/Log.h>
#include <ripple/protocol/STValidation.h>
#include <ripple/shamap/SHAMap.h>
#include <ripple/beast/utility/Journal.h>

namespace ripple {

/** Implements the consensus process and provides inter-round state. */
class ConsensusImp
    : public Consensus
{
public:
    ConsensusImp (FeeVote::Setup const& voteSetup, Logs& logs);

    ~ConsensusImp () = default;

    bool
    isProposing () const override;

    bool
    isValidating () const override;

    int
    getLastCloseProposers () const override;

    std::chrono::milliseconds
    getLastCloseDuration () const override;

    std::shared_ptr<LedgerConsensus<RCLCxTraits>>
    makeLedgerConsensus (
        Application& app,
        InboundTransactions& inboundTransactions,
        LedgerMaster& ledgerMaster,
        LocalTxs& localTxs) override;

    void
    startRound (
        NetClock::time_point now,
        LedgerConsensus<RCLCxTraits>& ledgerConsensus,
        LedgerHash const& prevLCLHash,
        std::shared_ptr<Ledger const> const& previousLedger) override;

    void
    setLastCloseTime (NetClock::time_point t) override;

    void
    storeProposal (
        LedgerProposal::ref proposal,
        NodeID const& nodeID) override;

    void
    setProposing (bool p, bool v);

    STValidation::ref
    getLastValidation () const;

    void
    setLastValidation (STValidation::ref v);

    void
    newLCL (
        int proposers,
        std::chrono::milliseconds convergeTime);

    NetClock::time_point
    validationTimestamp (NetClock::time_point vt);

    NetClock::time_point
    getLastCloseTime () const;

    std::vector <RCLCxPos>
    getStoredProposals (uint256 const& previousLedger);

private:
    beast::Journal journal_;
    std::unique_ptr <FeeVote> feeVote_;

    bool proposing_;
    bool validating_;

    // A pointer to the last validation that we issued
    STValidation::pointer lastValidation_;

    // The number of proposers who participated in the last ledger close
    int lastCloseProposers_;

    // How long the last ledger close took, in milliseconds
    std::chrono::milliseconds lastCloseConvergeTook_;

    // The timestamp of the last validation we used, in network time. This is
    // only used for our own validations.
    NetClock::time_point lastValidationTimestamp_;

    // The last close time
    NetClock::time_point lastCloseTime_;

    Consensus::Proposals storedProposals_;

    // lock to protect storedProposals_
    std::mutex lock_;
};

}

#endif
