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
#include <ripple/beast/core/LexicalCast.h>
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
#include <ripple/app/misc/HashRouter.h>
#include <ripple/consensus/LedgerTiming.h>
#include <ripple/basics/make_lock.h>
#include <ripple/app/ledger/LocalTxs.h>
#include <ripple/app/misc/TxQ.h>
#include <ripple/app/tx/apply.h>
#include <ripple/protocol/Feature.h>
#include <ripple/app/misc/LoadFeeTrack.h>

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


std::vector<RCLCxPeerPos>
RCLConsensus::proposals (LedgerHash const& prevLedger)
{
    std::vector <RCLCxPeerPos> ret;
    {
        std::lock_guard <std::mutex> _(peerPositionsLock_);

        for (auto const& it : peerPositions_)
            for (auto const& pos : it.second)
                if (pos->proposal().prevLedger() == prevLedger)
                    ret.emplace_back (*pos);
    }

    return ret;
}

void
RCLConsensus::storeProposal (
    RCLCxPeerPos::ref peerPos,
    NodeID const& nodeID)
{
    std::lock_guard <std::mutex> _(peerPositionsLock_);

    auto& props = peerPositions_[nodeID];

    if (props.size () >= 10)
        props.pop_front ();

    props.push_back (peerPos);
}

void
RCLConsensus::relay(RCLCxPeerPos const & peerPos)
{
    protocol::TMProposeSet prop;

    auto const & proposal = peerPos.proposal();

    prop.set_proposeseq (
        proposal.proposeSeq ());
    prop.set_closetime (
        proposal.closeTime ().time_since_epoch().count());

    prop.set_currenttxhash (
        proposal.position().begin(), proposal.position().size());
    prop.set_previousledger (
        proposal.prevLedger().begin(), proposal.position().size());

    auto const pk = peerPos.getPublicKey().slice();
    prop.set_nodepubkey (pk.data(), pk.size());

    auto const sig = peerPos.getSignature();
    prop.set_signature (sig.data(), sig.size());

    app_.overlay().relay (prop, peerPos.getSuppressionID ());
}

void
RCLConsensus::relay(DisputedTx <RCLCxTx, NodeID> const & dispute)
{
     // If we didn't relay this transaction recently, relay it to all peers
    auto const & tx = dispute.tx();
    if (app_.getHashRouter ().shouldRelay (tx.id()))
    {
        auto const slice = tx.tx_.slice();

        protocol::TMTransaction msg;
        msg.set_rawtransaction (slice.data(), slice.size());
        msg.set_status (protocol::tsNEW);
        msg.set_receivetimestamp (
            app_.timeKeeper().now().time_since_epoch().count());
        app_.overlay ().foreach (send_always (
            std::make_shared<Message> (
                msg, protocol::mtTRANSACTION)));
    }
}
void
RCLConsensus::propose (RCLCxPeerPos::Proposal const& proposal)
{
    JLOG (j_.trace()) << "We propose: " <<
        (proposal.isBowOut () ?  std::string ("bowOut") :
            to_string (proposal.position ()));

    protocol::TMProposeSet prop;

    prop.set_currenttxhash (proposal.position().begin(),
        proposal.position().size());
    prop.set_previousledger (proposal.prevLedger().begin(),
        proposal.position().size());
    prop.set_proposeseq (proposal.proposeSeq());
    prop.set_closetime (
        proposal.closeTime().time_since_epoch().count());

    prop.set_nodepubkey (valPublic_.data(), valPublic_.size());

    auto signingHash = sha512Half(
        HashPrefix::proposal,
        std::uint32_t(proposal.proposeSeq()),
        proposal.closeTime().time_since_epoch().count(),
        proposal.prevLedger(), proposal.position());

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
RCLConsensus::acquireTxSet(RCLTxSet::ID const & setId)
{
    if (auto set = inboundTransactions_.getSet(setId, true))
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

int
RCLConsensus::numProposersFinished(LedgerHash const & h) const
{
    return app_.getValidations().getNodesAfter(h);
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

std::pair <RCLTxSet, typename RCLCxPeerPos::Proposal>
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

    return std::make_pair<RCLTxSet, RCLCxPeerPos::Proposal> (
        std::move (initialSet),
        RCLCxPeerPos::Proposal {
            initialLedger->info().parentHash,
            RCLCxPeerPos::Proposal::seqJoin,
            setHash,
            closeTime,
            now,
            nodeID_ });
}

void
RCLConsensus::dispatchAccept(RCLTxSet const & txSet)
{
    app_.getJobQueue().addJob(jtACCEPT, "acceptLedger",
        [that = this->shared_from_this(),
        consensusSet = txSet]
        (auto &)
        {
            that->accept(consensusSet);
        });
}

bool
RCLConsensus::accept(
    RCLTxSet const& set,
    NetClock::time_point consensusCloseTime,
    bool proposing_,
    bool validating_,
    bool haveCorrectLCL_,
    bool consensusFail_,
    LedgerHash const &prevLedgerHash_,
    RCLCxLedger const & previousLedger_,
    NetClock::duration closeResolution_,
    NetClock::time_point const & now_,
    std::chrono::milliseconds const & roundTime_,
    hash_map<RCLCxTx::ID, DisputedTx <RCLCxTx, NodeID>> const & disputes_,
    std::map <NetClock::time_point, int> closeTimes_,
    NetClock::time_point const & closeTime_,
    Json::Value && json
    )
{
    bool closeTimeCorrect;

    if (consensusCloseTime == NetClock::time_point{})
    {
        // We agreed to disagree on the close time
        consensusCloseTime = previousLedger_.closeTime() + 1s;
        closeTimeCorrect = false;
    }
    else
    {
        // We agreed on a close time

        consensusCloseTime = effectiveCloseTime(consensusCloseTime,
            closeResolution_, previousLedger_.closeTime());

        closeTimeCorrect = true;
    }

    JLOG (j_.debug())
        << "Report: Prop=" << (proposing_ ? "yes" : "no")
        << " val=" << (validating_ ? "yes" : "no")
        << " corLCL=" << (haveCorrectLCL_ ? "yes" : "no")
        << " fail=" << (consensusFail_ ? "yes" : "no");
    JLOG (j_.debug())
        << "Report: Prev = " << prevLedgerHash_
        << ":" << previousLedger_.seq();

    //--------------------------------------------------------------------------
    // Put transactions into a deterministic, but unpredictable, order
    CanonicalTXSet retriableTxs{ set.id() };

    auto sharedLCL
            = buildLCL(previousLedger_, set,
                     consensusCloseTime, closeTimeCorrect,
                     closeResolution_, now_, roundTime_, retriableTxs);


    auto const newLCLHash = sharedLCL.id();
    JLOG (j_.debug())
        << "Report: NewL  = " << newLCLHash
        << ":" << sharedLCL.seq();

    // Tell directly connected peers that we have a new LCL
    notify (protocol::neACCEPTED_LEDGER, sharedLCL, haveCorrectLCL_);

    if (validating_)
        validating_ = ledgerMaster_.isCompatible(*sharedLCL.ledger_,
        app_.journal("LedgerConsensus").warn(),
        "Not validating");

    if (validating_ && ! consensusFail_)
    {
        validate(sharedLCL, now_, proposing_);
        JLOG (j_.info())
            << "CNF Val " << newLCLHash;
    }
    else
        JLOG (j_.info())
            << "CNF buildLCL " << newLCLHash;

    // See if we can accept a ledger as fully-validated
    ledgerMaster_.consensusBuilt (sharedLCL.ledger_, std::move(json));

    //-------------------------------------------------------------------------
    {
        // Apply disputed transactions that didn't get in
        //
        // The first crack of transactions to get into the new
        // open ledger goes to transactions proposed by a validator
        // we trust but not included in the consensus set.
        //
        // These are done first because they are the most likely
        // to receive agreement during consensus. They are also
        // ordered logically "sooner" than transactions not mentioned
        // in the previous consensus round.
        //
        bool anyDisputes = false;
        for (auto& it : disputes_)
        {
            if (!it.second.getOurVote ())
            {
                // we voted NO
                try
                {
                    JLOG (j_.debug())
                        << "Test applying disputed transaction that did"
                        << " not get in";

                    SerialIter sit (it.second.tx().tx_.slice());
                    auto txn = std::make_shared<STTx const>(sit);
                    retriableTxs.insert (txn);

                    anyDisputes = true;
                }
                catch (std::exception const&)
                {
                    JLOG (j_.debug())
                        << "Failed to apply transaction we voted NO on";
                }
            }
        }

        // Build new open ledger
        auto lock = make_lock(
            app_.getMasterMutex(), std::defer_lock);
        auto sl = make_lock(
            ledgerMaster_.peekMutex (), std::defer_lock);
        std::lock(lock, sl);

        auto const lastVal = ledgerMaster_.getValidatedLedger();
        boost::optional<Rules> rules;
        if (lastVal)
            rules.emplace(*lastVal, app_.config().features);
        else
            rules.emplace(app_.config().features);
        app_.openLedger().accept(app_, *rules,
            sharedLCL.ledger_, localTxs_.getTxSet(), anyDisputes, retriableTxs, tapNONE,
                "consensus",
                    [&](OpenView& view, beast::Journal j)
                    {
                        // Stuff the ledger with transactions from the queue.
                        return app_.getTxQ().accept(app_, view);
                    });

    }

    //-------------------------------------------------------------------------
    {
        ledgerMaster_.switchLCL (sharedLCL.ledger_);

        // Do these need to exist?
        assert (ledgerMaster_.getClosedLedger()->info().hash == sharedLCL.id());
        assert (app_.openLedger().current()->info().parentHash == sharedLCL.id());
    }

    //-------------------------------------------------------------------------
    if (haveCorrectLCL_ && ! consensusFail_)
    {
        // we entered the round with the network,
        // see how close our close time is to other node's
        //  close time reports, and update our clock.
        JLOG (j_.info())
            << "We closed at " << closeTime_.time_since_epoch().count();
        using usec64_t = std::chrono::duration<std::uint64_t>;
        usec64_t closeTotal = std::chrono::duration_cast<usec64_t>(closeTime_.time_since_epoch());
        int closeCount = 1;

        for (auto const& p : closeTimes_)
        {
            // FIXME: Use median, not average
            JLOG (j_.info())
                << beast::lexicalCastThrow <std::string> (p.second)
                << " time votes for "
                << beast::lexicalCastThrow <std::string>
                       (p.first.time_since_epoch().count());
            closeCount += p.second;
            closeTotal += std::chrono::duration_cast<usec64_t>(p.first.time_since_epoch()) * p.second;
        }

        closeTotal += usec64_t(closeCount / 2);  // for round to nearest
        closeTotal /= closeCount;
        using duration = typename NetClock::duration;

        auto offset = NetClock::time_point{closeTotal} -
                      std::chrono::time_point_cast<duration>(closeTime_);
        JLOG (j_.info())
            << "Our close offset is estimated at "
            << offset.count() << " (" << closeCount << ")";

        app_.timeKeeper().adjustCloseTime(offset);
    }

    return validating_;
}

void
RCLConsensus::endConsensus(bool correctLCL)
{
    app_.getOPs ().endConsensus (correctLCL);
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


/** Apply a set of transactions to a ledger.

  Typically the txFilter is used to reject transactions
  that already accepted in the prior ledger.

  @param set            set of transactions to apply
  @param view           ledger to apply to
  @param txFilter       callback, return false to reject txn
  @return               retriable transactions
*/

CanonicalTXSet
applyTransactions (
    Application& app,
    RCLTxSet const& cSet,
    OpenView& view,
    std::function<bool(uint256 const&)> txFilter)
{
    auto j = app.journal ("LedgerConsensus");

    auto& set = *(cSet.map_);
    CanonicalTXSet retriableTxs (set.getHash().as_uint256());


    for (auto const& item : set)
    {
        if (! txFilter (item.key()))
            continue;

        // The transaction wan't filtered
        // Add it to the set to be tried in canonical order
        JLOG (j.debug()) <<
            "Processing candidate transaction: " << item.key();
        try
        {
            retriableTxs.insert (
                std::make_shared<STTx const>(SerialIter{item.slice()}));
        }
        catch (std::exception const&)
        {
            JLOG (j.warn()) << "Txn " << item.key() << " throws";
        }
    }

    bool certainRetry = true;
    // Attempt to apply all of the retriable transactions
    for (int pass = 0; pass < LEDGER_TOTAL_PASSES; ++pass)
    {
        JLOG (j.debug()) << "Pass: " << pass << " Txns: "
            << retriableTxs.size ()
            << (certainRetry ? " retriable" : " final");
        int changes = 0;

        auto it = retriableTxs.begin ();

        while (it != retriableTxs.end ())
        {
            try
            {
                switch (applyTransaction (app, view,
                    *it->second, certainRetry, tapNO_CHECK_SIGN, j))
                {
                case ApplyResult::Success:
                    it = retriableTxs.erase (it);
                    ++changes;
                    break;

                case ApplyResult::Fail:
                    it = retriableTxs.erase (it);
                    break;

                case ApplyResult::Retry:
                    ++it;
                }
            }
            catch (std::exception const&)
            {
                JLOG (j.warn())
                    << "Transaction throws";
                it = retriableTxs.erase (it);
            }
        }

        JLOG (j.debug()) << "Pass: "
            << pass << " finished " << changes << " changes";

        // A non-retry pass made no changes
        if (!changes && !certainRetry)
            return retriableTxs;

        // Stop retriable passes
        if (!changes || (pass >= LEDGER_RETRY_PASSES))
            certainRetry = false;
    }

    // If there are any transactions left, we must have
    // tried them in at least one final pass
    assert (retriableTxs.empty() || !certainRetry);
    return retriableTxs;
}

RCLCxLedger
RCLConsensus::buildLCL(
    RCLCxLedger const & previousLedger,
    RCLTxSet const & set,
    NetClock::time_point closeTime,
    bool closeTimeCorrect,
    NetClock::duration closeResolution,
    NetClock::time_point now,
    std::chrono::milliseconds roundTime,
    CanonicalTXSet & retriableTxs)
{
    auto replay = ledgerMaster_.releaseReplay();
    if (replay)
    {
        // replaying, use the time the ledger we're replaying closed
        closeTime = replay->closeTime_;
        closeTimeCorrect = ((replay->closeFlags_ & sLCF_NoConsensusTime) == 0);
    }

    JLOG (j_.debug())
        << "Report: TxSt = " << set.id ()
        << ", close " << closeTime.time_since_epoch().count()
        << (closeTimeCorrect ? "" : "X");


    // Build the new last closed ledger
    auto buildLCL = std::make_shared<Ledger>(*previousLedger.ledger_, now);

    auto const v2_enabled = buildLCL->rules().enabled(featureSHAMapV2);

    auto v2_transition = false;
    if (v2_enabled && !buildLCL->stateMap().is_v2())
    {
        buildLCL->make_v2();
        v2_transition = true;
    }

    // Set up to write SHAMap changes to our database,
    //   perform updates, extract changes
    JLOG (j_.debug())
        << "Applying consensus set transactions to the"
        << " last closed ledger";

    {
        OpenView accum(&*buildLCL);
        assert(!accum.open());
        if (replay)
        {
            // Special case, we are replaying a ledger close
            for (auto& tx : replay->txns_)
                applyTransaction (app_, accum, *tx.second,
                                 false, tapNO_CHECK_SIGN, j_);
        }
        else
        {
            // Normal case, we are not replaying a ledger close
            retriableTxs = applyTransactions (app_, set, accum,
                [&buildLCL](uint256 const& txID)
                {
                    return ! buildLCL->txExists(txID);
                });
        }
        // Update fee computations.
        app_.getTxQ().processClosedLedger(app_, accum,
            roundTime > 5s);
        accum.apply(*buildLCL);
    }

    // retriableTxs will include any transactions that
    // made it into the consensus set but failed during application
    // to the ledger.

    buildLCL->updateSkipList ();

    {
        // Write the final version of all modified SHAMap
        // nodes to the node store to preserve the new LCL

        int asf = buildLCL->stateMap().flushDirty (
            hotACCOUNT_NODE, buildLCL->info().seq);
        int tmf = buildLCL->txMap().flushDirty (
            hotTRANSACTION_NODE, buildLCL->info().seq);
        JLOG (j_.debug()) << "Flushed " <<
            asf << " accounts and " <<
            tmf << " transaction nodes";
    }
    buildLCL->unshare();

    // Accept ledger
    buildLCL->setAccepted(closeTime, closeResolution,
                        closeTimeCorrect, app_.config());

    // And stash the ledger in the ledger master
    if (ledgerMaster_.storeLedger (buildLCL))
        JLOG (j_.debug())
            << "Consensus built ledger we already had";
    else if (app_.getInboundLedgers().find (buildLCL->info().hash))
        JLOG (j_.debug())
            << "Consensus built ledger we were acquiring";
    else
        JLOG (j_.debug())
            << "Consensus built new ledger";
    return RCLCxLedger{std::move(buildLCL)};

}

void
RCLConsensus::validate(
    RCLCxLedger const & ledger,
    NetClock::time_point now,
    bool proposing)
{
    auto validationTime = now;
    if (validationTime <= lastValidationTime_)
        validationTime = lastValidationTime_ + 1s;
    lastValidationTime_ = validationTime;

    // Build validation
    auto v = std::make_shared<STValidation> (ledger.id(),
        validationTime, valPublic_, proposing);
    v->setFieldU32 (sfLedgerSequence, ledger.seq());

    // Add our load fee to the validation
    auto const& feeTrack = app_.getFeeTrack();
    std::uint32_t fee = std::max(
        feeTrack.getLocalFee(),
        feeTrack.getClusterFee());

    if (fee > feeTrack.getLoadBase())
        v->setFieldU32(sfLoadFee, fee);

    if (((ledger.seq() + 1) % 256) == 0)
    // next ledger is flag ledger
    {
        // Suggest fee changes and new features
        feeVote_->doValidation (ledger.ledger_, *v);
        app_.getAmendmentTable ().doValidation (ledger.ledger_, *v);
    }

    auto const signingHash = v->sign (valSecret_);
    v->setTrusted ();
    // suppress it if we receive it - FIXME: wrong suppression
    app_.getHashRouter ().addSuppression (signingHash);
    app_.getValidations ().addValidation (v, "local");
    Blob validation = v->getSerialized ();
    protocol::TMValidation val;
    val.set_validation (&validation[0], validation.size ());
    // Send signed validation to all of our directly connected peers
    app_.overlay().send(val);
}

}
