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
#include <ripple/app/ledger/OpenLedger.h>
#include <ripple/protocol/digest.h>
#include <ripple/overlay/predicates.h>
#include <ripple/app/misc/AmendmentTable.h>

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

void
RCLConsensus::propose (LedgerProposal const& position)
{
    JLOG (j_.trace()) << "We propose: " <<
        (position.isBowOut () ?  std::string ("bowOut") :
            to_string (position.position ()));

    protocol::TMProposeSet prop;

    prop.set_currenttxhash (position.position().begin(),
        256 / 8);
    prop.set_previousledger (position.prevLedger().begin(),
        256 / 8);
    prop.set_proposeseq (position.proposeSeq());
    prop.set_closetime (
        position.closeTime().time_since_epoch().count());

    prop.set_nodepubkey (valPublic_.data(), valPublic_.size());

    auto signingHash = sha512Half(
        HashPrefix::proposal,
        std::uint32_t(position.proposeSeq()),
        position.closeTime().time_since_epoch().count(),
        position.prevLedger(), position.position());

    auto sig = signDigest (
        valPublic_, valSecret_, signingHash);

    prop.set_signature (sig.data(), sig.size());

    app_.overlay().send(prop);
}

void
RCLConsensus::share (RCLTxSet const& set)
{
    app_.getInboundTransactions().giveSet (set.id(),
        set.map_, false);
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


bool
RCLConsensus::hasOpenTransactions() const
{
    return ! app_.openLedger().empty();
}

int
RCLConsensus::numProposersValidated(LedgerHash const & h) const
{
    return app_.getValidations().getTrustedValidationCount(h);
}

uint256
RCLConsensus::getLCL (
    uint256 const& currentLedger,
    uint256 const& priorLedger,
    bool believedCorrect)
{
    // Get validators that are on our ledger, or "close" to being on
    // our ledger.
    auto vals =
        app_.getValidations().getCurrentValidations(
            currentLedger, priorLedger,
            app_.getLedgerMaster().getValidLedgerIndex());

    uint256 netLgr = currentLedger;
    int netLgrCount = 0;
    for (auto& it : vals)
    {
        if ((it.second.first > netLgrCount) ||
            ((it.second.first == netLgrCount) && (it.first == priorLedger)))
        {
           netLgr = it.first;
           netLgrCount = it.second.first;
        }
    }

    if(netLgr != currentLedger)
    {
      if (believedCorrect)
        app_.getOPs().consensusViewChange();
      if (auto stream = j_.debug())
      {
        for (auto& it : vals)
            stream << "V: " << it.first << ", " << it.second.first;
        stream << getJson (true);
      }
    }

    return netLgr;
}


void
RCLConsensus::onClose(RCLCxLedger const & ledger, bool haveCorrectLCL)
{
    notify(protocol::neCLOSING_LEDGER, ledger, haveCorrectLCL);
}

std::pair <RCLTxSet, LedgerProposal>
RCLConsensus::makeInitialPosition (RCLCxLedger const & prevLedgerT,
        bool proposing,
        bool correctLCL,
        NetClock::time_point closeTime,
        NetClock::time_point now)
{
    auto& ledgerMaster = app_.getLedgerMaster();
    auto const &prevLedger = prevLedgerT.ledger_;
    ledgerMaster.applyHeldTransactions ();

    // Tell the ledger master not to acquire the ledger we're probably building
    ledgerMaster.setBuildingLedger (prevLedger->info().seq + 1);

    auto initialLedger = app_.openLedger().current();

    auto initialSet = std::make_shared <SHAMap> (
        SHAMapType::TRANSACTION, app_.family(), SHAMap::version{1});
    initialSet->setUnbacked ();

    // Build SHAMap containing all transactions in our open ledger
    for (auto const& tx : initialLedger->txs)
    {
        Serializer s (2048);
        tx.first->add(s);
        initialSet->addItem (
            SHAMapItem (tx.first->getTransactionID(), std::move (s)), true, false);
    }

    // Add pseudo-transactions to the set
    if ((app_.config().standalone() || (proposing && correctLCL))
            && ((prevLedger->info().seq % 256) == 0))
    {
        // previous ledger was flag ledger, add pseudo-transactions
        auto const validations =
            app_.getValidations().getValidations (
                prevLedger->info().parentHash);

        auto const count = std::count_if (
            validations.begin(), validations.end(),
            [](auto const& v)
            {
                return v.second->isTrusted();
            });

        if (count >= ledgerMaster.getMinValidations())
        {
            feeVote_->doVoting (
                prevLedger,
                validations,
                initialSet);
            app_.getAmendmentTable ().doVoting (
                prevLedger,
                validations,
                initialSet);
        }
    }

    // Now we need an immutable snapshot
    initialSet = initialSet->snapShot(false);
    auto setHash = initialSet->getHash().as_uint256();

    return std::make_pair<RCLTxSet, LedgerProposal> (
        std::move (initialSet),
        LedgerProposal {
            initialLedger->info().parentHash,
            setHash,
            closeTime,
            now,
            nodeID_ });
}

void
RCLConsensus::notify(
    protocol::NodeEvent ne,
    RCLCxLedger const & ledger,
    bool haveCorrectLCL)
{

    protocol::TMStatusChange s;

    if (!haveCorrectLCL)
        s.set_newevent (protocol::neLOST_SYNC);
    else
    	s.set_newevent(ne);

    s.set_ledgerseq (ledger.seq());
    s.set_networktime (app_.timeKeeper().now().time_since_epoch().count());
    s.set_ledgerhashprevious(ledger.parentID().begin (),
        std::decay_t<decltype(ledger.parentID())>::bytes);
    s.set_ledgerhash (ledger.id().begin (),
        std::decay_t<decltype(ledger.id())>::bytes);

    std::uint32_t uMin, uMax;
    if (! ledgerMaster_.getFullValidatedRange (uMin, uMax))
    {
        uMin = 0;
        uMax = 0;
    }
    else
    {
        // Don't advertise ledgers we're not willing to serve
        std::uint32_t early = ledgerMaster_.getEarliestFetch ();
        if (uMin < early)
           uMin = early;
    }
    s.set_firstseq (uMin);
    s.set_lastseq (uMax);
    app_.overlay ().foreach (send_always (
        std::make_shared <Message> (
            s, protocol::mtSTATUS_CHANGE)));
    JLOG (j_.trace()) << "send status change to peer";
}

}
