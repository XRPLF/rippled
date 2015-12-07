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
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/app/misc/TxQ.h>
#include <ripple/app/misc/Validations.h>
#include <ripple/app/tx/apply.h>
#include <ripple/basics/contract.h>
#include <ripple/basics/CountedObject.h>
#include <ripple/basics/Log.h>
#include <ripple/core/Config.h>
#include <ripple/core/JobQueue.h>
#include <ripple/core/LoadFeeTrack.h>
#include <ripple/core/TimeKeeper.h>
#include <ripple/json/to_string.h>
#include <ripple/overlay/Overlay.h>
#include <ripple/overlay/predicates.h>
#include <ripple/protocol/st.h>
#include <ripple/protocol/Feature.h>
#include <beast/module/core/text/LexicalCast.h>
#include <beast/utility/make_lock.h>
#include <type_traits>

namespace ripple {

/** Determines whether the current ledger should close at this time.

    This function should be called when a ledger is open and there is no close
    in progress, or when a transaction is received and no close is in progress.

    @param anyTransactions indicates whether any transactions have been received
    @param previousProposers proposers in the last closing
    @param proposersClosed proposers who have currently closed this ledger
    @param proposersValidated proposers who have validated the last closed
                              ledger
    @param previousMSeconds time, in milliseconds, for the previous ledger to
                            reach consensus (in milliseconds)
    @param currentMSeconds time, in milliseconds, since the previous ledger's
                           (possibly rounded) close time
    @param openMSeconds time, in milliseconds, waiting to close this ledger
    @param idleInterval the network's desired idle interval
*/
bool shouldCloseLedger (
    bool anyTransactions,
    int previousProposers,
    int proposersClosed,
    int proposersValidated,
    int previousMSeconds,
    int currentMSeconds, // Time since last ledger's close time
    int openMSeconds,    // Time waiting to close this ledger
    int idleInterval,
    beast::Journal j)
{
    if ((previousMSeconds < -1000) || (previousMSeconds > 600000) ||
        (currentMSeconds > 600000))
    {
        // These are unexpected cases, we just close the ledger
        JLOG (j.warning) <<
            "shouldCloseLedger Trans=" << (anyTransactions ? "yes" : "no") <<
            " Prop: " << previousProposers << "/" << proposersClosed <<
            " Secs: " << currentMSeconds << " (last: " <<
            previousMSeconds << ")";
        return true;
    }

    if ((proposersClosed + proposersValidated) > (previousProposers / 2))
    {
        // If more than half of the network has closed, we close
        JLOG (j.trace) << "Others have closed";
        return true;
    }

    if (!anyTransactions)
    {
        // Only close at the end of the idle interval
        return currentMSeconds >= (idleInterval * 1000); // normal idle
    }

    // Preserve minimum ledger open time
    if (openMSeconds < LEDGER_MIN_CLOSE)
    {
        JLOG (j.debug) <<
            "Must wait minimum time before closing";
        return false;
    }

    // Don't let this ledger close more than twice as fast as the previous
    // ledger reached consensus so that slower validators can slow down
    // the network
    if (openMSeconds < (previousMSeconds / 2))
    {
        JLOG (j.debug) <<
            "Ledger has not been open long enough";
        return false;
    }

    // Close the ledger
    return true;
}

bool
checkConsensusReached (int agreeing, int proposing)
{
    int currentPercentage = (agreeing * 100) / (proposing + 1);

    return currentPercentage > minimumConsensusPercentage;
}

/** What state the consensus process is on. */
enum class ConsensusState
{
    No,           // We do not have consensus
    MovedOn,      // The network has consensus without us
    Yes           // We have consensus along with the network
};

/** Determine whether the network reached consensus and whether we joined.

    @param previousProposers proposers in the last closing (not including us)
    @param currentProposers proposers in this closing so far (not including us)
    @param currentAgree proposers who agree with us
    @param currentFinished proposers who have validated a ledger after this one
    @param previousAgreeTime how long, in milliseconds, it took to agree on the
                             last ledger
    @param currentAgreeTime how long, in milliseconds, we've been trying to
                            agree
*/
ConsensusState checkConsensus (
    int previousProposers,
    int currentProposers,
    int currentAgree,
    int currentFinished,
    int previousAgreeTime,
    int currentAgreeTime,
    beast::Journal j)
{
    JLOG (j.trace) <<
        "checkConsensus: prop=" << currentProposers <<
        "/" << previousProposers <<
        " agree=" << currentAgree << " validated=" << currentFinished <<
        " time=" << currentAgreeTime <<  "/" << previousAgreeTime;

    if (currentAgreeTime <= LEDGER_MIN_CONSENSUS)
        return ConsensusState::No;

    if (currentProposers < (previousProposers * 3 / 4))
    {
        // Less than 3/4 of the last ledger's proposers are present; don't
        // rush: we may need more time.
        if (currentAgreeTime < (previousAgreeTime + LEDGER_MIN_CONSENSUS))
        {
            JLOG (j.trace) <<
                "too fast, not enough proposers";
            return ConsensusState::No;
        }
    }

    // Have we, together with the nodes on our UNL list, reached the treshold
    // to declare consensus?
    if (checkConsensusReached (currentAgree + 1, currentProposers))
    {
        JLOG (j.debug) << "normal consensus";
        return ConsensusState::Yes;
    }

    // Have sufficient nodes on our UNL list moved on and reached the threshold
    // to declare consensus?
    if (checkConsensusReached (currentFinished, currentProposers))
    {
        JLOG (j.warning) <<
            "We see no consensus, but 80% of nodes have moved on";
        return ConsensusState::MovedOn;
    }

    // no consensus yet
    JLOG (j.trace) << "no consensus";
    return ConsensusState::No;
}

LedgerConsensusImp::LedgerConsensusImp (
        Application& app,
        ConsensusImp& consensus,
        int previousProposers,
        int previousConvergeTime,
        InboundTransactions& inboundTransactions,
        LocalTxs& localtx,
        LedgerMaster& ledgerMaster,
        LedgerHash const & prevLCLHash,
        Ledger::ref previousLedger,
        std::uint32_t closeTime,
        FeeVote& feeVote)
    : app_ (app)
    , consensus_ (consensus)
    , inboundTransactions_ (inboundTransactions)
    , m_localTX (localtx)
    , ledgerMaster_ (ledgerMaster)
    , m_feeVote (feeVote)
    , state_ (State::open)
    , mCloseTime (closeTime)
    , mPrevLedgerHash (prevLCLHash)
    , mPreviousLedger (previousLedger)
    , mValPublic (app_.config().VALIDATION_PUB)
    , mValPrivate (app_.config().VALIDATION_PRIV)
    , mConsensusFail (false)
    , mCurrentMSeconds (0)
    , mClosePercent (0)
    , mHaveCloseTimeConsensus (false)
    , mConsensusStartTime (std::chrono::steady_clock::now ())
    , mPreviousProposers (previousProposers)
    , mPreviousMSeconds (previousConvergeTime)
    , j_ (app.journal ("LedgerConsensus"))
{
    JLOG (j_.debug) << "Creating consensus object";
    JLOG (j_.trace)
        << "LCL:" << previousLedger->getHash () << ", ct=" << closeTime;

    assert (mPreviousMSeconds);

    inboundTransactions_.newRound (mPreviousLedger->info().seq);

    // Adapt close time resolution to recent network conditions
    mCloseResolution = getNextLedgerTimeResolution (
        mPreviousLedger->info().closeTimeResolution,
        getCloseAgree (mPreviousLedger->info()),
        mPreviousLedger->info().seq + 1);

    if (mValPublic.isSet () && mValPrivate.isSet ()
        && !app_.getOPs ().isNeedNetworkLedger ())
    {
        // If the validation keys were set, and if we need a ledger,
        // then we want to validate, and possibly propose a ledger.
        JLOG (j_.info)
            << "Entering consensus process, validating";
        mValidating = true;
        // Propose if we are in sync with the network
        mProposing =
            app_.getOPs ().getOperatingMode () == NetworkOPs::omFULL;
    }
    else
    {
        // Otherwise we just want to monitor the validation process.
        JLOG (j_.info)
            << "Entering consensus process, watching";
        mProposing = mValidating = false;
    }

    mHaveCorrectLCL = (mPreviousLedger->getHash () == mPrevLedgerHash);

    if (!mHaveCorrectLCL)
    {
        // If we were not handed the correct LCL, then set our state
        // to not proposing.
        consensus_.setProposing (false, false);
        handleLCL (mPrevLedgerHash);

        if (!mHaveCorrectLCL)
        {
            //          mProposing = mValidating = false;
            JLOG (j_.info)
                << "Entering consensus with: "
                << previousLedger->getHash ();
            JLOG (j_.info)
                << "Correct LCL is: " << prevLCLHash;
        }
    }
    else
        // update the network status table as to whether we're
        // proposing/validating
        consensus_.setProposing (mProposing, mValidating);

    playbackProposals ();
    if (mPeerPositions.size() > (mPreviousProposers / 2))
    {
        // We may be falling behind, don't wait for the timer
        // consider closing the ledger immediately
        timerEntry ();
    }
}

Json::Value LedgerConsensusImp::getJson (bool full)
{
    Json::Value ret (Json::objectValue);
    ret["proposing"] = mProposing;
    ret["validating"] = mValidating;
    ret["proposers"] = static_cast<int> (mPeerPositions.size ());

    if (mHaveCorrectLCL)
    {
        ret["synched"] = true;
        ret["ledger_seq"] = mPreviousLedger->info().seq + 1;
        ret["close_granularity"] = mCloseResolution;
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

    case State::finished:
        ret[jss::state] = "finished";
        break;

    case State::accepted:
        ret[jss::state] = "accepted";
        break;
    }

    int v = mDisputes.size ();

    if ((v != 0) && !full)
        ret["disputes"] = v;

    if (mOurPosition)
        ret["our_position"] = mOurPosition->getJson ();

    if (full)
    {

        ret["current_ms"] = mCurrentMSeconds;
        ret["close_percent"] = mClosePercent;
        ret["close_resolution"] = mCloseResolution;
        ret["have_time_consensus"] = mHaveCloseTimeConsensus;
        ret["previous_proposers"] = mPreviousProposers;
        ret["previous_mseconds"] = mPreviousMSeconds;

        if (!mPeerPositions.empty ())
        {
            Json::Value ppj (Json::objectValue);

            for (auto& pp : mPeerPositions)
            {
                ppj[to_string (pp.first)] = pp.second->getJson ();
            }
            ret["peer_positions"] = ppj;
        }

        if (!mAcquired.empty ())
        {
            // acquired
            Json::Value acq (Json::objectValue);
            for (auto& at : mAcquired)
            {
                if (at.second)
                    acq[to_string (at.first)] = "acquired";
                else
                    acq[to_string (at.first)] = "failed";
            }
            ret["acquired"] = acq;
        }

        if (!mDisputes.empty ())
        {
            Json::Value dsj (Json::objectValue);
            for (auto& dt : mDisputes)
            {
                dsj[to_string (dt.first)] = dt.second->getJson ();
            }
            ret["disputes"] = dsj;
        }

        if (!mCloseTimes.empty ())
        {
            Json::Value ctj (Json::objectValue);
            for (auto& ct : mCloseTimes)
            {
                ctj[std::to_string(ct.first)] = ct.
                second;
            }
            ret["close_times"] = ctj;
        }

        if (!mDeadNodes.empty ())
        {
            Json::Value dnj (Json::arrayValue);
            for (auto const& dn : mDeadNodes)
            {
                dnj.append (to_string (dn));
            }
            ret["dead_nodes"] = dnj;
        }
    }

    return ret;
}

uint256 LedgerConsensusImp::getLCL ()
{
    return mPrevLedgerHash;
}

void LedgerConsensusImp::mapCompleteInternal (
    uint256 const& hash,
    std::shared_ptr<SHAMap> const& map,
    bool acquired)
{
    CondLog (acquired, lsDEBUG, LedgerConsensus)
        << "We have acquired TXS " << hash;

    if (!map)  // If the map was invalid
    {
        // this is an invalid/corrupt map
        mAcquired[hash] = map;
        JLOG (j_.warning)
            << "A trusted node directed us to acquire an invalid TXN map";
        return;
    }

    assert (hash == map->getHash ().as_uint256());

    auto it = mAcquired.find (hash);

    // If we have already acquired this transaction set
    if (it != mAcquired.end ())
    {
        if (it->second)
        {
            return; // we already have this map
        }

        // We previously failed to acquire this map, now we have it
        mAcquired.erase (hash);
    }

    // We now have a map that we did not have before

    if (!acquired)
    {
        // Put the map where others can get it
        inboundTransactions_.giveSet (hash, map, false);
    }

    // Inform directly-connected peers that we have this transaction set
    sendHaveTxSet (hash, true);

    if (mOurPosition && (!mOurPosition->isBowOut ())
        && (hash != mOurPosition->getCurrentHash ()))
    {
        // this will create disputed transactions
        auto it2 = mAcquired.find (mOurPosition->getCurrentHash ());

        if (it2 != mAcquired.end ())
        {
            assert ((it2->first == mOurPosition->getCurrentHash ())
                && it2->second);
            mCompares.insert(hash);
            // Our position is not the same as the acquired position
            createDisputes (it2->second, map);
        }
        else
            assert (false); // We don't have our own position?!
    }
    else if (!mOurPosition)
        JLOG (j_.debug)
            << "Not creating disputes: no position yet.";
    else if (mOurPosition->isBowOut ())
        JLOG (j_.warning)
            << "Not creating disputes: not participating.";
    else
        JLOG (j_.debug)
            << "Not creating disputes: identical position.";

    mAcquired[hash] = map;

    // Adjust tracking for each peer that takes this position
    std::vector<NodeID> peers;
    for (auto& it : mPeerPositions)
    {
        if (it.second->getCurrentHash () == map->getHash ().as_uint256())
            peers.push_back (it.second->getPeerID ());
    }

    if (!peers.empty ())
    {
        adjustCount (map, peers);
    }
    else
    {
        CondLog (acquired, lsWARNING, LedgerConsensus)
            << "By the time we got the map "
            << hash << " no peers were proposing it";
    }
}

void LedgerConsensusImp::mapComplete (
    uint256 const& hash,
    std::shared_ptr<SHAMap> const& map,
    bool acquired)
{
    try
    {
        mapCompleteInternal (hash, map, acquired);
    }
    catch (SHAMapMissingNode const& mn)
    {
        leaveConsensus();
        JLOG (j_.error) <<
            "Missing node processing complete map " << mn;
        Throw();
    }
}

void LedgerConsensusImp::checkLCL ()
{
    uint256 netLgr = mPrevLedgerHash;
    int netLgrCount = 0;

    uint256 favoredLedger = mPrevLedgerHash; // Don't jump forward
    uint256 priorLedger;

    if (mHaveCorrectLCL)
        priorLedger = mPreviousLedger->info().parentHash; // don't jump back

    // Get validators that are on our ledger, or  "close" to being on
    // our ledger.
    hash_map<uint256, ValidationCounter> vals =
        app_.getValidations ().getCurrentValidations(
            favoredLedger, priorLedger,
            ledgerMaster_.getValidLedgerIndex ());

    for (auto& it : vals)
    {
        if ((it.second.first > netLgrCount) ||
            ((it.second.first == netLgrCount) && (it.first == mPrevLedgerHash)))
        {
           netLgr = it.first;
           netLgrCount = it.second.first;
        }
    }

    if (netLgr != mPrevLedgerHash)
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

        case State::finished:
            status = "finished";
            break;

        case State::accepted:
            status = "accepted";
            break;

        default:
            status = "unknown";
        }

        JLOG (j_.warning)
            << "View of consensus changed during " << status
            << " (" << netLgrCount << ") status="
            << status << ", "
            << (mHaveCorrectLCL ? "CorrectLCL" : "IncorrectLCL");
        JLOG (j_.warning) << mPrevLedgerHash
            << " to " << netLgr;
        JLOG (j_.warning)
            << ripple::getJson (*mPreviousLedger);

        if (j_.debug)
        {
            for (auto& it : vals)
                j_.debug
                    << "V: " << it.first << ", " << it.second.first;
            j_.debug << getJson (true);
        }

        if (mHaveCorrectLCL)
            app_.getOPs ().consensusViewChange ();

        handleLCL (netLgr);
    }
    else if (mPreviousLedger->getHash () != mPrevLedgerHash)
        handleLCL (netLgr);
}

void LedgerConsensusImp::handleLCL (uint256 const& lclHash)
{
    assert (lclHash != mPrevLedgerHash ||
            mPreviousLedger->getHash () != lclHash);

    if (mPrevLedgerHash != lclHash)
    {
        // first time switching to this ledger
        mPrevLedgerHash = lclHash;

        if (mHaveCorrectLCL && mProposing && mOurPosition)
        {
            JLOG (j_.info) << "Bowing out of consensus";
            mOurPosition->bowOut ();
            propose ();
        }

        // Stop proposing because we are out of sync
        mProposing = false;
        mPeerPositions.clear ();
        mDisputes.clear ();
        mCloseTimes.clear ();
        mDeadNodes.clear ();
        // To get back in sync:
        playbackProposals ();
    }

    if (mPreviousLedger->getHash () == mPrevLedgerHash)
        return;

    // we need to switch the ledger we're working from
    auto newLCL = ledgerMaster_.getLedgerByHash (mPrevLedgerHash);
    if (!newLCL)
    {
        if (mAcquiringLedger != lclHash)
        {
            // need to start acquiring the correct consensus LCL
            JLOG (j_.warning) <<
                "Need consensus ledger " << mPrevLedgerHash;

            // Tell the ledger acquire system that we need the consensus ledger
            mAcquiringLedger = mPrevLedgerHash;

            auto app = &app_;
            auto hash = mAcquiringLedger;
            app_.getJobQueue().addJob (
                jtADVANCE, "getConsensusLedger",
                [app, hash] (Job&) {
                    app->getInboundLedgers().acquire(
                        hash, 0, InboundLedger::fcCONSENSUS);
                });

            mHaveCorrectLCL = false;
        }
        return;
    }

    assert (!newLCL->info().open && newLCL->isImmutable ());
    assert (newLCL->getHash () == lclHash);
    mPreviousLedger = newLCL;
    mPrevLedgerHash = lclHash;

    JLOG (j_.info) <<
        "Have the consensus ledger " << mPrevLedgerHash;
    mHaveCorrectLCL = true;

    mCloseResolution = getNextLedgerTimeResolution (
        mPreviousLedger->info().closeTimeResolution,
        getCloseAgree(mPreviousLedger->info()),
        mPreviousLedger->info().seq + 1);
}

void LedgerConsensusImp::timerEntry ()
{
    try
    {
       if ((state_ != State::finished) && (state_ != State::accepted))
            checkLCL ();

        mCurrentMSeconds = std::chrono::duration_cast<std::chrono::milliseconds>
            (std::chrono::steady_clock::now() - mConsensusStartTime).count ();
        mClosePercent = mCurrentMSeconds * 100 / mPreviousMSeconds;

        switch (state_)
        {
        case State::open:
            statePreClose ();
            return;

        case State::establish:
            stateEstablish ();

            if (state_ != State::finished) return;

            // Fall through

        case State::finished:
            stateFinished ();

            if (state_ != State::accepted) return;

            // Fall through

        case State::accepted:
            stateAccepted ();
            return;
        }

        assert (false);
    }
    catch (SHAMapMissingNode const& mn)
    {
        leaveConsensus ();
        JLOG (j_.error) <<
           "Missing node during consensus process " << mn;
        Throw();
    }
}

void LedgerConsensusImp::statePreClose ()
{
    // it is shortly before ledger close time
    bool anyTransactions = ! app_.openLedger().empty();
    int proposersClosed = mPeerPositions.size ();
    int proposersValidated
        = app_.getValidations ().getTrustedValidationCount
        (mPrevLedgerHash);

    // This computes how long since last ledger's close time
    int sinceClose;
    {
        bool previousCloseCorrect = mHaveCorrectLCL
            && getCloseAgree (mPreviousLedger->info())
            && (mPreviousLedger->info().closeTime !=
                (mPreviousLedger->info().parentCloseTime + 1));

        auto closeTime = previousCloseCorrect
            ? mPreviousLedger->info().closeTime // use consensus timing
            : consensus_.getLastCloseTime(); // use the time we saw

        auto now =
            app_.timeKeeper().closeTime().time_since_epoch().count();
        if (now >= closeTime)
            sinceClose = static_cast<int> (1000 * (now - closeTime));
        else
            sinceClose = - static_cast<int> (1000 * (closeTime - now));
    }

    auto const idleInterval = std::max (LEDGER_IDLE_INTERVAL,
        2 * mPreviousLedger->info().closeTimeResolution);

    // Decide if we should close the ledger
    if (shouldCloseLedger (anyTransactions
        , mPreviousProposers, proposersClosed, proposersValidated
        , mPreviousMSeconds, sinceClose, mCurrentMSeconds
        , idleInterval, app_.journal ("LedgerTiming")))
    {
        closeLedger ();
    }
}

void LedgerConsensusImp::stateEstablish ()
{
    // Give everyone a chance to take an initial position
    if (mCurrentMSeconds < LEDGER_MIN_CONSENSUS)
        return;

    updateOurPositions ();

    // Nothing to do if we don't have consensus.
    if (!haveConsensus ())
        return;

    if (!mHaveCloseTimeConsensus)
    {
        JLOG (j_.info) <<
            "We have TX consensus but not CT consensus";
        return;
    }

    JLOG (j_.info) <<
        "Converge cutoff (" << mPeerPositions.size () << " participants)";
    state_ = State::finished;
    beginAccept (false);
}

void LedgerConsensusImp::stateFinished ()
{
    // we are processing the finished ledger
    // logic of calculating next ledger advances us out of this state
    // nothing to do
}

void LedgerConsensusImp::stateAccepted ()
{
    // we have accepted a new ledger
    endConsensus ();
}

bool LedgerConsensusImp::haveConsensus ()
{
    // CHECKME: should possibly count unacquired TX sets as disagreeing
    int agree = 0, disagree = 0;
    uint256 ourPosition = mOurPosition->getCurrentHash ();

    // Count number of agreements/disagreements with our position
    for (auto& it : mPeerPositions)
    {
        if (!it.second->isBowOut ())
        {
            if (it.second->getCurrentHash () == ourPosition)
            {
                ++agree;
            }
            else
            {
                JLOG (j_.debug) << to_string (it.first)
                    << " has " << to_string (it.second->getCurrentHash ());
                ++disagree;
                if (mCompares.count(it.second->getCurrentHash()) == 0)
                { // Make sure we have generated disputes
                    uint256 hash = it.second->getCurrentHash();
                    JLOG (j_.debug)
                        << "We have not compared to " << hash;
                    auto it1 = mAcquired.find (hash);
                    auto it2 = mAcquired.find(mOurPosition->getCurrentHash ());
                    if ((it1 != mAcquired.end()) && (it2 != mAcquired.end())
                        && (it1->second) && (it2->second))
                    {
                        mCompares.insert(hash);
                        createDisputes(it2->second, it1->second);
                    }
                }
            }
        }
    }
    int currentValidations = app_.getValidations ()
        .getNodesAfter (mPrevLedgerHash);

    JLOG (j_.debug)
        << "Checking for TX consensus: agree=" << agree
        << ", disagree=" << disagree;

    // Determine if we actually have consensus or not
    auto ret = checkConsensus (mPreviousProposers, agree + disagree, agree,
        currentValidations, mPreviousMSeconds, mCurrentMSeconds,
        app_.journal ("LedgerTiming"));

    if (ret == ConsensusState::No)
        return false;

    // There is consensus, but we need to track if the network moved on
    // without us.
    if (ret == ConsensusState::MovedOn)
        mConsensusFail = true;
    else
        mConsensusFail = false;

    return true;
}

std::shared_ptr<SHAMap> LedgerConsensusImp::getTransactionTree (
    uint256 const& hash)
{
    auto it = mAcquired.find (hash);
    if (it != mAcquired.end() && it->second)
        return it->second;

    auto set = inboundTransactions_.getSet (hash, true);

    if (set)
        mAcquired[hash] = set;

    return set;
}

bool LedgerConsensusImp::peerPosition (LedgerProposal::ref newPosition)
{
    auto const peerID = newPosition->getPeerID ();

    if (mDeadNodes.find (peerID) != mDeadNodes.end ())
    {
        JLOG (j_.info)
            << "Position from dead node: " << to_string (peerID);
        return false;
    }

    LedgerProposal::pointer& currentPosition = mPeerPositions[peerID];

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
        JLOG (j_.info)
            << "Peer bows out: " << to_string (peerID);
        for (auto& it : mDisputes)
            it.second->unVote (peerID);
        mPeerPositions.erase (peerID);
        mDeadNodes.insert (peerID);
        return true;
    }

    if (newPosition->isInitial ())
    {
        // Record the close time estimate
        JLOG (j_.trace)
            << "Peer reports close time as "
            << newPosition->getCloseTime ();
        ++mCloseTimes[newPosition->getCloseTime ()];
    }

    JLOG (j_.trace) << "Processing peer proposal "
        << newPosition->getProposeSeq () << "/"
        << newPosition->getCurrentHash ();
    currentPosition = newPosition;

    std::shared_ptr<SHAMap> set
        = getTransactionTree (newPosition->getCurrentHash ());

    if (set)
    {
        for (auto& it : mDisputes)
            it.second->setVote (peerID, set->hasItem (it.first));
    }
    else
    {
        JLOG (j_.debug)
            << "Don't have tx set for peer";
    }

    return true;
}

void LedgerConsensusImp::simulate ()
{
    JLOG (j_.info) << "Simulating consensus";
    closeLedger ();
    mCurrentMSeconds = 100;
    beginAccept (true);
    endConsensus ();
    JLOG (j_.info) << "Simulation complete";
}

void LedgerConsensusImp::accept (std::shared_ptr<SHAMap> set)
{
    Json::Value consensusStatus;

    {
        auto lock = beast::make_lock(app_.getMasterMutex());

        // put our set where others can get it later
        if (set->getHash ().isNonZero ())
           consensus_.takePosition (mPreviousLedger->info().seq, set);

        assert (set->getHash ().as_uint256() == mOurPosition->getCurrentHash ());
        consensusStatus = getJson (true);
    }

    auto  closeTime = mOurPosition->getCloseTime ();
    bool closeTimeCorrect;

    auto replay = ledgerMaster_.releaseReplay();
    if (replay)
    {
        // replaying, use the time the ledger we're replaying closed
        closeTime = replay->closeTime_;
        closeTimeCorrect = ((replay->closeFlags_ & sLCF_NoConsensusTime) == 0);
    }
    else if (closeTime == 0)
    {
        // We agreed to disagree on the close time
        closeTime = mPreviousLedger->info().closeTime + 1;
        closeTimeCorrect = false;
    }
    else
    {
        // We agreed on a close time
        closeTime = effectiveCloseTime (closeTime);
        closeTimeCorrect = true;
    }

    JLOG (j_.debug)
        << "Report: Prop=" << (mProposing ? "yes" : "no")
        << " val=" << (mValidating ? "yes" : "no")
        << " corLCL=" << (mHaveCorrectLCL ? "yes" : "no")
        << " fail=" << (mConsensusFail ? "yes" : "no");
    JLOG (j_.debug)
        << "Report: Prev = " << mPrevLedgerHash
        << ":" << mPreviousLedger->info().seq;
    JLOG (j_.debug)
        << "Report: TxSt = " << set->getHash ()
        << ", close " << closeTime << (closeTimeCorrect ? "" : "X");

    // Put transactions into a deterministic, but unpredictable, order
    CanonicalTXSet retriableTxs (set->getHash ().as_uint256());

    // Build the new last closed ledger
    auto newLCL = std::make_shared<Ledger>(
        open_ledger, *mPreviousLedger,
        app_.timeKeeper().closeTime());
    newLCL->setClosed (); // so applyTransactions sees a closed ledger

    // Set up to write SHAMap changes to our database,
    //   perform updates, extract changes
    JLOG (j_.debug)
        << "Applying consensus set transactions to the"
        << " last closed ledger";

    {
        OpenView accum(&*newLCL);
        assert(accum.closed());
        if (replay)
        {
            // Special case, we are replaying a ledger close
            for (auto& tx : replay->txns_)
                applyTransaction (app_, accum, tx.second, false, tapNO_CHECK_SIGN, j_);
        }
        else
        {
            // Normal case, we are not replaying a ledger close
            applyTransactions (app_, set.get(), accum,
                newLCL, retriableTxs, tapNONE);
        }
        // Update fee computations.
        app_.getTxQ().processValidatedLedger(app_, accum,
            mCurrentMSeconds > 5000);

        accum.apply(*newLCL);
    }

    // retriableTxs will include any transactions that
    // made it into the consensus set but failed during application
    // to the ledger.

    newLCL->updateSkipList ();

    {
        int asf = newLCL->stateMap().flushDirty (
            hotACCOUNT_NODE, newLCL->info().seq);
        int tmf = newLCL->txMap().flushDirty (
            hotTRANSACTION_NODE, newLCL->info().seq);
        JLOG (j_.debug) << "Flushed " <<
            asf << " accounts and " <<
            tmf << " transaction nodes";
    }

    // Accept ledger
    newLCL->setAccepted (closeTime, mCloseResolution, closeTimeCorrect, app_.config());

    // And stash the ledger in the ledger master
    if (ledgerMaster_.storeLedger (newLCL))
        JLOG (j_.debug)
            << "Consensus built ledger we already had";
    else if (app_.getInboundLedgers().find (newLCL->getHash()))
        JLOG (j_.debug)
            << "Consensus built ledger we were acquiring";
    else
        JLOG (j_.debug)
            << "Consensus built new ledger";

    uint256 const newLCLHash = newLCL->getHash ();
    JLOG (j_.debug)
        << "Report: NewL  = " << newLCL->getHash ()
        << ":" << newLCL->info().seq;
    // Tell directly connected peers that we have a new LCL
    statusChange (protocol::neACCEPTED_LEDGER, *newLCL);

    if (mValidating &&
        ! ledgerMaster_.isCompatible (newLCL,
            app_.journal("LedgerConsensus").warning,
            "Not validating"))
    {
        mValidating = false;
    }

    if (mValidating && !mConsensusFail)
    {
        // Build validation
        auto v = std::make_shared<STValidation> (newLCLHash,
            consensus_.validationTimestamp (
                app_.timeKeeper().now().time_since_epoch().count()),
            mValPublic, mProposing);
        v->setFieldU32 (sfLedgerSequence, newLCL->info().seq);
        addLoad(v);  // Our network load

        if (((newLCL->info().seq + 1) % 256) == 0)
        // next ledger is flag ledger
        {
            // Suggest fee changes and new features
            m_feeVote.doValidation (newLCL, *v);
            app_.getAmendmentTable ().doValidation (newLCL, *v);
        }

        auto const signingHash = v->sign (mValPrivate);
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
        JLOG (j_.info)
            << "CNF Val " << newLCLHash;
    }
    else
        JLOG (j_.info)
            << "CNF newLCL " << newLCLHash;

    // See if we can accept a ledger as fully-validated
    ledgerMaster_.consensusBuilt (newLCL, std::move (consensusStatus));

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
        for (auto& it : mDisputes)
        {
            if (!it.second->getOurVote ())
            {
                // we voted NO
                try
                {
                    JLOG (j_.debug)
                        << "Test applying disputed transaction that did"
                        << " not get in";
                    SerialIter sit (it.second->peekTransaction().slice());

                    auto txn = std::make_shared<STTx const>(sit);

                    retriableTxs.insert (txn);

                    anyDisputes = true;
                }
                catch (std::exception const&)
                {
                    JLOG (j_.debug)
                        << "Failed to apply transaction we voted NO on";
                }
            }
        }

        // Build new open ledger
        auto lock = beast::make_lock(
            app_.getMasterMutex(), std::defer_lock);
        auto sl = beast::make_lock(
            ledgerMaster_.peekMutex (), std::defer_lock);
        std::lock(lock, sl);

        auto const localTx = m_localTX.getTxSet();
        auto const oldOL = ledgerMaster_.getCurrentLedger();

        auto const lastVal =
            app_.getLedgerMaster().getValidatedLedger();
        boost::optional<Rules> rules;
        if (lastVal)
            rules.emplace(*lastVal);
        else
            rules.emplace();
        app_.openLedger().accept(app_, *rules,
            newLCL, localTx, anyDisputes, retriableTxs, tapNONE,
                "consensus",
                    [&](OpenView& view, beast::Journal j)
                    {
                        // Stuff the ledger with transactions from the queue.
                        return app_.getTxQ().accept(app_, view);
                    });
    }

    mNewLedgerHash = newLCL->getHash ();
    ledgerMaster_.switchLCL (newLCL);
    state_ = State::accepted;

    assert (ledgerMaster_.getClosedLedger()->getHash() == newLCL->getHash());
    assert (app_.openLedger().current()->info().parentHash == newLCL->getHash());

    if (mValidating)
    {
        // see how close our close time is to other node's
        //  close time reports, and update our clock.
        JLOG (j_.info)
            << "We closed at " << mCloseTime;
        std::uint64_t closeTotal = mCloseTime;
        int closeCount = 1;

        for (auto it = mCloseTimes.begin ()
            , end = mCloseTimes.end (); it != end; ++it)
        {
            // FIXME: Use median, not average
            JLOG (j_.info)
                << beast::lexicalCastThrow <std::string> (it->second)
                << " time votes for "
                << beast::lexicalCastThrow <std::string> (it->first);
            closeCount += it->second;
            closeTotal += static_cast<std::uint64_t>
                (it->first) * static_cast<std::uint64_t> (it->second);
        }

        closeTotal += (closeCount / 2);
        closeTotal /= closeCount;
        int offset = static_cast<int> (closeTotal)
            - static_cast<int> (mCloseTime);
        JLOG (j_.info)
            << "Our close offset is estimated at "
            << offset << " (" << closeCount << ")";
        app_.timeKeeper().adjustCloseTime(
            std::chrono::seconds(offset));
    }
}

void LedgerConsensusImp::createDisputes (
    std::shared_ptr<SHAMap> const& m1,
    std::shared_ptr<SHAMap> const& m2)
{
    if (m1->getHash() == m2->getHash())
        return;

    JLOG (j_.debug) << "createDisputes "
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
    JLOG (j_.debug) << dc << " differences found";
}

void LedgerConsensusImp::addDisputedTransaction (
    uint256 const& txID,
    Blob const& tx)
{
    if (mDisputes.find (txID) != mDisputes.end ())
        return;

    JLOG (j_.debug) << "Transaction "
        << txID << " is disputed";

    bool ourVote = false;

    // Update our vote on the disputed transaction
    if (mOurPosition)
    {
        auto mit (mAcquired.find (mOurPosition->getCurrentHash ()));

        if (mit != mAcquired.end ())
            ourVote = mit->second->hasItem (txID);
        else
            assert (false); // We don't have our own position?
    }

    auto txn = std::make_shared<DisputedTx> (txID, tx, ourVote, j_);
    mDisputes[txID] = txn;

    // Update all of the peer's votes on the disputed transaction
    for (auto& pit : mPeerPositions)
    {
        auto cit (mAcquired.find (pit.second->getCurrentHash ()));

        if ((cit != mAcquired.end ()) && cit->second)
        {
            txn->setVote (pit.first, cit->second->hasItem (txID));
        }
    }

    // If we didn't relay this transaction recently, relay it
    if (app_.getHashRouter ().setFlags (txID, SF_RELAYED))
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
    for (auto& it : mDisputes)
    {
        bool setHas = map->hasItem (it.second->getTransactionID ());
        for (auto const& pit : peers)
            it.second->setVote (pit, setHas);
    }
}

void LedgerConsensusImp::leaveConsensus ()
{
    if (mProposing)
    {
        if (mOurPosition && ! mOurPosition->isBowOut ())
        {
            mOurPosition->bowOut();
            propose();
        }
        mProposing = false;
    }
}

void LedgerConsensusImp::propose ()
{
    JLOG (j_.trace) << "We propose: " <<
        (mOurPosition->isBowOut ()
            ? std::string ("bowOut")
            : to_string (mOurPosition->getCurrentHash ()));
    protocol::TMProposeSet prop;

    prop.set_currenttxhash (mOurPosition->getCurrentHash ().begin ()
        , 256 / 8);
    prop.set_previousledger (mOurPosition->getPrevLedger ().begin ()
        , 256 / 8);
    prop.set_proposeseq (mOurPosition->getProposeSeq ());
    prop.set_closetime (mOurPosition->getCloseTime ());

    Blob const pubKey = mValPublic.getNodePublic ();
    prop.set_nodepubkey (&pubKey[0], pubKey.size ());

    Blob const sig = mOurPosition->sign (mValPrivate);
    prop.set_signature (&sig[0], sig.size ());

    app_.overlay().send(prop);
}

void LedgerConsensusImp::sendHaveTxSet (uint256 const& hash, bool direct)
{
    protocol::TMHaveTransactionSet msg;
    msg.set_hash (hash.begin (), 256 / 8);
    msg.set_status (direct ? protocol::tsHAVE : protocol::tsCAN_GET);
    app_.overlay ().foreach (send_always (
        std::make_shared <Message> (
            msg, protocol::mtHAVE_SET)));
}

void LedgerConsensusImp::statusChange (
    protocol::NodeEvent event, Ledger& ledger)
{
    protocol::TMStatusChange s;

    if (!mHaveCorrectLCL)
        s.set_newevent (protocol::neLOST_SYNC);
    else
        s.set_newevent (event);

    s.set_ledgerseq (ledger.info().seq);
    s.set_networktime (app_.timeKeeper().now().time_since_epoch().count());
    s.set_ledgerhashprevious(ledger.info().parentHash.begin (),
        std::decay_t<decltype(ledger.info().parentHash)>::bytes);
    s.set_ledgerhash (ledger.getHash ().begin (),
        std::decay_t<decltype(ledger.getHash ())>::bytes);

    std::uint32_t uMin, uMax;
    if (!ledgerMaster_.getFullValidatedRange (uMin, uMax))
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
    JLOG (j_.trace) << "send status change to peer";
}

void LedgerConsensusImp::takeInitialPosition (
    std::shared_ptr<ReadView const> const& initialLedger)
{
    std::shared_ptr<SHAMap> initialSet = std::make_shared <SHAMap> (
        SHAMapType::TRANSACTION, app_.family());

    // Build SHAMap containing all transactions in our open ledger
    for (auto const& tx : initialLedger->txs)
    {
        Serializer s (2048);
        tx.first->add(s);
        initialSet->addItem (
            SHAMapItem (tx.first->getTransactionID(), std::move (s)), true, false);
    }

    if ((app_.config().RUN_STANDALONE || (mProposing && mHaveCorrectLCL))
            && ((mPreviousLedger->info().seq % 256) == 0))
    {
        // previous ledger was flag ledger, add pseudo-transactions
        ValidationSet parentSet = app_.getValidations().getValidations (
            mPreviousLedger->info().parentHash);
        m_feeVote.doVoting (mPreviousLedger, parentSet, initialSet);
        app_.getAmendmentTable ().doVoting (
            mPreviousLedger, parentSet, initialSet);
    }

    // Set should be immutable snapshot
    initialSet = initialSet->snapShot (false);

    // Tell the ledger master not to acquire the ledger we're probably building
    ledgerMaster_.setBuildingLedger (mPreviousLedger->info().seq + 1);

    auto txSet = initialSet->getHash ().as_uint256();
    JLOG (j_.info) << "initial position " << txSet;
    mapCompleteInternal (txSet, initialSet, false);

    mOurPosition = std::make_shared<LedgerProposal>
        (mValPublic, initialLedger->info().parentHash, txSet, mCloseTime);

    for (auto& it : mDisputes)
    {
        it.second->setOurVote (initialLedger->txExists (it.first));
    }

    // if any peers have taken a contrary position, process disputes
    hash_set<uint256> found;

    for (auto& it : mPeerPositions)
    {
        uint256 set = it.second->getCurrentHash ();

        if (found.insert (set).second)
        {
            auto iit (mAcquired.find (set));

            if (iit != mAcquired.end ())
            {
                mCompares.insert(iit->second->getHash().as_uint256());
                createDisputes (initialSet, iit->second);
            }
        }
    }

    if (mProposing)
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

std::uint32_t LedgerConsensusImp::effectiveCloseTime (std::uint32_t closeTime)
{
    if (closeTime == 0)
        return 0;

    return std::max (
        roundCloseTime (closeTime, mCloseResolution),
        mPreviousLedger->info().closeTime + 1);
}

void LedgerConsensusImp::updateOurPositions ()
{
    // Compute a cutoff time
    auto peerCutoff
        = std::chrono::steady_clock::now ();
    auto ourCutoff
        = peerCutoff - std::chrono::seconds (PROPOSE_INTERVAL);
    peerCutoff -= std::chrono::seconds (PROPOSE_FRESHNESS);

    bool changes = false;
    std::shared_ptr<SHAMap> ourPosition;
    //  std::vector<uint256> addedTx, removedTx;

    // Verify freshness of peer positions and compute close times
    std::map<std::uint32_t, int> closeTimes;
    auto it = mPeerPositions.begin ();

    while (it != mPeerPositions.end ())
    {
        if (it->second->isStale (peerCutoff))
        {
            // peer's proposal is stale, so remove it
            auto const& peerID = it->second->getPeerID ();
            JLOG (j_.warning)
                << "Removing stale proposal from " << peerID;
            for (auto& dt : mDisputes)
                dt.second->unVote (peerID);
            it = mPeerPositions.erase (it);
        }
        else
        {
            // proposal is still fresh
            ++closeTimes[effectiveCloseTime (
                it->second->getCloseTime ())];
            ++it;
        }
    }

    // Update votes on disputed transactions
    for (auto& it : mDisputes)
    {
        // Because the threshold for inclusion increases,
        //  time can change our position on a dispute
        if (it.second->updateVote (mClosePercent, mProposing))
        {
            if (!changes)
            {
                ourPosition = mAcquired[mOurPosition->getCurrentHash ()]
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

    if (mClosePercent < AV_MID_CONSENSUS_TIME)
        neededWeight = AV_INIT_CONSENSUS_PCT;
    else if (mClosePercent < AV_LATE_CONSENSUS_TIME)
        neededWeight = AV_MID_CONSENSUS_PCT;
    else if (mClosePercent < AV_STUCK_CONSENSUS_TIME)
        neededWeight = AV_LATE_CONSENSUS_PCT;
    else
        neededWeight = AV_STUCK_CONSENSUS_PCT;

    std::uint32_t closeTime = 0;
    mHaveCloseTimeConsensus = false;

    if (mPeerPositions.empty ())
    {
        // no other times
        mHaveCloseTimeConsensus = true;
        closeTime = effectiveCloseTime (mOurPosition->getCloseTime ());
    }
    else
    {
        int participants = mPeerPositions.size ();
        if (mProposing)
        {
            ++closeTimes[
                effectiveCloseTime (mOurPosition->getCloseTime ())];
            ++participants;
        }

        // Threshold for non-zero vote
        int threshVote = participantsNeeded (participants,
            neededWeight);

        // Threshold to declare consensus
        int const threshConsensus = participantsNeeded (
            participants, AV_CT_CONSENSUS_PCT);

        JLOG (j_.info) << "Proposers:"
            << mPeerPositions.size () << " nw:" << neededWeight
            << " thrV:" << threshVote << " thrC:" << threshConsensus;

        for (auto const& it : closeTimes)
        {
            JLOG (j_.debug) << "CCTime: seq "
                << mPreviousLedger->info().seq + 1 << ": "
                << it.first << " has " << it.second << ", "
                << threshVote << " required";

            if (it.second >= threshVote)
            {
                // A close time has enough votes for us to try to agree
                closeTime = it.first;
                threshVote = it.second;

                if (threshVote >= threshConsensus)
                    mHaveCloseTimeConsensus = true;
            }
        }

        CondLog (!mHaveCloseTimeConsensus, lsDEBUG, LedgerConsensus)
            << "No CT consensus: Proposers:" << mPeerPositions.size ()
            << " Proposing:" << (mProposing ? "yes" : "no") << " Thresh:"
            << threshConsensus << " Pos:" << closeTime;
    }

    // Temporarily send a new proposal if there's any change to our
    // claimed close time. Once the new close time code is deployed
    // to the full network, this can be relaxed to force a change
    // only if the rounded close time has changed.
    if (!changes &&
            ((closeTime != mOurPosition->getCloseTime ())
            || mOurPosition->isStale (ourCutoff)))
    {
        // close time changed or our position is stale
        ourPosition = mAcquired[mOurPosition->getCurrentHash ()]
            ->snapShot (true);
        assert (ourPosition);
        changes = true; // We pretend our position changed to force
    }                   //   a new proposal

    if (changes)
    {
        auto newHash = ourPosition->getHash ().as_uint256();
        JLOG (j_.info)
            << "Position change: CTime " << closeTime
            << ", tx " << newHash;

        if (mOurPosition->changePosition (newHash, closeTime))
        {
            if (mProposing)
                propose ();

            mapCompleteInternal (newHash, ourPosition, false);
        }
    }
}

void LedgerConsensusImp::playbackProposals ()
{
    for (auto const& it: consensus_.peekStoredProposals ())
    {
        for (auto const& proposal : it.second)
        {
            if (proposal->isPrevLedger (mPrevLedgerHash) &&
                peerPosition (proposal))
            {
                JLOG (j_.warning)
                    << "We should do delayed relay of this proposal,"
                    << " but we cannot";
            }
        }
    }
}

void LedgerConsensusImp::closeLedger ()
{
    checkOurValidation ();
    state_ = State::establish;
    mConsensusStartTime = std::chrono::steady_clock::now ();
    mCloseTime = app_.timeKeeper().closeTime().time_since_epoch().count();
    consensus_.setLastCloseTime (mCloseTime);
    statusChange (protocol::neCLOSING_LEDGER, *mPreviousLedger);
    ledgerMaster_.applyHeldTransactions ();
    takeInitialPosition (app_.openLedger().current());
}

void LedgerConsensusImp::checkOurValidation ()
{
    // This only covers some cases - Fix for the case where we can't ever
    // acquire the consensus ledger
    if (!mHaveCorrectLCL || !mValPublic.isSet ()
        || !mValPrivate.isSet ()
        || app_.getOPs ().isNeedNetworkLedger ())
    {
        return;
    }

    auto lastValidation = consensus_.getLastValidation ();

    if (lastValidation)
    {
        if (lastValidation->getFieldU32 (sfLedgerSequence)
            == mPreviousLedger->info().seq)
        {
            return;
        }
        if (lastValidation->getLedgerHash () == mPrevLedgerHash)
            return;
    }

    auto v = std::make_shared<STValidation> (mPreviousLedger->getHash (),
        consensus_.validationTimestamp (
            app_.timeKeeper().now().time_since_epoch().count()),
        mValPublic, false);
    addLoad(v);
    v->setTrusted ();
    auto const signingHash = v->sign (mValPrivate);
        // FIXME: wrong supression
    app_.getHashRouter ().addSuppression (signingHash);
    app_.getValidations ().addValidation (v, "localMissing");
    Blob validation = v->getSigned ();
    protocol::TMValidation val;
    val.set_validation (&validation[0], validation.size ());
    consensus_.setLastValidation (v);
    JLOG (j_.warning) << "Sending partial validation";
}

void LedgerConsensusImp::beginAccept (bool synchronous)
{
    auto consensusSet = mAcquired[mOurPosition->getCurrentHash ()];

    if (!consensusSet)
    {
        JLOG (j_.fatal)
            << "We don't have a consensus set";
        abort ();
        return;
    }

    consensus_.newLCL (
        mPeerPositions.size (), mCurrentMSeconds, mNewLedgerHash);

    if (synchronous)
        accept (consensusSet);
    else
    {
        app_.getJobQueue().addJob (jtACCEPT, "acceptLedger",
            std::bind (&LedgerConsensusImp::accept, shared_from_this (),
                       consensusSet));
    }
}

void LedgerConsensusImp::endConsensus ()
{
    app_.getOPs ().endConsensus (mHaveCorrectLCL);
}

void LedgerConsensusImp::addLoad(STValidation::ref val)
{
    std::uint32_t fee = std::max(
        app_.getFeeTrack().getLocalFee(),
        app_.getFeeTrack().getClusterFee());
    std::uint32_t ref = app_.getFeeTrack().getLoadBase();
    if (fee > ref)
        val->setFieldU32(sfLoadFee, fee);
}

//------------------------------------------------------------------------------
std::shared_ptr <LedgerConsensus>
make_LedgerConsensus (Application& app, ConsensusImp& consensus, int previousProposers,
    int previousConvergeTime, InboundTransactions& inboundTransactions,
    LocalTxs& localtx, LedgerMaster& ledgerMaster,
    LedgerHash const &prevLCLHash,
    Ledger::ref previousLedger, std::uint32_t closeTime, FeeVote& feeVote)
{
    return std::make_shared <LedgerConsensusImp> (app, consensus, previousProposers,
        previousConvergeTime, inboundTransactions, localtx, ledgerMaster,
        prevLCLHash, previousLedger, closeTime, feeVote);
}

//------------------------------------------------------------------------------

int
applyTransaction (Application& app, OpenView& view,
    std::shared_ptr<STTx const> const& txn,
        bool retryAssured, ApplyFlags flags,
            beast::Journal j)
{
    // Returns false if the transaction has need not be retried.
    if (retryAssured)
        flags = flags | tapRETRY;

    JLOG (j.debug) << "TXN "
        << txn->getTransactionID ()
        //<< (engine.view().open() ? " open" : " closed")
        // because of the optional in engine
        << (retryAssured ? "/retry" : "/final");
    JLOG (j.trace) << txn->getJson (0);

    try
    {
        auto const result = apply(app,
            view, *txn, flags, j);
        if (result.second)
        {
            JLOG (j.debug)
                << "Transaction applied: " << transHuman (result.first);
            return LedgerConsensusImp::resultSuccess;
        }

        if (isTefFailure (result.first) || isTemMalformed (result.first) ||
            isTelLocal (result.first))
        {
            // failure
            JLOG (j.debug)
                << "Transaction failure: " << transHuman (result.first);
            return LedgerConsensusImp::resultFail;
        }

        JLOG (j.debug)
            << "Transaction retry: " << transHuman (result.first);
        return LedgerConsensusImp::resultRetry;
    }
    catch (std::exception const&)
    {
        JLOG (j.warning) << "Throws";
        return LedgerConsensusImp::resultFail;
    }
}

void applyTransactions (
    Application& app,
    SHAMap const* set,
    OpenView& view,
    Ledger::ref checkLedger,
    CanonicalTXSet& retriableTxs,
    ApplyFlags flags)
{

    auto j = app.journal ("LedgerConsensus");
    if (set)
    {
        for (auto const& item : *set)
        {
            if (checkLedger->txExists (item.key()))
                continue;

            // The transaction isn't in the check ledger, try to apply it
            JLOG (j.debug) <<
                "Processing candidate transaction: " << item.key();
            std::shared_ptr<STTx const> txn;
            try
            {
                txn = std::make_shared<STTx const>(SerialIter{item.slice()});
            }
            catch (std::exception const&)
            {
                JLOG (j.warning) << "  Throws";
            }

            if (txn)
            {
                // All transactions execute in canonical order
                retriableTxs.insert (txn);
            }
        }
    }

    bool certainRetry = true;
    // Attempt to apply all of the retriable transactions
    for (int pass = 0; pass < LEDGER_TOTAL_PASSES; ++pass)
    {
        JLOG (j.debug) << "Pass: " << pass << " Txns: "
            << retriableTxs.size ()
            << (certainRetry ? " retriable" : " final");
        int changes = 0;

        auto it = retriableTxs.begin ();

        while (it != retriableTxs.end ())
        {
            try
            {
                switch (applyTransaction (app, view,
                    it->second, certainRetry, flags, j))
                {
                case LedgerConsensusImp::resultSuccess:
                    it = retriableTxs.erase (it);
                    ++changes;
                    break;

                case LedgerConsensusImp::resultFail:
                    it = retriableTxs.erase (it);
                    break;

                case LedgerConsensusImp::resultRetry:
                    ++it;
                }
            }
            catch (std::exception const&)
            {
                JLOG (j.warning)
                    << "Transaction throws";
                it = retriableTxs.erase (it);
            }
        }

        JLOG (j.debug) << "Pass: "
            << pass << " finished " << changes << " changes";

        // A non-retry pass made no changes
        if (!changes && !certainRetry)
            return;

        // Stop retriable passes
        if (!changes || (pass >= LEDGER_RETRY_PASSES))
            certainRetry = false;
    }

    // If there are any transactions left, we must have
    // tried them in at least one final pass
    assert (retriableTxs.empty() || !certainRetry);
}

} // ripple
