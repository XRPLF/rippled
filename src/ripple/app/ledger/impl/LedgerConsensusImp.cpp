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
#include <ripple/app/ledger/InboundLedgers.h>
#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/ledger/LedgerTiming.h>
#include <ripple/app/ledger/LedgerToJson.h>
#include <ripple/app/ledger/LocalTxs.h>
#include <ripple/app/ledger/OpenLedger.h>
#include <ripple/app/ledger/impl/DisputedTx.h>
#include <ripple/app/ledger/impl/LedgerConsensusImp.h>
#include <ripple/app/ledger/impl/TransactionAcquire.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/AmendmentTable.h>
#include <ripple/app/misc/CanonicalTXSet.h>
#include <ripple/app/misc/HashRouter.h>
#include <ripple/app/misc/LoadFeeTrack.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/app/misc/TxQ.h>
#include <ripple/app/misc/Validations.h>
#include <ripple/app/tx/apply.h>
#include <ripple/basics/contract.h>
#include <ripple/basics/CountedObject.h>
#include <ripple/basics/Log.h>
#include <ripple/core/Config.h>
#include <ripple/core/JobQueue.h>
#include <ripple/core/TimeKeeper.h>
#include <ripple/json/to_string.h>
#include <ripple/overlay/Overlay.h>
#include <ripple/overlay/predicates.h>
#include <ripple/protocol/st.h>
#include <ripple/protocol/Feature.h>
#include <ripple/beast/core/LexicalCast.h>
#include <ripple/basics/make_lock.h>
#include <type_traits>

namespace ripple {

LedgerConsensusImp::LedgerConsensusImp (
        Application& app,
        ConsensusImp& consensus,
        InboundTransactions& inboundTransactions,
        LocalTxs& localtx,
        LedgerMaster& ledgerMaster,
        FeeVote& feeVote)
    : app_ (app)
    , consensus_ (consensus)
    , inboundTransactions_ (inboundTransactions)
    , localTX_ (localtx)
    , ledgerMaster_ (ledgerMaster)
    , feeVote_ (feeVote)
    , state_ (State::open)
    , closeTime_ {}
    , valPublic_ (app_.config().VALIDATION_PUB)
    , valSecret_ (app_.config().VALIDATION_PRIV)
    , consensusFail_ (false)
    , roundTime_ (0)
    , closePercent_ (0)
    , closeResolution_ (30)
    , haveCloseTimeConsensus_ (false)
    , consensusStartTime_ (std::chrono::steady_clock::now ())
    , previousProposers_ (0)
    , previousRoundTime_ (0)
    , j_ (app.journal ("LedgerConsensus"))
{
    JLOG (j_.debug()) << "Creating consensus object";
}

Json::Value LedgerConsensusImp::getJson (bool full)
{
    Json::Value ret (Json::objectValue);
    std::lock_guard<std::recursive_mutex> _(lock_);

    ret["proposing"] = proposing_;
    ret["validating"] = validating_;
    ret["proposers"] = static_cast<int> (peerPositions_.size ());

    if (haveCorrectLCL_)
    {
        ret["synched"] = true;
        ret["ledger_seq"] = previousLedger_->info().seq + 1;
        ret["close_granularity"] = closeResolution_.count();
    }
    else
        ret["synched"] = false;

    switch (state_)
    {
    case State::open:
        ret[jss::state] = "open";
        break;

    case State::establish:
        ret[jss::state] = "consensus";
        break;

    case State::processing:
        ret[jss::state] = "processing";
        break;

    case State::accepted:
        ret[jss::state] = "accepted";
        break;
    }

    int v = disputes_.size ();

    if ((v != 0) && !full)
        ret["disputes"] = v;

    if (ourPosition_)
        ret["our_position"] = ourPosition_->getJson ();

    if (full)
    {
        using Int = Json::Value::Int;
        ret["current_ms"] = static_cast<Int>(roundTime_.count());
        ret["close_percent"] = closePercent_;
        ret["close_resolution"] = closeResolution_.count();
        ret["have_timeconsensus_"] = haveCloseTimeConsensus_;
        ret["previous_proposers"] = previousProposers_;
        ret["previous_mseconds"] =
            static_cast<Int>(previousRoundTime_.count());

        if (! peerPositions_.empty ())
        {
            Json::Value ppj (Json::objectValue);

            for (auto& pp : peerPositions_)
            {
                ppj[to_string (pp.first)] = pp.second->getJson ();
            }
            ret["peer_positions"] = std::move(ppj);
        }

        if (! acquired_.empty ())
        {
            // acquired
            Json::Value acq (Json::objectValue);
            for (auto& at : acquired_)
            {
                if (at.second)
                    acq[to_string (at.first)] = "acquired";
                else
                    acq[to_string (at.first)] = "failed";
            }
            ret["acquired"] = std::move(acq);
        }

        if (! disputes_.empty ())
        {
            Json::Value dsj (Json::objectValue);
            for (auto& dt : disputes_)
            {
                dsj[to_string (dt.first)] = dt.second->getJson ();
            }
            ret["disputes"] = std::move(dsj);
        }

        if (! closeTimes_.empty ())
        {
            Json::Value ctj (Json::objectValue);
            for (auto& ct : closeTimes_)
            {
                ctj[std::to_string(ct.first.time_since_epoch().count())] = ct.second;
            }
            ret["close_times"] = std::move(ctj);
        }

        if (! deadNodes_.empty ())
        {
            Json::Value dnj (Json::arrayValue);
            for (auto const& dn : deadNodes_)
            {
                dnj.append (to_string (dn));
            }
            ret["dead_nodes"] = std::move(dnj);
        }
    }

    return ret;
}

uint256 LedgerConsensusImp::getLCL ()
{
    std::lock_guard<std::recursive_mutex> _(lock_);

    return prevLedgerHash_;
}

// Called when:
// 1) We take our initial position
// 2) We take a new position
// 3) We acquire a position a validator took
//
// We store it, notify peers that we have it,
// and update our tracking if any validators currently
// propose it
void LedgerConsensusImp::mapCompleteInternal (
    std::shared_ptr<SHAMap> const& map,
    bool acquired)
{
    auto hash = map->getHash ().as_uint256();

    {
        auto it = acquired_.find (hash);
        if (it != acquired_.end ())
        {
            // We have already acquired (or proven invalid) this transaction set
            if (map && ! it->second)
            {
                JLOG (j_.warn()) << "Map " << hash << " proven invalid then acquired";
                assert (false);
            }
            else if (it->second && ! map)
            {
                JLOG (j_.warn()) << "Map " << hash << " acquired then proven invalid";
                assert (false);
                return;
            }
            else
            {
                // nothing to do
                return;
            }
        }
    }

    if (acquired)
    {
        JLOG (j_.trace()) << "We have acquired txs " << hash;
    }

    if (!map)  // If the map was invalid
    {
        JLOG (j_.warn())
            << "Tried to acquire invalid transaction map: "
            << hash;
        acquired_[hash] = map;
        return;
    }

    assert (hash == map->getHash ().as_uint256());

    // We now have a map that we did not have before

    if (! acquired)
    {
        // Put the map where others can get it
        inboundTransactions_.giveSet (hash, map, false);
    }

    if (ourPosition_ && (! ourPosition_->isBowOut ())
        && (hash != ourPosition_->getCurrentHash ()))
    {
        // this will create disputed transactions
        auto it = acquired_.find (ourPosition_->getCurrentHash ());

        if (it == acquired_.end())
            LogicError ("We cannot find our own position!");

        assert ((it->first == ourPosition_->getCurrentHash ())
            && it->second);
        compares_.insert(hash);
        // Our position is not the same as the acquired position
        createDisputes (it->second, map);
    }
    else if (! ourPosition_)
    {
        JLOG (j_.debug())
            << "Not creating disputes: no position yet.";
    }
    else if (ourPosition_->isBowOut ())
    {
        JLOG (j_.warn())
            << "Not creating disputes: not participating.";
    }
    else
    {
        JLOG (j_.debug())
            << "Not creating disputes: identical position.";
    }

    acquired_[hash] = map;

    // Adjust tracking for each peer that takes this position
    std::vector<NodeID> peers;
    auto const mapHash = map->getHash ().as_uint256();
    for (auto& it : peerPositions_)
    {
        if (it.second->getCurrentHash () == mapHash)
            peers.push_back (it.second->getPeerID ());
    }

    if (!peers.empty ())
    {
        adjustCount (map, peers);
    }
    else if (acquired)
    {
        JLOG (j_.warn())
            << "By the time we got the map " << hash
            << " no peers were proposing it";
    }
}

void LedgerConsensusImp::gotMap (
    std::shared_ptr<SHAMap> const& map)
{
    std::lock_guard<std::recursive_mutex> _(lock_);

    try
    {
        mapCompleteInternal (map, true);
    }
    catch (SHAMapMissingNode const& mn)
    {
        // This should never happen
        leaveConsensus();
        JLOG (j_.error()) <<
            "Missing node processing complete map " << mn;
        Rethrow();
    }
}

void LedgerConsensusImp::checkLCL ()
{
    uint256 netLgr = prevLedgerHash_;
    int netLgrCount = 0;

    uint256 favoredLedger = prevLedgerHash_; // Don't jump forward
    uint256 priorLedger;

    if (haveCorrectLCL_)
        priorLedger = previousLedger_->info().parentHash; // don't jump back

    // Get validators that are on our ledger, or  "close" to being on
    // our ledger.
    hash_map<uint256, ValidationCounter> vals =
        app_.getValidations ().getCurrentValidations(
            favoredLedger, priorLedger,
            ledgerMaster_.getValidLedgerIndex ());

    for (auto& it : vals)
    {
        if ((it.second.first > netLgrCount) ||
            ((it.second.first == netLgrCount) && (it.first == prevLedgerHash_)))
        {
           netLgr = it.first;
           netLgrCount = it.second.first;
        }
    }

    if (netLgr != prevLedgerHash_)
    {
        // LCL change
        const char* status;

        switch (state_)
        {
        case State::open:
            status = "open";
            break;

        case State::establish:
            status = "establish";
            break;

        case State::processing:
            status = "processing";
            break;

        case State::accepted:
            status = "accepted";
            break;

        default:
            status = "unknown";
        }

        JLOG (j_.warn())
            << "View of consensus changed during " << status
            << " (" << netLgrCount << ") status="
            << status << ", "
            << (haveCorrectLCL_ ? "CorrectLCL" : "IncorrectLCL");
        JLOG (j_.warn()) << prevLedgerHash_
            << " to " << netLgr;
        JLOG (j_.warn())
            << ripple::getJson (*previousLedger_);

        if (auto stream = j_.debug())
        {
            for (auto& it : vals)
                stream
                    << "V: " << it.first << ", " << it.second.first;
            stream << getJson (true);
        }

        if (haveCorrectLCL_)
            app_.getOPs ().consensusViewChange ();

        handleLCL (netLgr);
    }
    else if (previousLedger_->info().hash != prevLedgerHash_)
        handleLCL (netLgr);
}

// Handle a change in the LCL during a consensus round
void LedgerConsensusImp::handleLCL (uint256 const& lclHash)
{
    assert (lclHash != prevLedgerHash_ ||
            previousLedger_->info().hash != lclHash);

    if (prevLedgerHash_ != lclHash)
    {
        // first time switching to this ledger
        prevLedgerHash_ = lclHash;

        if (haveCorrectLCL_ && proposing_ && ourPosition_)
        {
            JLOG (j_.info()) << "Bowing out of consensus";
            ourPosition_->bowOut ();
            propose ();
        }

        // Stop proposing because we are out of sync
        proposing_ = false;
        peerPositions_.clear ();
        disputes_.clear ();
        compares_.clear ();
        closeTimes_.clear ();
        deadNodes_.clear ();
        // To get back in sync:
        playbackProposals ();
    }

    if (previousLedger_->info().hash == prevLedgerHash_)
        return;

    // we need to switch the ledger we're working from
    auto buildLCL = ledgerMaster_.getLedgerByHash (prevLedgerHash_);
    if (! buildLCL)
    {
        if (acquiringLedger_ != lclHash)
        {
            // need to start acquiring the correct consensus LCL
            JLOG (j_.warn()) <<
                "Need consensus ledger " << prevLedgerHash_;

            // Tell the ledger acquire system that we need the consensus ledger
            acquiringLedger_ = prevLedgerHash_;

            auto app = &app_;
            auto hash = acquiringLedger_;
            app_.getJobQueue().addJob (
                jtADVANCE, "getConsensusLedger",
                [app, hash] (Job&) {
                    app->getInboundLedgers().acquire(
                        hash, 0, InboundLedger::fcCONSENSUS);
                });

            haveCorrectLCL_ = false;
        }
        return;
    }

    assert (!buildLCL->open() && buildLCL->isImmutable ());
    assert (buildLCL->info().hash == lclHash);
    JLOG (j_.info()) <<
        "Have the consensus ledger " << prevLedgerHash_;
    startRound (
        lclHash,
        buildLCL,
        closeTime_,
        previousProposers_,
        previousRoundTime_);
    proposing_ = false;
}

void LedgerConsensusImp::timerEntry ()
{
    std::lock_guard<std::recursive_mutex> _(lock_);

    try
    {
       if ((state_ != State::processing) && (state_ != State::accepted))
           checkLCL ();

        using namespace std::chrono;
        roundTime_ = duration_cast<milliseconds>
                           (steady_clock::now() - consensusStartTime_);

        closePercent_ = roundTime_ * 100 /
            std::max<milliseconds> (
                previousRoundTime_, AV_MIN_CONSENSUS_TIME);

        switch (state_)
        {
        case State::open:
            statePreClose ();

            if (state_ != State::establish) return;

            // Fall through

        case State::establish:
            stateEstablish ();
            return;

        case State::processing:
            // We are processing the finished ledger
            // logic of calculating next ledger advances us out of this state
            // nothing to do
            return;

        case State::accepted:
            // NetworkOPs needs to setup the next round
            // nothing to do
            return;
        }

        assert (false);
    }
    catch (SHAMapMissingNode const& mn)
    {
        // This should never happen
        leaveConsensus ();
        JLOG (j_.error()) <<
           "Missing node during consensus process " << mn;
        Rethrow();
    }
}

void LedgerConsensusImp::statePreClose ()
{
    // it is shortly before ledger close time
    bool anyTransactions = ! app_.openLedger().empty();
    int proposersClosed = peerPositions_.size ();
    int proposersValidated
        = app_.getValidations ().getTrustedValidationCount
        (prevLedgerHash_);

    // This computes how long since last ledger's close time
    using namespace std::chrono;
    milliseconds sinceClose;
    {
        bool previousCloseCorrect = haveCorrectLCL_
            && getCloseAgree (previousLedger_->info())
            && (previousLedger_->info().closeTime !=
                (previousLedger_->info().parentCloseTime + 1s));

        auto closeTime = previousCloseCorrect
            ? previousLedger_->info().closeTime // use consensus timing
            : consensus_.getLastCloseTime(); // use the time we saw

        auto now = app_.timeKeeper().closeTime();
        if (now >= closeTime)
            sinceClose = now - closeTime;
        else
            sinceClose = -milliseconds{closeTime - now};
    }

    auto const idleInterval = std::max<seconds>(LEDGER_IDLE_INTERVAL,
        2 * previousLedger_->info().closeTimeResolution);

    // Decide if we should close the ledger
    if (shouldCloseLedger (anyTransactions
        , previousProposers_, proposersClosed, proposersValidated
        , previousRoundTime_, sinceClose, roundTime_
        , idleInterval, app_.journal ("LedgerTiming")))
    {
        closeLedger ();
    }
}

void LedgerConsensusImp::stateEstablish ()
{
    // Give everyone a chance to take an initial position
    if (roundTime_ < LEDGER_MIN_CONSENSUS)
        return;

    updateOurPositions ();

    // Nothing to do if we don't have consensus.
    if (!haveConsensus ())
        return;

    if (!haveCloseTimeConsensus_)
    {
        JLOG (j_.info()) <<
            "We have TX consensus but not CT consensus";
        return;
    }

    JLOG (j_.info()) <<
        "Converge cutoff (" << peerPositions_.size () << " participants)";
    state_ = State::processing;
    beginAccept (false);
}

bool LedgerConsensusImp::haveConsensus ()
{
    // CHECKME: should possibly count unacquired TX sets as disagreeing
    int agree = 0, disagree = 0;
    uint256 ourPosition = ourPosition_->getCurrentHash ();

    // Count number of agreements/disagreements with our position
    for (auto& it : peerPositions_)
    {
        if (!it.second->isBowOut ())
        {
            if (it.second->getCurrentHash () == ourPosition)
            {
                ++agree;
            }
            else
            {
                JLOG (j_.debug()) << to_string (it.first)
                    << " has " << to_string (it.second->getCurrentHash ());
                ++disagree;
                if (compares_.count(it.second->getCurrentHash()) == 0)
                { // Make sure we have generated disputes
                    uint256 hash = it.second->getCurrentHash();
                    JLOG (j_.debug())
                        << "We have not compared to " << hash;
                    auto it1 = acquired_.find (hash);
                    auto it2 = acquired_.find(ourPosition_->getCurrentHash ());
                    if ((it1 != acquired_.end()) && (it2 != acquired_.end())
                        && (it1->second) && (it2->second))
                    {
                        compares_.insert(hash);
                        createDisputes(it2->second, it1->second);
                    }
                }
            }
        }
    }
    int currentValidations = app_.getValidations ()
        .getNodesAfter (prevLedgerHash_);

    JLOG (j_.debug())
        << "Checking for TX consensus: agree=" << agree
        << ", disagree=" << disagree;

    // Determine if we actually have consensus or not
    auto ret = checkConsensus (previousProposers_, agree + disagree, agree,
        currentValidations, previousRoundTime_, roundTime_, proposing_,
        app_.journal ("LedgerTiming"));

    if (ret == ConsensusState::No)
        return false;

    // There is consensus, but we need to track if the network moved on
    // without us.
    consensusFail_ = (ret == ConsensusState::MovedOn);

    return true;
}

std::shared_ptr<SHAMap> LedgerConsensusImp::getTransactionTree (
    uint256 const& hash)
{
    std::lock_guard<std::recursive_mutex> _(lock_);

    auto it = acquired_.find (hash);
    if (it != acquired_.end() && it->second)
        return it->second;

    auto set = inboundTransactions_.getSet (hash, true);

    if (set)
        acquired_[hash] = set;

    return set;
}

bool LedgerConsensusImp::peerPosition (LedgerProposal::ref newPosition)
{
    std::lock_guard<std::recursive_mutex> _(lock_);

    auto const peerID = newPosition->getPeerID ();

    if (newPosition->getPrevLedger() != prevLedgerHash_)
    {
        JLOG (j_.debug()) << "Got proposal for "
            << newPosition->getPrevLedger ()
            << " but we are on " << prevLedgerHash_;
        return false;
    }

    if (deadNodes_.find (peerID) != deadNodes_.end ())
    {
        JLOG (j_.info())
            << "Position from dead node: " << to_string (peerID);
        return false;
    }

    LedgerProposal::pointer& currentPosition = peerPositions_[peerID];

    if (currentPosition)
    {
        assert (peerID == currentPosition->getPeerID ());

        if (newPosition->getProposeSeq ()
            <= currentPosition->getProposeSeq ())
        {
            return false;
        }
    }

    if (newPosition->isBowOut ())
    {
        JLOG (j_.info())
            << "Peer bows out: " << to_string (peerID);
        for (auto& it : disputes_)
            it.second->unVote (peerID);
        peerPositions_.erase (peerID);
        deadNodes_.insert (peerID);
        return true;
    }

    if (newPosition->isInitial ())
    {
        // Record the close time estimate
        JLOG (j_.trace())
            << "Peer reports close time as "
            << newPosition->getCloseTime().time_since_epoch().count();
        ++closeTimes_[newPosition->getCloseTime()];
    }

    JLOG (j_.trace()) << "Processing peer proposal "
        << newPosition->getProposeSeq () << "/"
        << newPosition->getCurrentHash ();
    currentPosition = newPosition;

    std::shared_ptr<SHAMap> set
        = getTransactionTree (newPosition->getCurrentHash ());

    if (set)
    {
        for (auto& it : disputes_)
            it.second->setVote (peerID, set->hasItem (it.first));
    }
    else
    {
        JLOG (j_.debug())
            << "Don't have tx set for peer";
    }

    return true;
}

void LedgerConsensusImp::simulate (
    boost::optional<std::chrono::milliseconds> consensusDelay)
{
    std::lock_guard<std::recursive_mutex> _(lock_);

    JLOG (j_.info()) << "Simulating consensus";
    closeLedger ();
    roundTime_ = consensusDelay.value_or(100ms);
    beginAccept (true);
    JLOG (j_.info()) << "Simulation complete";
}

void LedgerConsensusImp::accept (std::shared_ptr<SHAMap> set)
{
    auto closeTime = ourPosition_->getCloseTime();
    bool closeTimeCorrect;

    auto replay = ledgerMaster_.releaseReplay();
    if (replay)
    {
        // replaying, use the time the ledger we're replaying closed
        closeTime = replay->closeTime_;
        closeTimeCorrect = ((replay->closeFlags_ & sLCF_NoConsensusTime) == 0);
    }
    else if (closeTime == NetClock::time_point{})
    {
        // We agreed to disagree on the close time
        closeTime = previousLedger_->info().closeTime + 1s;
        closeTimeCorrect = false;
    }
    else
    {
        // We agreed on a close time
        closeTime = effectiveCloseTime (closeTime);
        closeTimeCorrect = true;
    }

    JLOG (j_.debug())
        << "Report: Prop=" << (proposing_ ? "yes" : "no")
        << " val=" << (validating_ ? "yes" : "no")
        << " corLCL=" << (haveCorrectLCL_ ? "yes" : "no")
        << " fail=" << (consensusFail_ ? "yes" : "no");
    JLOG (j_.debug())
        << "Report: Prev = " << prevLedgerHash_
        << ":" << previousLedger_->info().seq;
    JLOG (j_.debug())
        << "Report: TxSt = " << set->getHash ()
        << ", close " << closeTime.time_since_epoch().count()
        << (closeTimeCorrect ? "" : "X");

    // Put transactions into a deterministic, but unpredictable, order
    CanonicalTXSet retriableTxs (set->getHash ().as_uint256());

    std::shared_ptr<Ledger const> sharedLCL;
    {
        // Build the new last closed ledger
        auto buildLCL = std::make_shared<Ledger>(
            *previousLedger_,
            app_.timeKeeper().closeTime());
        auto const v2_enabled = buildLCL->rules().enabled(featureSHAMapV2,
                                                       app_.config().features);
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
                    applyTransaction (app_, accum, *tx.second, false, tapNO_CHECK_SIGN, j_);
            }
            else
            {
                // Normal case, we are not replaying a ledger close
                retriableTxs = applyTransactions (app_, *set, accum,
                    [&buildLCL](uint256 const& txID)
                    {
                        return ! buildLCL->txExists(txID);
                    });
            }
            // Update fee computations.
            app_.getTxQ().processClosedLedger(app_, accum,
                roundTime_ > 5s);
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
        buildLCL->setAccepted(closeTime, closeResolution_,
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
        sharedLCL = std::move(buildLCL);
    }

    uint256 const newLCLHash = sharedLCL->info().hash;
    JLOG (j_.debug())
        << "Report: NewL  = " << newLCLHash
        << ":" << sharedLCL->info().seq;
    // Tell directly connected peers that we have a new LCL
    statusChange (protocol::neACCEPTED_LEDGER, *sharedLCL);

    if (validating_ &&
        ! ledgerMaster_.isCompatible (*sharedLCL,
            app_.journal("LedgerConsensus").warn(),
            "Not validating"))
    {
        validating_ = false;
    }

    if (validating_ && ! consensusFail_)
    {
        // Build validation
        auto v = std::make_shared<STValidation> (newLCLHash,
            consensus_.validationTimestamp(app_.timeKeeper().now()),
            valPublic_, proposing_);
        v->setFieldU32 (sfLedgerSequence, sharedLCL->info().seq);
        addLoad(v);  // Our network load

        if (((sharedLCL->info().seq + 1) % 256) == 0)
        // next ledger is flag ledger
        {
            // Suggest fee changes and new features
            feeVote_.doValidation (sharedLCL, *v);
            app_.getAmendmentTable ().doValidation (sharedLCL, *v);
        }

        auto const signingHash = v->sign (valSecret_);
        v->setTrusted ();
        // suppress it if we receive it - FIXME: wrong suppression
        app_.getHashRouter ().addSuppression (signingHash);
        app_.getValidations ().addValidation (v, "local");
        consensus_.setLastValidation (v);
        Blob validation = v->getSigned ();
        protocol::TMValidation val;
        val.set_validation (&validation[0], validation.size ());
        // Send signed validation to all of our directly connected peers
        app_.overlay().send(val);
        JLOG (j_.info())
            << "CNF Val " << newLCLHash;
    }
    else
        JLOG (j_.info())
            << "CNF buildLCL " << newLCLHash;

    // See if we can accept a ledger as fully-validated
    ledgerMaster_.consensusBuilt (sharedLCL, getJson (true));

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
            if (!it.second->getOurVote ())
            {
                // we voted NO
                try
                {
                    JLOG (j_.debug())
                        << "Test applying disputed transaction that did"
                        << " not get in";
                    SerialIter sit (it.second->peekTransaction().slice());

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

        auto const localTx = localTX_.getTxSet();
        auto const oldOL = ledgerMaster_.getCurrentLedger();

        auto const lastVal = ledgerMaster_.getValidatedLedger();
        boost::optional<Rules> rules;
        if (lastVal)
            rules.emplace(*lastVal);
        else
            rules.emplace();
        app_.openLedger().accept(app_, *rules,
            sharedLCL, localTx, anyDisputes, retriableTxs, tapNONE,
                "consensus",
                    [&](OpenView& view, beast::Journal j)
                    {
                        // Stuff the ledger with transactions from the queue.
                        return app_.getTxQ().accept(app_, view);
                    });
    }

    ledgerMaster_.switchLCL (sharedLCL);

    assert (ledgerMaster_.getClosedLedger()->info().hash == sharedLCL->info().hash);
    assert (app_.openLedger().current()->info().parentHash == sharedLCL->info().hash);

    if (validating_)
    {
        // see how close our close time is to other node's
        //  close time reports, and update our clock.
        JLOG (j_.info())
            << "We closed at " << closeTime_.time_since_epoch().count();
        using usec64_t = std::chrono::duration<std::uint64_t>;
        usec64_t closeTotal = closeTime_.time_since_epoch();
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
            closeTotal += usec64_t(p.first.time_since_epoch()) * p.second;
        }

        closeTotal += usec64_t(closeCount / 2);  // for round to nearest
        closeTotal /= closeCount;
        using duration = std::chrono::duration<std::int32_t>;
        using time_point = std::chrono::time_point<NetClock, duration>;
        auto offset = time_point{closeTotal} -
                      std::chrono::time_point_cast<duration>(closeTime_);
        JLOG (j_.info())
            << "Our close offset is estimated at "
            << offset.count() << " (" << closeCount << ")";
        app_.timeKeeper().adjustCloseTime(offset);
    }

    // we have accepted a new ledger
    bool correct;
    {
        std::lock_guard<std::recursive_mutex> _(lock_);

        state_ = State::accepted;
        correct = haveCorrectLCL_;
    }

    endConsensus (correct);
}

void LedgerConsensusImp::createDisputes (
    std::shared_ptr<SHAMap> const& m1,
    std::shared_ptr<SHAMap> const& m2)
{
    if (m1->getHash() == m2->getHash())
        return;

    JLOG (j_.debug()) << "createDisputes "
        << m1->getHash() << " to " << m2->getHash();
    SHAMap::Delta differences;
    m1->compare (*m2, differences, 16384);

    int dc = 0;
    // for each difference between the transactions
    for (auto& pos : differences)
    {
        ++dc;
        // create disputed transactions (from the ledger that has them)
        if (pos.second.first)
        {
            // transaction is only in first map
            assert (!pos.second.second);
            addDisputedTransaction (pos.first
                , pos.second.first->peekData ());
        }
        else if (pos.second.second)
        {
            // transaction is only in second map
            assert (!pos.second.first);
            addDisputedTransaction (pos.first
                , pos.second.second->peekData ());
        }
        else // No other disagreement over a transaction should be possible
            assert (false);
    }
    JLOG (j_.debug()) << dc << " differences found";
}

void LedgerConsensusImp::addDisputedTransaction (
    uint256 const& txID,
    Blob const& tx)
{
    if (disputes_.find (txID) != disputes_.end ())
        return;

    JLOG (j_.debug()) << "Transaction "
        << txID << " is disputed";

    bool ourVote = false;

    // Update our vote on the disputed transaction
    if (ourPosition_)
    {
        auto mit (acquired_.find (ourPosition_->getCurrentHash ()));

        if (mit != acquired_.end ())
            ourVote = mit->second->hasItem (txID);
        else
            assert (false); // We don't have our own position?
    }

    auto txn = std::make_shared<DisputedTx> (txID, tx, ourVote, j_);
    disputes_[txID] = txn;

    // Update all of the peer's votes on the disputed transaction
    for (auto& pit : peerPositions_)
    {
        auto cit (acquired_.find (pit.second->getCurrentHash ()));

        if ((cit != acquired_.end ()) && cit->second)
        {
            txn->setVote (pit.first, cit->second->hasItem (txID));
        }
    }

    // If we didn't relay this transaction recently, relay it to all peers
    if (app_.getHashRouter ().shouldRelay (txID))
    {
        protocol::TMTransaction msg;
        msg.set_rawtransaction (& (tx.front ()), tx.size ());
        msg.set_status (protocol::tsNEW);
        msg.set_receivetimestamp (
            app_.timeKeeper().now().time_since_epoch().count());
        app_.overlay ().foreach (send_always (
            std::make_shared<Message> (
                msg, protocol::mtTRANSACTION)));
    }
}

void LedgerConsensusImp::adjustCount (std::shared_ptr<SHAMap> const& map,
                  const std::vector<NodeID>& peers)
{
    for (auto& it : disputes_)
    {
        bool setHas = map->hasItem (it.second->getTransactionID ());
        for (auto const& pit : peers)
            it.second->setVote (pit, setHas);
    }
}

void LedgerConsensusImp::leaveConsensus ()
{
    if (proposing_)
    {
        if (ourPosition_ && ! ourPosition_->isBowOut ())
        {
            ourPosition_->bowOut();
            propose();
        }
        proposing_ = false;
    }
}

void LedgerConsensusImp::propose ()
{
    JLOG (j_.trace()) << "We propose: " <<
        (ourPosition_->isBowOut ()
            ? std::string ("bowOut")
            : to_string (ourPosition_->getCurrentHash ()));
    protocol::TMProposeSet prop;

    prop.set_currenttxhash (ourPosition_->getCurrentHash ().begin ()
        , 256 / 8);
    prop.set_previousledger (ourPosition_->getPrevLedger ().begin ()
        , 256 / 8);
    prop.set_proposeseq (ourPosition_->getProposeSeq ());
    prop.set_closetime(ourPosition_->getCloseTime().time_since_epoch().count());

    prop.set_nodepubkey (valPublic_.data(), valPublic_.size());

    ourPosition_->setSignature (
        signDigest (
            valPublic_,
            valSecret_,
            ourPosition_->getSigningHash()));

    auto sig = ourPosition_->getSignature();
    prop.set_signature (sig.data(), sig.size());

    app_.overlay().send(prop);
}

void LedgerConsensusImp::statusChange (
    protocol::NodeEvent event, ReadView const& ledger)
{
    protocol::TMStatusChange s;

    if (!haveCorrectLCL_)
        s.set_newevent (protocol::neLOST_SYNC);
    else
        s.set_newevent (event);

    s.set_ledgerseq (ledger.info().seq);
    s.set_networktime (app_.timeKeeper().now().time_since_epoch().count());
    s.set_ledgerhashprevious(ledger.info().parentHash.begin (),
        std::decay_t<decltype(ledger.info().parentHash)>::bytes);
    s.set_ledgerhash (ledger.info().hash.begin (),
        std::decay_t<decltype(ledger.info().hash)>::bytes);

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

// For the consensus refactor, takeInitialPosition has been split
// into two pieces. This piece, makeInitialPosition does the
// non-consensus parts
std::shared_ptr<SHAMap> LedgerConsensusImp::makeInitialPosition ()
{
    // Tell the ledger master not to acquire the ledger we're probably building
    ledgerMaster_.setBuildingLedger (previousLedger_->info().seq + 1);

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

    if ((app_.config().standalone() || (proposing_ && haveCorrectLCL_))
            && ((previousLedger_->info().seq % 256) == 0))
    {
        // previous ledger was flag ledger, add pseudo-transactions
        auto const validations =
            app_.getValidations().getValidations (
                previousLedger_->info().parentHash);

        auto const count = std::count_if (
            validations.begin(), validations.end(),
            [](auto const& v)
            {
                return v.second->isTrusted();
            });

        if (count >= ledgerMaster_.getMinValidations())
        {
            feeVote_.doVoting (
                previousLedger_,
                validations,
                initialSet);
            app_.getAmendmentTable ().doVoting (
                previousLedger_,
                validations,
                initialSet);
        }
    }

    // Set should be immutable snapshot
    return initialSet->snapShot (false);
}

void LedgerConsensusImp::takeInitialPosition()
{
    auto initialSet = makeInitialPosition();

    mapCompleteInternal (initialSet, false);

    ourPosition_ = std::make_shared<LedgerProposal> (
        previousLedger_->info().hash,
        initialSet->getHash().as_uint256(),
        closeTime_);

    for (auto& it : disputes_)
    {
        it.second->setOurVote (initialSet->hasItem (it.first));
    }

    // if any peers have taken a contrary position, process disputes
    hash_set<uint256> found;

    for (auto& it : peerPositions_)
    {
        uint256 const& set = it.second->getCurrentHash ();

        if (found.insert (set).second)
        {
            auto iit (acquired_.find (set));

            if (iit != acquired_.end ())
            {
                compares_.insert(iit->second->getHash().as_uint256());
                createDisputes (initialSet, iit->second);
            }
        }
    }

    if (proposing_)
        propose ();
}

/** How many of the participants must agree to reach a given threshold?

    Note that the number may not precisely yield the requested percentage.
    For example, with with size = 5 and percent = 70, we return 3, but
    3 out of 5 works out to 60%. There are no security implications to
    this.

    @param participants the number of participants (i.e. validators)
    @param the percent that we want to reach

    @return the number of participants which must agree
*/
static
int
participantsNeeded (int participants, int percent)
{
    int result = ((participants * percent) + (percent / 2)) / 100;

    return (result == 0) ? 1 : result;
}

NetClock::time_point
LedgerConsensusImp::effectiveCloseTime(NetClock::time_point closeTime)
{
    if (closeTime == NetClock::time_point{})
        return closeTime;

    return std::max<NetClock::time_point>(
        roundCloseTime (closeTime, closeResolution_),
        (previousLedger_->info().closeTime + 1s));
}

void LedgerConsensusImp::updateOurPositions ()
{
    // Compute a cutoff time
    auto peerCutoff
        = std::chrono::steady_clock::now ();
    auto ourCutoff
        = peerCutoff - PROPOSE_INTERVAL;
    peerCutoff -= PROPOSE_FRESHNESS;

    bool changes = false;
    std::shared_ptr<SHAMap> ourPosition;
    //  std::vector<uint256> addedTx, removedTx;

    // Verify freshness of peer positions and compute close times
    std::map<NetClock::time_point, int> closeTimes;
    {
        auto it = peerPositions_.begin ();
        while (it != peerPositions_.end ())
        {
            if (it->second->isStale (peerCutoff))
            {
                // peer's proposal is stale, so remove it
                auto const& peerID = it->second->getPeerID ();
                JLOG (j_.warn())
                    << "Removing stale proposal from " << peerID;
                for (auto& dt : disputes_)
                    dt.second->unVote (peerID);
                it = peerPositions_.erase (it);
            }
            else
            {
                // proposal is still fresh
                ++closeTimes[effectiveCloseTime(it->second->getCloseTime())];
                ++it;
            }
        }
    }

    // Update votes on disputed transactions
    for (auto& it : disputes_)
    {
        // Because the threshold for inclusion increases,
        //  time can change our position on a dispute
        if (it.second->updateVote (closePercent_, proposing_))
        {
            if (!changes)
            {
                ourPosition = acquired_[ourPosition_->getCurrentHash ()]
                    ->snapShot (true);
                assert (ourPosition);
                changes = true;
            }

            if (it.second->getOurVote ()) // now a yes
            {
                ourPosition->addItem (SHAMapItem (it.first
                    , it.second->peekTransaction ()), true, false);
                //              addedTx.push_back(it.first);
            }
            else // now a no
            {
                ourPosition->delItem (it.first);
                //              removedTx.push_back(it.first);
            }
        }
    }

    int neededWeight;

    if (closePercent_ < AV_MID_CONSENSUS_TIME)
        neededWeight = AV_INIT_CONSENSUS_PCT;
    else if (closePercent_ < AV_LATE_CONSENSUS_TIME)
        neededWeight = AV_MID_CONSENSUS_PCT;
    else if (closePercent_ < AV_STUCK_CONSENSUS_TIME)
        neededWeight = AV_LATE_CONSENSUS_PCT;
    else
        neededWeight = AV_STUCK_CONSENSUS_PCT;

    NetClock::time_point closeTime = {};
    haveCloseTimeConsensus_ = false;

    if (peerPositions_.empty ())
    {
        // no other times
        haveCloseTimeConsensus_ = true;
        closeTime = effectiveCloseTime(ourPosition_->getCloseTime());
    }
    else
    {
        int participants = peerPositions_.size ();
        if (proposing_)
        {
            ++closeTimes[effectiveCloseTime(ourPosition_->getCloseTime())];
            ++participants;
        }

        // Threshold for non-zero vote
        int threshVote = participantsNeeded (participants,
            neededWeight);

        // Threshold to declare consensus
        int const threshConsensus = participantsNeeded (
            participants, AV_CT_CONSENSUS_PCT);

        JLOG (j_.info()) << "Proposers:"
            << peerPositions_.size () << " nw:" << neededWeight
            << " thrV:" << threshVote << " thrC:" << threshConsensus;

        for (auto const& it : closeTimes)
        {
            JLOG (j_.debug()) << "CCTime: seq "
                << previousLedger_->info().seq + 1 << ": "
                << it.first.time_since_epoch().count()
                << " has " << it.second << ", "
                << threshVote << " required";

            if (it.second >= threshVote)
            {
                // A close time has enough votes for us to try to agree
                closeTime = it.first;
                threshVote = it.second;

                if (threshVote >= threshConsensus)
                    haveCloseTimeConsensus_ = true;
            }
        }

        if (!haveCloseTimeConsensus_)
        {
            JLOG (j_.debug()) << "No CT consensus:"
                << " Proposers:" << peerPositions_.size ()
                << " Proposing:" << (proposing_ ? "yes" : "no")
                << " Thresh:" << threshConsensus
                << " Pos:" << closeTime.time_since_epoch().count();
        }
    }

    // Temporarily send a new proposal if there's any change to our
    // claimed close time. Once the new close time code is deployed
    // to the full network, this can be relaxed to force a change
    // only if the rounded close time has changed.
    if (!changes &&
            ((closeTime != ourPosition_->getCloseTime())
            || ourPosition_->isStale (ourCutoff)))
    {
        // close time changed or our position is stale
        ourPosition = acquired_[ourPosition_->getCurrentHash ()]
            ->snapShot (true);
        assert (ourPosition);
        changes = true; // We pretend our position changed to force
    }                   //   a new proposal

    if (changes)
    {
        ourPosition = ourPosition->snapShot (false);

        auto newHash = ourPosition->getHash ().as_uint256();
        JLOG (j_.info())
            << "Position change: CTime "
            << closeTime.time_since_epoch().count()
            << ", tx " << newHash;

        if (ourPosition_->changePosition(newHash, closeTime))
        {
            if (proposing_)
                propose ();

            mapCompleteInternal (ourPosition, false);
        }
    }
}

void LedgerConsensusImp::playbackProposals ()
{
    auto proposals = consensus_.getStoredProposals (prevLedgerHash_);

    for (auto& proposal : proposals)
    {
        if (peerPosition (proposal))
        {
            // Now that we know this proposal
            // is useful, relay it
            protocol::TMProposeSet prop;

            prop.set_proposeseq (
                proposal->getProposeSeq ());
            prop.set_closetime (
                proposal->getCloseTime ().time_since_epoch().count());

            prop.set_currenttxhash (
                proposal->getCurrentHash().begin(), 256 / 8);
            prop.set_previousledger (
                proposal->getPrevLedger().begin(), 256 / 8);

            auto const pk = proposal->getPublicKey().slice();
            prop.set_nodepubkey (pk.data(), pk.size());

            auto const sig = proposal->getSignature();
            prop.set_signature (sig.data(), sig.size());

            app_.overlay().relay (
                prop, proposal->getSuppressionID ());
        }
    }
}

void LedgerConsensusImp::closeLedger ()
{
    checkOurValidation ();
    state_ = State::establish;
    consensusStartTime_ = std::chrono::steady_clock::now ();
    closeTime_ = app_.timeKeeper().closeTime();
    consensus_.setLastCloseTime(closeTime_);
    statusChange (protocol::neCLOSING_LEDGER, *previousLedger_);
    ledgerMaster_.applyHeldTransactions ();
    takeInitialPosition ();
}

void LedgerConsensusImp::checkOurValidation ()
{
    // This only covers some cases - Fix for the case where we can't ever
    // acquire the consensus ledger
    if (! haveCorrectLCL_ || ! valPublic_.size ()
        || app_.getOPs ().isNeedNetworkLedger ())
    {
        return;
    }

    auto lastValidation = consensus_.getLastValidation ();

    if (lastValidation)
    {
        if (lastValidation->getFieldU32 (sfLedgerSequence)
            == previousLedger_->info().seq)
        {
            return;
        }
        if (lastValidation->getLedgerHash () == prevLedgerHash_)
            return;
    }

    auto v = std::make_shared<STValidation> (previousLedger_->info().hash,
        consensus_.validationTimestamp(app_.timeKeeper().now()),
        valPublic_, false);
    addLoad(v);
    v->setTrusted ();
    auto const signingHash = v->sign (valSecret_);
        // FIXME: wrong supression
    app_.getHashRouter ().addSuppression (signingHash);
    app_.getValidations ().addValidation (v, "localMissing");
    Blob validation = v->getSigned ();
    protocol::TMValidation val;
    val.set_validation (&validation[0], validation.size ());
    consensus_.setLastValidation (v);
    JLOG (j_.warn()) << "Sending partial validation";
}

void LedgerConsensusImp::beginAccept (bool synchronous)
{
    auto consensusSet = acquired_[ourPosition_->getCurrentHash ()];

    if (!consensusSet)
    {
        JLOG (j_.fatal())
            << "We don't have a consensus set";
        abort ();
    }

    consensus_.newLCL (peerPositions_.size (), roundTime_);

    if (synchronous)
        accept (consensusSet);
    else
    {
        app_.getJobQueue().addJob (jtACCEPT, "acceptLedger",
            std::bind (&LedgerConsensusImp::accept, shared_from_this (),
                       consensusSet));
    }
}

void LedgerConsensusImp::endConsensus (bool correctLCL)
{
    app_.getOPs ().endConsensus (correctLCL);
}

void LedgerConsensusImp::startRound (
    LedgerHash const& prevLCLHash,
    std::shared_ptr<Ledger const> const& prevLedger,
    NetClock::time_point closeTime,
    int previousProposers,
    std::chrono::milliseconds previousConvergeTime)
{
    std::lock_guard<std::recursive_mutex> _(lock_);

    if (state_ == State::processing)
    {
        // We can't start a new round while we're processing
        return;
    }

    state_ = State::open;
    closeTime_ = closeTime;
    prevLedgerHash_ = prevLCLHash;
    previousLedger_ = prevLedger;
    ourPosition_.reset();
    consensusFail_ = false;
    roundTime_ = 0ms;
    closePercent_ = 0;
    haveCloseTimeConsensus_ = false;
    consensusStartTime_ = std::chrono::steady_clock::now();
    previousProposers_ = previousProposers;
    previousRoundTime_ = previousConvergeTime;
    inboundTransactions_.newRound (previousLedger_->info().seq);

    peerPositions_.clear();
    acquired_.clear();
    disputes_.clear();
    compares_.clear();
    closeTimes_.clear();
    deadNodes_.clear();

    closeResolution_ = getNextLedgerTimeResolution (
        previousLedger_->info().closeTimeResolution,
        getCloseAgree (previousLedger_->info()),
        previousLedger_->info().seq + 1);

    if (valPublic_.size () && ! app_.getOPs ().isNeedNetworkLedger ())
    {
        // If the validation keys were set, and if we need a ledger,
        // then we want to validate, and possibly propose a ledger.
        JLOG (j_.info())
            << "Entering consensus process, validating";
        validating_ = true;
        // Propose if we are in sync with the network
        proposing_ =
            app_.getOPs ().getOperatingMode () == NetworkOPs::omFULL;
    }
    else
    {
        // Otherwise we just want to monitor the validation process.
        JLOG (j_.info())
            << "Entering consensus process, watching";
        proposing_ = validating_ = false;
    }

    haveCorrectLCL_ = (previousLedger_->info().hash == prevLedgerHash_);

    if (! haveCorrectLCL_)
    {
        // If we were not handed the correct LCL, then set our state
        // to not proposing.
        consensus_.setProposing (false, false);
        handleLCL (prevLedgerHash_);

        if (! haveCorrectLCL_)
        {
            JLOG (j_.info())
                << "Entering consensus with: "
                << previousLedger_->info().hash;
            JLOG (j_.info())
                << "Correct LCL is: " << prevLCLHash;
        }
    }
    else
    {
        // update the network status table as to whether we're
        // proposing/validating
        consensus_.setProposing (proposing_, validating_);
    }

    playbackProposals ();
    if (peerPositions_.size() > (previousProposers_ / 2))
    {
        // We may be falling behind, don't wait for the timer
        // consider closing the ledger immediately
        timerEntry ();
    }

}

void LedgerConsensusImp::addLoad(STValidation::ref val)
{
    auto const& feeTrack = app_.getFeeTrack();
    std::uint32_t fee = std::max(
        feeTrack.getLocalFee(),
        feeTrack.getClusterFee());

    if (fee > feeTrack.getLoadBase())
        val->setFieldU32(sfLoadFee, fee);
}

//------------------------------------------------------------------------------
std::shared_ptr <LedgerConsensus>
make_LedgerConsensus (
    Application& app,
    ConsensusImp& consensus,
    InboundTransactions& inboundTransactions,
    LocalTxs& localtx,
    LedgerMaster& ledgerMaster,
    FeeVote& feeVote)
{
    return std::make_shared <LedgerConsensusImp> (app, consensus,
        inboundTransactions, localtx, ledgerMaster, feeVote);
}

//------------------------------------------------------------------------------

CanonicalTXSet
applyTransactions (
    Application& app,
    SHAMap const& set,
    OpenView& view,
    std::function<bool(uint256 const&)> txFilter)
{
    auto j = app.journal ("LedgerConsensus");

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

} // ripple
