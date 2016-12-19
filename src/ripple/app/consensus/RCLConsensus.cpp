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
#include <ripple/app/consensus/RCLConsensus.h>
#include <ripple/app/ledger/InboundTransactions.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/ledger/InboundLedgers.h>
#include <ripple/overlay/Overlay.h>

namespace ripple {


RCLConsensus::RCLConsensus(
        Application& app,
        std::unique_ptr<FeeVote> && feeVote,
        LedgerMaster& ledgerMaster,
        LocalTxs& localTxs,
        InboundTransactions& inboundTransactions,
        typename Base::clock_type const & clock,
        beast::Journal journal)
        : Base(clock, journal)
        , app_ (app)
        , feeVote_ (std::move(feeVote))
        , ledgerMaster_ (ledgerMaster)
        , localTxs_(localTxs)
        , inboundTransactions_{ inboundTransactions }
        , j_ (journal)
        , nodeID_{ calcNodeID(app.nodeIdentity().first) }
        , valPublic_ (app_.config().VALIDATION_PUB)
        , valSecret_ (app_.config().VALIDATION_PRIV)
{

}

void
RCLConsensus::onStartRound(RCLCxLedger const & ledger)
{
    inboundTransactions_.newRound(ledger.seq());
}

// First bool is whether or not we can propose
// Second bool is whether or not we can validate
std::pair <bool, bool>
RCLConsensus::getMode ()
{
    bool propose = false;
    bool validate = false;

    if (! app_.getOPs().isNeedNetworkLedger() && (valPublic_.size() != 0))
    {
        // We have a key, and we have some idea what the ledger is
        validate = true;

        // propose only if we're in sync with the network
        propose = app_.getOPs().getOperatingMode() == NetworkOPs::omFULL;
    }
    return { propose, validate };
}

boost::optional<RCLCxLedger>
RCLConsensus::acquireLedger(LedgerHash const & ledger)
{

    // we need to switch the ledger we're working from
    auto buildLCL = ledgerMaster_.getLedgerByHash(ledger);
    if (! buildLCL)
    {
        if (acquiringLedger_ != ledger)
        {
            // need to start acquiring the correct consensus LCL
            JLOG (j_.warn()) <<
                "Need consensus ledger " << ledger;

            // Tell the ledger acquire system that we need the consensus ledger
            acquiringLedger_ = ledger;

            auto app = &app_;
            auto hash = acquiringLedger_;
            app_.getJobQueue().addJob (
                jtADVANCE, "getConsensusLedger",
                [app, hash] (Job&) {
                    app->getInboundLedgers().acquire(
                        hash, 0, InboundLedger::fcCONSENSUS);
                });
        }
        return boost::none;
    }

    assert (!buildLCL->open() && buildLCL->isImmutable ());
    assert (buildLCL->info().hash == ledger);

    return RCLCxLedger(buildLCL);
}


std::vector<LedgerProposal>
RCLConsensus::proposals (LedgerHash const& prevLedger)
{
    std::vector <LedgerProposal> ret;
    {
        std::lock_guard <std::mutex> _(proposalsLock_);

        for (auto const& it : proposals_)
            for (auto const& prop : it.second)
                if (prop->prevLedger() == prevLedger)
                    ret.emplace_back (*prop);
    }

    return ret;
}

void
RCLConsensus::storeProposal (
    LedgerProposal::ref proposal,
    NodeID const& nodeID)
{
    std::lock_guard <std::mutex> _(proposalsLock_);

    auto& props = proposals_[nodeID];

    if (props.size () >= 10)
        props.pop_front ();

    props.push_back (proposal);
}

void
RCLConsensus::relay(LedgerProposal const & proposal)
{
    protocol::TMProposeSet prop;

    prop.set_proposeseq (
        proposal.proposeSeq ());
    prop.set_closetime (
        proposal.closeTime ().time_since_epoch().count());

    prop.set_currenttxhash (
        proposal.position().begin(), 256 / 8);
    prop.set_previousledger (
        proposal.prevLedger().begin(), 256 / 8);

    auto const pk = proposal.getPublicKey().slice();
    prop.set_nodepubkey (pk.data(), pk.size());

    auto const sig = proposal.getSignature();
    prop.set_signature (sig.data(), sig.size());

    app_.overlay().relay (prop, proposal.getSuppressionID ());
}


boost::optional<RCLTxSet>
RCLConsensus::acquireTxSet(LedgerProposal const & position)
{
    if (auto set = inboundTransactions_.getSet(position.position(), true))
    {
        return RCLTxSet{std::move(set)};
    }
    return boost::none;
}

}
