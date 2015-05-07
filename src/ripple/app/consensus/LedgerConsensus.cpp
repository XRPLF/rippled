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
#include <ripple/app/consensus/DisputedTx.h>
#include <ripple/app/consensus/LedgerConsensus.h>
#include <ripple/app/ledger/InboundLedgers.h>
#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/ledger/LedgerTiming.h>
#include <ripple/app/ledger/LedgerToJson.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/AmendmentTable.h>
#include <ripple/app/misc/CanonicalTXSet.h>
#include <ripple/app/misc/IHashRouter.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/app/misc/Validations.h>
#include <ripple/app/tx/TransactionAcquire.h>
#include <ripple/app/tx/InboundTransactions.h>
#include <ripple/basics/CountedObject.h>
#include <ripple/basics/Log.h>
#include <ripple/core/Config.h>
#include <ripple/core/JobQueue.h>
#include <ripple/core/LoadFeeTrack.h>
#include <ripple/json/to_string.h>
#include <ripple/overlay/Overlay.h>
#include <ripple/overlay/predicates.h>
#include <ripple/protocol/STValidation.h>
#include <ripple/protocol/UintTypes.h>
#include <beast/module/core/text/LexicalCast.h>
#include <beast/utility/make_lock.h>
#include <type_traits>

namespace ripple {

/**
  Provides the implementation for LedgerConsensus.

  Achieves consensus on the next ledger.
  This object is created when the consensus process starts, and
  is destroyed when the process is complete.

  Nearly everything herein is invoked with the master lock.

  Two things need consensus:
    1.  The set of transactions.
    2.  The close time for the ledger.
*/
class LedgerConsensusImp
    : public LedgerConsensus
    , public std::enable_shared_from_this <LedgerConsensusImp>
    , public CountedObject <LedgerConsensusImp>
{
public:
    /**
     * The result of applying a transaction to a ledger.
    */
    enum {resultSuccess, resultFail, resultRetry};

    static char const* getCountedObjectName () { return "LedgerConsensus"; }

    LedgerConsensusImp(LedgerConsensusImp const&) = delete;
    LedgerConsensusImp& operator=(LedgerConsensusImp const&) = delete;

    /**
      The result of applying a transaction to a ledger.

      @param localtx        A set of local transactions to apply.
      @param prevLCLHash    The hash of the Last Closed Ledger (LCL).
      @param previousLedger Best guess of what the Last Closed Ledger (LCL)
                            was.
      @param closeTime      Closing time point of the LCL.
      @param feeVote        Our desired fee levels and voting logic.
    */
    LedgerConsensusImp (LocalTxs& localtx,
        LedgerHash const & prevLCLHash, Ledger::ref previousLedger,
            std::uint32_t closeTime, FeeVote& feeVote)
        : m_localTX (localtx)
        , m_feeVote (feeVote)
        , mState (lcsPRE_CLOSE)
        , mCloseTime (closeTime)
        , mPrevLedgerHash (prevLCLHash)
        , mPreviousLedger (previousLedger)
        , mValPublic (getConfig ().VALIDATION_PUB)
        , mValPrivate (getConfig ().VALIDATION_PRIV)
        , mConsensusFail (false)
        , mCurrentMSeconds (0)
        , mClosePercent (0)
        , mHaveCloseTimeConsensus (false)
        , mConsensusStartTime
            (std::chrono::steady_clock::now ())
    {
        WriteLog (lsDEBUG, LedgerConsensus) << "Creating consensus object";
        WriteLog (lsTRACE, LedgerConsensus)
            << "LCL:" << previousLedger->getHash () << ", ct=" << closeTime;
        mPreviousProposers = getApp().getOPs ().getPreviousProposers ();
        mPreviousMSeconds = getApp().getOPs ().getPreviousConvergeTime ();
        assert (mPreviousMSeconds);

        getApp().getInboundTransactions().newRound (mPreviousLedger->getLedgerSeq());

        // Adapt close time resolution to recent network conditions
        mCloseResolution = ContinuousLedgerTiming::getNextLedgerTimeResolution (
            mPreviousLedger->getCloseResolution (),
            mPreviousLedger->getCloseAgree (),
            previousLedger->getLedgerSeq () + 1);

        if (mValPublic.isSet () && mValPrivate.isSet ()
            && !getApp().getOPs ().isNeedNetworkLedger ())
        {
            // If the validation keys were set, and if we need a ledger,
            // then we want to validate, and possibly propose a ledger.
            WriteLog (lsINFO, LedgerConsensus)
                << "Entering consensus process, validating";
            mValidating = true;
            // Propose if we are in sync with the network
            mProposing =
                getApp().getOPs ().getOperatingMode () == NetworkOPs::omFULL;
        }
        else
        {
            // Otherwise we just want to monitor the validation process.
            WriteLog (lsINFO, LedgerConsensus)
                << "Entering consensus process, watching";
            mProposing = mValidating = false;
        }

        mHaveCorrectLCL = (mPreviousLedger->getHash () == mPrevLedgerHash);

        if (!mHaveCorrectLCL)
        {
            // If we were not handed the correct LCL, then set our state
            // to not proposing.
            getApp().getOPs ().setProposing (false, false);
            handleLCL (mPrevLedgerHash);

            if (!mHaveCorrectLCL)
            {
                //          mProposing = mValidating = false;
                WriteLog (lsINFO, LedgerConsensus)
                    << "Entering consensus with: "
                    << previousLedger->getHash ();
                WriteLog (lsINFO, LedgerConsensus)
                    << "Correct LCL is: " << prevLCLHash;
            }
        }
        else  // update the network status table as to whether we're proposing/validating
            getApp().getOPs ().setProposing (mProposing, mValidating);
    }

    /**
      This function is called, but its return value is always ignored.

      @return 1.
    */
    int startup ()
    {
        return 1;
    }

    /**
      Get the Json state of the consensus process.
      Called by the consensus_info RPC.

      @param full True if verbose response desired.
      @return     The Json state.
    */
    Json::Value getJson (bool full)
    {
        Json::Value ret (Json::objectValue);
        ret["proposing"] = mProposing;
        ret["validating"] = mValidating;
        ret["proposers"] = static_cast<int> (mPeerPositions.size ());

        if (mHaveCorrectLCL)
        {
            ret["synched"] = true;
            ret["ledger_seq"] = mPreviousLedger->getLedgerSeq () + 1;
            ret["close_granularity"] = mCloseResolution;
        }
        else
            ret["synched"] = false;

        switch (mState)
        {
        case lcsPRE_CLOSE:
            ret[jss::state] = "open";
            break;

        case lcsESTABLISH:
            ret[jss::state] = "consensus";
            break;

        case lcsFINISHED:
            ret[jss::state] = "finished";
            break;

        case lcsACCEPTED:
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
                    ctj[beast::lexicalCastThrow <std::string> (ct.first)] = ct.second;
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

    Ledger::ref peekPreviousLedger ()
    {
        return mPreviousLedger;
    }

    uint256 getLCL ()
    {
        return mPrevLedgerHash;
    }

    /**
      We have a complete transaction set, typically acquired from the network

      @param hash     hash of the transaction set.
      @param map      the transaction set.
      @param acquired true if we have acquired the transaction set.
    */
    void mapComplete (uint256 const& hash, std::shared_ptr<SHAMap> const& map,
                      bool acquired)
    {
        try
        {
            mapCompleteInternal (hash, map, acquired);
        }
        catch (SHAMapMissingNode const& mn)
        {
            leaveConsensus();
            WriteLog (lsERROR, LedgerConsensus) <<
                "Missing node processing complete map " << mn;
            throw;
        }
    }

    void mapCompleteInternal (uint256 const& hash,
                              std::shared_ptr<SHAMap> const& map, bool acquired)
    {
        CondLog (acquired, lsDEBUG, LedgerConsensus)
            << "We have acquired TXS " << hash;

        if (!map)  // If the map was invalid
        {
            // this is an invalid/corrupt map
            mAcquired[hash] = map;
            WriteLog (lsWARNING, LedgerConsensus)
                << "A trusted node directed us to acquire an invalid TXN map";
            return;
        }

        assert (hash == map->getHash ());

        auto it = mAcquired.find (hash);

        // If we have already acquired this transaction set
        if (mAcquired.find (hash) != mAcquired.end ())
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
            getApp().getInboundTransactions().giveSet (hash, map, false);
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
        else
            WriteLog (lsDEBUG, LedgerConsensus)
                << "Not ready to create disputes";

        mAcquired[hash] = map;

        // Adjust tracking for each peer that takes this position
        std::vector<NodeID> peers;
        for (auto& it : mPeerPositions)
        {
            if (it.second->getCurrentHash () == map->getHash ())
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

    /**
      Check if our last closed ledger matches the network's.
      This tells us if we are still in sync with the network.
      This also helps us if we enter the consensus round with
      the wrong ledger, to leave it with the correct ledger so
      that we can participate in the next round.
    */
    void checkLCL ()
    {
        uint256 netLgr = mPrevLedgerHash;
        int netLgrCount = 0;

        uint256 favoredLedger = mPrevLedgerHash; // Don't jump forward
        uint256 priorLedger;

        if (mHaveCorrectLCL)
            priorLedger = mPreviousLedger->getParentHash (); // don't jump back

        // Get validators that are on our ledger, or  "close" to being on
        // our ledger.
        hash_map<uint256, ValidationCounter> vals =
            getApp().getValidations ().getCurrentValidations
            (favoredLedger, priorLedger);

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

            switch (mState)
            {
            case lcsPRE_CLOSE:
                status = "PreClose";
                break;

            case lcsESTABLISH:
                status = "Establish";
                break;

            case lcsFINISHED:
                status = "Finished";
                break;

            case lcsACCEPTED:
                status = "Accepted";
                break;

            default:
                status = "unknown";
            }

            WriteLog (lsWARNING, LedgerConsensus)
                << "View of consensus changed during " << status
                << " (" << netLgrCount << ") status="
                << status << ", "
                << (mHaveCorrectLCL ? "CorrectLCL" : "IncorrectLCL");
            WriteLog (lsWARNING, LedgerConsensus) << mPrevLedgerHash
                << " to " << netLgr;
            WriteLog (lsWARNING, LedgerConsensus)
                << ripple::getJson (*mPreviousLedger);

            if (ShouldLog (lsDEBUG, LedgerConsensus))
            {
                for (auto& it : vals)
                {
                    WriteLog (lsDEBUG, LedgerConsensus)
                        << "V: " << it.first << ", " << it.second.first;
                }
            }

            if (mHaveCorrectLCL)
                getApp().getOPs ().consensusViewChange ();

            handleLCL (netLgr);
        }
        else if (mPreviousLedger->getHash () != mPrevLedgerHash)
            handleLCL (netLgr);
    }

    /**
      Change our view of the last closed ledger

      @param lclHash Hash of the last closed ledger.
    */
    void handleLCL (uint256 const& lclHash)
    {
        assert ((lclHash != mPrevLedgerHash) || (mPreviousLedger->getHash () != lclHash));

        if (mPrevLedgerHash != lclHash)
        {
            // first time switching to this ledger
            mPrevLedgerHash = lclHash;

            if (mHaveCorrectLCL && mProposing && mOurPosition)
            {
                WriteLog (lsINFO, LedgerConsensus) << "Bowing out of consensus";
                mOurPosition->bowOut ();
                propose ();
            }

            // Stop proposing because we are out of sync
            mProposing = false;
            //      mValidating = false;
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
        Ledger::pointer newLCL = getApp().getLedgerMaster ().getLedgerByHash (mPrevLedgerHash);
        if (!newLCL)
        {
            if (mAcquiringLedger != lclHash)
            {
                // need to start acquiring the correct consensus LCL
                WriteLog (lsWARNING, LedgerConsensus) << "Need consensus ledger " << mPrevLedgerHash;

                // Tell the ledger acquire system that we need the consensus ledger
                mAcquiringLedger = mPrevLedgerHash;
                getApp().getJobQueue().addJob (jtADVANCE, "getConsensusLedger",
                    std::bind (
                        &InboundLedgers::acquire,
                        &getApp().getInboundLedgers(),
                        mPrevLedgerHash, 0, InboundLedger::fcCONSENSUS));
                mHaveCorrectLCL = false;
            }
            return;
        }

        assert (newLCL->isClosed () && newLCL->isImmutable ());
        assert (newLCL->getHash () == lclHash);
        mPreviousLedger = newLCL;
        mPrevLedgerHash = lclHash;

        WriteLog (lsINFO, LedgerConsensus) << "Have the consensus ledger " << mPrevLedgerHash;
        mHaveCorrectLCL = true;

        mCloseResolution = ContinuousLedgerTiming::getNextLedgerTimeResolution (
                               mPreviousLedger->getCloseResolution (), mPreviousLedger->getCloseAgree (),
                               mPreviousLedger->getLedgerSeq () + 1);
    }



    /**
      On timer call the correct handler for each state.
    */
    void timerEntry ()
    {
        try
        {
           doTimer();
        }
        catch (SHAMapMissingNode const& mn)
        {
            leaveConsensus ();
            WriteLog (lsERROR, LedgerConsensus) <<
               "Missing node during consensus process " << mn;
            throw;
        }
    }

    void doTimer ()
    {
        if ((mState != lcsFINISHED) && (mState != lcsACCEPTED))
            checkLCL ();

        mCurrentMSeconds = std::chrono::duration_cast <std::chrono::milliseconds>
            (std::chrono::steady_clock::now() - mConsensusStartTime).count ();
        mClosePercent = mCurrentMSeconds * 100 / mPreviousMSeconds;

        switch (mState)
        {
        case lcsPRE_CLOSE:
            statePreClose ();
            return;

        case lcsESTABLISH:
            stateEstablish ();

            if (mState != lcsFINISHED) return;

            // Fall through

        case lcsFINISHED:
            stateFinished ();

            if (mState != lcsACCEPTED) return;

            // Fall through

        case lcsACCEPTED:
            stateAccepted ();
            return;
        }

        assert (false);
    }

    /**
      Handle pre-close state.
    */
    void statePreClose ()
    {
        // it is shortly before ledger close time
        bool anyTransactions
            = getApp().getLedgerMaster ().getCurrentLedger ()
            ->peekTransactionMap ()->getHash ().isNonZero ();
        int proposersClosed = mPeerPositions.size ();
        int proposersValidated
            = getApp().getValidations ().getTrustedValidationCount
            (mPrevLedgerHash);

        // This ledger is open. This computes how long since last ledger closed
        int sinceClose;
        int idleInterval = 0;

        if (mHaveCorrectLCL && mPreviousLedger->getCloseAgree ())
        {
            // we can use consensus timing
            sinceClose = 1000 * (getApp().getOPs ().getCloseTimeNC ()
                - mPreviousLedger->getCloseTimeNC ());
            idleInterval = 2 * mPreviousLedger->getCloseResolution ();

            if (idleInterval < LEDGER_IDLE_INTERVAL)
                idleInterval = LEDGER_IDLE_INTERVAL;
        }
        else
        {
            // Use the time we saw the last ledger close
            sinceClose = 1000 * (getApp().getOPs ().getCloseTimeNC ()
                - getApp().getOPs ().getLastCloseTime ());
            idleInterval = LEDGER_IDLE_INTERVAL;
        }

        idleInterval = std::max (idleInterval, LEDGER_IDLE_INTERVAL);
        idleInterval = std::max (idleInterval, 2 * mPreviousLedger->getCloseResolution ());

        // Decide if we should close the ledger
        if (ContinuousLedgerTiming::shouldClose (anyTransactions
            , mPreviousProposers, proposersClosed, proposersValidated
            , mPreviousMSeconds, sinceClose, mCurrentMSeconds
            , idleInterval))
        {
            closeLedger ();
        }
    }

    /** We are establishing a consensus
       Update our position only on the timer, and in this state.
       If we have consensus, move to the finish state
    */
    void stateEstablish ()
    {

        // Give everyone a chance to take an initial position
        if (mCurrentMSeconds < LEDGER_MIN_CONSENSUS)
            return;

        updateOurPositions ();

        if (!mHaveCloseTimeConsensus)
        {
            CondLog (haveConsensus (false), lsINFO, LedgerConsensus)
                << "We have TX consensus but not CT consensus";
        }
        else if (haveConsensus (true))
        {
            WriteLog (lsINFO, LedgerConsensus)
                << "Converge cutoff (" << mPeerPositions.size ()
                << " participants)";
            mState = lcsFINISHED;
            beginAccept (false);
        }
    }

    void stateFinished ()
    {
        // we are processing the finished ledger
        // logic of calculating next ledger advances us out of this state
        // nothing to do
    }
    void stateAccepted ()
    {
        // we have accepted a new ledger
        endConsensus ();
    }

    /** Check if we've reached consensus
    */
    bool haveConsensus (bool forReal)
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
                    WriteLog (lsDEBUG, LedgerConsensus) << to_string (it.first)
                        << " has " << to_string (it.second->getCurrentHash ());
                    ++disagree;
                    if (mCompares.count(it.second->getCurrentHash()) == 0)
                    { // Make sure we have generated disputes
                        uint256 hash = it.second->getCurrentHash();
                        WriteLog (lsDEBUG, LedgerConsensus)
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
        int currentValidations = getApp().getValidations ()
            .getNodesAfter (mPrevLedgerHash);

        WriteLog (lsDEBUG, LedgerConsensus)
            << "Checking for TX consensus: agree=" << agree
            << ", disagree=" << disagree;

        // Determine if we actually have consensus or not
        return ContinuousLedgerTiming::haveConsensus (mPreviousProposers,
            agree + disagree, agree, currentValidations
            , mPreviousMSeconds, mCurrentMSeconds, forReal, mConsensusFail);
    }

    std::shared_ptr<SHAMap> getTransactionTree (uint256 const& hash)
    {
        auto it = mAcquired.find (hash);
        if (it != mAcquired.end() && it->second)
            return it->second;

        auto set = getApp().getInboundTransactions().getSet (hash, true);

        if (set)
            mAcquired[hash] = set;

        return set;
    }

    /**
      A server has taken a new position, adjust our tracking
      Called when a peer takes a new postion.

      @param newPosition the new position
      @return            true if we should do delayed relay of this position.
    */
    bool peerPosition (LedgerProposal::ref newPosition)
    {
        auto peerID = newPosition->getPeerID ();

        if (mDeadNodes.find (peerID) != mDeadNodes.end ())
        {
            WriteLog (lsINFO, LedgerConsensus)
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

        if (newPosition->getProposeSeq () == 0)
        {
            // new initial close time estimate
            WriteLog (lsTRACE, LedgerConsensus)
                << "Peer reports close time as "
                << newPosition->getCloseTime ();
            ++mCloseTimes[newPosition->getCloseTime ()];
        }
        else if (newPosition->getProposeSeq () == LedgerProposal::seqLeave)
        {
            // peer bows out
            WriteLog (lsINFO, LedgerConsensus)
                << "Peer bows out: " << to_string (peerID);
            for (auto& it : mDisputes)
                it.second->unVote (peerID);
            mPeerPositions.erase (peerID);
            mDeadNodes.insert (peerID);
            return true;
        }


        WriteLog (lsTRACE, LedgerConsensus) << "Processing peer proposal "
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
            WriteLog (lsDEBUG, LedgerConsensus)
                << "Don't have tx set for peer";
        }

        return true;
    }

    bool isOurPubKey (const RippleAddress & k)
    {
        return k == mValPublic;
    }

    /** Simulate a consensus round without any network traffic
    */
    void simulate ()
    {
        WriteLog (lsINFO, LedgerConsensus) << "Simulating consensus";
        closeLedger ();
        mCurrentMSeconds = 100;
        beginAccept (true);
        endConsensus ();
        WriteLog (lsINFO, LedgerConsensus) << "Simulation complete";
    }
private:
    /** We have a new last closed ledger, process it. Final accept logic

      @param set Our consensus set
    */
    void accept (std::shared_ptr<SHAMap> set)
    {

        {
            std::lock_guard<Application::MutexType> lock(getApp().getMasterMutex());

            // put our set where others can get it later
            if (set->getHash ().isNonZero ())
               getApp().getOPs ().takePosition (
                   mPreviousLedger->getLedgerSeq (), set);

            assert (set->getHash () == mOurPosition->getCurrentHash ());
            // these are now obsolete
            getApp().getOPs ().peekStoredProposals ().clear ();
        }

        std::uint32_t closeTime = roundCloseTime (mOurPosition->getCloseTime ());
        bool closeTimeCorrect = true;

        if (closeTime == 0)
        {
            // we agreed to disagree
            closeTimeCorrect = false;
            closeTime = mPreviousLedger->getCloseTimeNC () + 1;
        }

        WriteLog (lsDEBUG, LedgerConsensus)
            << "Report: Prop=" << (mProposing ? "yes" : "no")
            << " val=" << (mValidating ? "yes" : "no")
            << " corLCL=" << (mHaveCorrectLCL ? "yes" : "no")
            << " fail=" << (mConsensusFail ? "yes" : "no");
        WriteLog (lsDEBUG, LedgerConsensus)
            << "Report: Prev = " << mPrevLedgerHash
            << ":" << mPreviousLedger->getLedgerSeq ();
        WriteLog (lsDEBUG, LedgerConsensus)
            << "Report: TxSt = " << set->getHash ()
            << ", close " << closeTime << (closeTimeCorrect ? "" : "X");

        // Put failed transactions into a deterministic order
        CanonicalTXSet retriableTransactions (set->getHash ());

        // Build the new last closed ledger
        Ledger::pointer newLCL
            = std::make_shared<Ledger> (false
            , *mPreviousLedger);

        // Set up to write SHAMap changes to our database,
        //   perform updates, extract changes
        WriteLog (lsDEBUG, LedgerConsensus)
            << "Applying consensus set transactions to the"
            << " last closed ledger";
        applyTransactions (set, newLCL, newLCL, retriableTransactions, false);
        newLCL->updateSkipList ();
        newLCL->setClosed ();

        int asf = newLCL->peekAccountStateMap ()->flushDirty (
            hotACCOUNT_NODE, newLCL->getLedgerSeq());
        int tmf = newLCL->peekTransactionMap ()->flushDirty (
            hotTRANSACTION_NODE, newLCL->getLedgerSeq());
        WriteLog (lsDEBUG, LedgerConsensus) << "Flushed " << asf << " account and " <<
            tmf << "transaction nodes";

        // Accept ledger
        newLCL->setAccepted (closeTime, mCloseResolution, closeTimeCorrect);

        // And stash the ledger in the ledger master
        if (getApp().getLedgerMaster().storeLedger (newLCL))
            WriteLog (lsDEBUG, LedgerConsensus)
                << "Consensus built ledger we already had";
        else if (getApp().getInboundLedgers().find (newLCL->getHash()))
            WriteLog (lsDEBUG, LedgerConsensus)
                << "Consensus built ledger we were acquiring";
        else
            WriteLog (lsDEBUG, LedgerConsensus)
                << "Consensus built new ledger";

        WriteLog (lsDEBUG, LedgerConsensus)
            << "Report: NewL  = " << newLCL->getHash ()
            << ":" << newLCL->getLedgerSeq ();
        uint256 newLCLHash = newLCL->getHash ();
        // Tell directly connected peers that we have a new LCL
        statusChange (protocol::neACCEPTED_LEDGER, *newLCL);

        if (mValidating && !mConsensusFail)
        {
            // Build validation
            uint256 signingHash;
            STValidation::pointer v =
                std::make_shared<STValidation>
                (newLCLHash, getApp().getOPs ().getValidationTimeNC ()
                , mValPublic, mProposing);
            v->setFieldU32 (sfLedgerSequence, newLCL->getLedgerSeq ());
            addLoad(v);  // Our network load

            if (((newLCL->getLedgerSeq () + 1) % 256) == 0)
            // next ledger is flag ledger
            {
                // Suggest fee changes and new features
                m_feeVote.doValidation (newLCL, *v);
                getApp().getAmendmentTable ().doValidation (newLCL, *v);
            }

            v->sign (signingHash, mValPrivate);
            v->setTrusted ();
            // suppress it if we receive it - FIXME: wrong suppression
            getApp().getHashRouter ().addSuppression (signingHash);
            getApp().getValidations ().addValidation (v, "local");
            getApp().getOPs ().setLastValidation (v);
            Blob validation = v->getSigned ();
            protocol::TMValidation val;
            val.set_validation (&validation[0], validation.size ());
            // Send signed validation to all of our directly connected peers
            getApp().overlay().send(val);
            WriteLog (lsINFO, LedgerConsensus)
                << "CNF Val " << newLCLHash;
        }
        else
            WriteLog (lsINFO, LedgerConsensus)
                << "CNF newLCL " << newLCLHash;

        // See if we can accept a ledger as fully-validated
        getApp().getLedgerMaster().consensusBuilt (newLCL);

        // Build new open ledger
        Ledger::pointer newOL = std::make_shared<Ledger>
            (true, *newLCL);

        // Apply disputed transactions that didn't get in
        TransactionEngine engine (newOL);
        bool anyDisputes = false;
        for (auto& it : mDisputes)
        {
            if (!it.second->getOurVote ())
            {
                // we voted NO
                try
                {
                    WriteLog (lsDEBUG, LedgerConsensus)
                        << "Test applying disputed transaction that did"
                        << " not get in";
                    SerialIter sit (it.second->peekTransaction ());
                    STTx::pointer txn
                        = std::make_shared<STTx>(sit);

                    retriableTransactions.push_back (txn);
                    anyDisputes = true;
                }
                catch (...)
                {
                    WriteLog (lsDEBUG, LedgerConsensus)
                        << "Failed to apply transaction we voted NO on";
                }
            }
        }

        if (anyDisputes)
        {
            applyTransactions (std::shared_ptr<SHAMap>(),
                newOL, newLCL, retriableTransactions, true);
        }

        {
            auto lock = beast::make_lock(getApp().getMasterMutex(), std::defer_lock);
            LedgerMaster::ScopedLockType sl
                (getApp().getLedgerMaster ().peekMutex (), std::defer_lock);
            std::lock(lock, sl);

            // Apply transactions from the old open ledger
            Ledger::pointer oldOL = getApp().getLedgerMaster().getCurrentLedger();
            if (oldOL->peekTransactionMap()->getHash().isNonZero ())
            {
                WriteLog (lsDEBUG, LedgerConsensus)
                    << "Applying transactions from current open ledger";
                applyTransactions (oldOL->peekTransactionMap (),
                    newOL, newLCL, retriableTransactions, true);
            }

            // Apply local transactions
            TransactionEngine engine (newOL);
            m_localTX.apply (engine);

            // We have a new Last Closed Ledger and new Open Ledger
            getApp().getLedgerMaster ().pushLedger (newLCL, newOL);
        }

        mNewLedgerHash = newLCL->getHash ();
        mState = lcsACCEPTED;

        if (mValidating)
        {
            // see how close our close time is to other node's
            //  close time reports, and update our clock.
            WriteLog (lsINFO, LedgerConsensus)
                << "We closed at "
                << beast::lexicalCastThrow <std::string> (mCloseTime);
            std::uint64_t closeTotal = mCloseTime;
            int closeCount = 1;

            for (auto it = mCloseTimes.begin ()
                , end = mCloseTimes.end (); it != end; ++it)
            {
                // FIXME: Use median, not average
                WriteLog (lsINFO, LedgerConsensus)
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
            WriteLog (lsINFO, LedgerConsensus)
                << "Our close offset is estimated at "
                << offset << " (" << closeCount << ")";
            getApp().getOPs ().closeTimeOffset (offset);
        }
    }

    /**
      Compare two proposed transaction sets and create disputed
        transctions structures for any mismatches

      @param m1 One transaction set
      @param m2 The other transaction set
    */
    void createDisputes (std::shared_ptr<SHAMap> const& m1,
                         std::shared_ptr<SHAMap> const& m2)
    {
        if (m1->getHash() == m2->getHash())
            return;

        WriteLog (lsDEBUG, LedgerConsensus) << "createDisputes "
            << m1->getHash() << " to " << m2->getHash();
        SHAMap::Delta differences;
        m1->compare (m2, differences, 16384);

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
        WriteLog (lsDEBUG, LedgerConsensus) << dc << " differences found";
    }

    /**
      Add a disputed transaction (one that at least one node wants
      in the consensus set and at least one node does not) to our tracking

      @param txID The ID of the disputed transaction
      @param tx   The data of the disputed transaction
    */
    void addDisputedTransaction (uint256 const& txID, Blob const& tx)
    {
        if (mDisputes.find (txID) != mDisputes.end ())
            return;

        WriteLog (lsDEBUG, LedgerConsensus) << "Transaction "
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

        DisputedTx::pointer txn = std::make_shared<DisputedTx>
            (txID, tx, ourVote);
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
        if (getApp().getHashRouter ().setFlag (txID, SF_RELAYED))
        {
            protocol::TMTransaction msg;
            msg.set_rawtransaction (& (tx.front ()), tx.size ());
            msg.set_status (protocol::tsNEW);
            msg.set_receivetimestamp (getApp().getOPs ().getNetworkTimeNC ());
            getApp ().overlay ().foreach (send_always (
                std::make_shared<Message> (
                    msg, protocol::mtTRANSACTION)));
        }
    }

    /**
      Adjust the votes on all disputed transactions based
        on the set of peers taking this position

      @param map   A disputed position
      @param peers peers which are taking the position map
    */
    void adjustCount (std::shared_ptr<SHAMap> const& map,
                      const std::vector<NodeID>& peers)
    {
        for (auto& it : mDisputes)
        {
            bool setHas = map->hasItem (it.second->getTransactionID ());
            for (auto const& pit : peers)
                it.second->setVote (pit, setHas);
        }
    }

    /**
      Revoke our outstanding proposal, if any, and
      cease proposing at least until this round ends
    */
    void leaveConsensus ()
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

    /** Make and send a proposal
    */
    void propose ()
    {
        WriteLog (lsTRACE, LedgerConsensus) << "We propose: " <<
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

        Blob pubKey = mOurPosition->getPubKey ();
        Blob sig = mOurPosition->sign ();
        prop.set_nodepubkey (&pubKey[0], pubKey.size ());
        prop.set_signature (&sig[0], sig.size ());
        getApp().overlay().send(prop);
    }

    /** Let peers know that we a particular transactions set so they
       can fetch it from us.

      @param hash   The ID of the transaction.
      @param direct true if we have this transaction set locally, else a
                    directly connected peer has it.
    */
    void sendHaveTxSet (uint256 const& hash, bool direct)
    {
        protocol::TMHaveTransactionSet msg;
        msg.set_hash (hash.begin (), 256 / 8);
        msg.set_status (direct ? protocol::tsHAVE : protocol::tsCAN_GET);
        getApp ().overlay ().foreach (send_always (
            std::make_shared <Message> (
                msg, protocol::mtHAVE_SET)));
    }

    /**
      Round the close time to the close time resolution.

      @param closeTime The time to be rouned.
      @return          The rounded close time.
    */
    std::uint32_t roundCloseTime (std::uint32_t closeTime)
    {
        return Ledger::roundCloseTime (closeTime, mCloseResolution);
    }

    /** Send a node status change message to our directly connected peers

      @param event   The event which caused the status change.  This is
                     typically neACCEPTED_LEDGER or neCLOSING_LEDGER.
      @param ledger  The ledger associated with the event.
    */
    void statusChange (protocol::NodeEvent event, Ledger& ledger)
    {
        protocol::TMStatusChange s;

        if (!mHaveCorrectLCL)
            s.set_newevent (protocol::neLOST_SYNC);
        else
            s.set_newevent (event);

        s.set_ledgerseq (ledger.getLedgerSeq ());
        s.set_networktime (getApp().getOPs ().getNetworkTimeNC ());
        s.set_ledgerhashprevious(ledger.getParentHash ().begin (),
            std::decay_t<decltype(ledger.getParentHash ())>::bytes);
        s.set_ledgerhash (ledger.getHash ().begin (),
            std::decay_t<decltype(ledger.getHash ())>::bytes);

        std::uint32_t uMin, uMax;
        if (!getApp().getOPs ().getFullValidatedRange (uMin, uMax))
        {
            uMin = 0;
            uMax = 0;
        }
        else
        {
            // Don't advertise ledgers we're not willing to serve
            std::uint32_t early = getApp().getLedgerMaster().getEarliestFetch ();
            if (uMin < early)
               uMin = early;
        }
        s.set_firstseq (uMin);
        s.set_lastseq (uMax);
        getApp ().overlay ().foreach (send_always (
            std::make_shared <Message> (
                s, protocol::mtSTATUS_CHANGE)));
        WriteLog (lsTRACE, LedgerConsensus) << "send status change to peer";
    }

    /** Take an initial position on what we think the consensus should be
        based on the transactions that made it into our open ledger

      @param initialLedger The ledger that contains our initial position.
    */
    void takeInitialPosition (Ledger& initialLedger)
    {
        std::shared_ptr<SHAMap> initialSet;

        if ((getConfig ().RUN_STANDALONE || (mProposing && mHaveCorrectLCL))
                && ((mPreviousLedger->getLedgerSeq () % 256) == 0))
        {
            // previous ledger was flag ledger
            std::shared_ptr<SHAMap> preSet
                = initialLedger.peekTransactionMap ()->snapShot (true);
            m_feeVote.doVoting (mPreviousLedger, preSet);
            getApp().getAmendmentTable ().doVoting (mPreviousLedger, preSet);
            initialSet = preSet->snapShot (false);
        }
        else
            initialSet = initialLedger.peekTransactionMap ()->snapShot (false);

        // Tell the ledger master not to acquire the ledger we're probably building
        getApp().getLedgerMaster().setBuildingLedger (mPreviousLedger->getLedgerSeq () + 1);

        uint256 txSet = initialSet->getHash ();
        WriteLog (lsINFO, LedgerConsensus) << "initial position " << txSet;
        mapCompleteInternal (txSet, initialSet, false);

        if (mValidating)
        {
            mOurPosition = std::make_shared<LedgerProposal>
                           (mValPublic, mValPrivate
                            , initialLedger.getParentHash ()
                            , txSet, mCloseTime);
        }
        else
        {
            mOurPosition
                = std::make_shared<LedgerProposal>
                (initialLedger.getParentHash (), txSet, mCloseTime);
        }

        for (auto& it : mDisputes)
        {
            it.second->setOurVote (initialLedger.hasTransaction (it.first));
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
                    mCompares.insert(iit->second->getHash());
                    createDisputes (initialSet, iit->second);
                }
            }
        }

        if (mProposing)
            propose ();
    }

    /**
      For a given number of participants and required percent
      for consensus, how many participants must agree?

      @param size    number of validators
      @param percent desired percent for consensus
      @return number of participates which must agree
    */
    static int computePercent (int size, int percent)
    {
        int result = ((size * percent) + (percent / 2)) / 100;
        return (result == 0) ? 1 : result;
    }

    /**
       Called while trying to avalanche towards consensus.
       Adjusts our positions to try to agree with other validators.
    */
    void updateOurPositions ()
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
                WriteLog (lsWARNING, LedgerConsensus)
                    << "Removing stale proposal from " << peerID;
                for (auto& dt : mDisputes)
                    dt.second->unVote (peerID);
                it = mPeerPositions.erase (it);
            }
            else
            {
                // proposal is still fresh
                ++closeTimes[roundCloseTime (it->second->getCloseTime ())];
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
            closeTime = roundCloseTime (mOurPosition->getCloseTime ());
        }
        else
        {
            int participants = mPeerPositions.size ();
            if (mProposing)
            {
                ++closeTimes[roundCloseTime (mOurPosition->getCloseTime ())];
                ++participants;
            }

            // Threshold for non-zero vote
            int threshVote = computePercent (participants, neededWeight);

            // Threshold to declare consensus
            int threshConsensus = computePercent (participants, AV_CT_CONSENSUS_PCT);

            WriteLog (lsINFO, LedgerConsensus) << "Proposers:"
                << mPeerPositions.size () << " nw:" << neededWeight
                << " thrV:" << threshVote << " thrC:" << threshConsensus;

            for (auto it = closeTimes.begin ()
                , end = closeTimes.end (); it != end; ++it)
            {
                WriteLog (lsDEBUG, LedgerConsensus) << "CCTime: seq"
                    << mPreviousLedger->getLedgerSeq () + 1 << ": "
                    << it->first << " has " << it->second << ", "
                    << threshVote << " required";

                if (it->second >= threshVote)
                {
                    WriteLog (lsDEBUG, LedgerConsensus)
                        << "Close time consensus reached: " << it->first;
                    closeTime = it->first;
                    threshVote = it->second;

                    if (threshVote >= threshConsensus)
                        mHaveCloseTimeConsensus = true;
                }
            }

            // If we agree to disagree on the close time, don't delay consensus
            if (!mHaveCloseTimeConsensus && (closeTimes[0] > threshConsensus))
            {
                closeTime = 0;
                mHaveCloseTimeConsensus = true;
            }

            CondLog (!mHaveCloseTimeConsensus, lsDEBUG, LedgerConsensus)
                << "No CT consensus: Proposers:" << mPeerPositions.size ()
                << " Proposing:" << (mProposing ? "yes" : "no") << " Thresh:"
                << threshConsensus << " Pos:" << closeTime;
        }

        if (!changes &&
                ((closeTime != roundCloseTime (mOurPosition->getCloseTime ()))
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
            uint256 newHash = ourPosition->getHash ();
            WriteLog (lsINFO, LedgerConsensus)
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

    /** If we radically changed our consensus context for some reason,
        we need to replay recent proposals so that they're not lost.
    */
    void playbackProposals ()
    {
        for (auto const& it: getApp().getOPs ().peekStoredProposals ())
        {
            bool relay = false;
            for (auto const& proposal : it.second)
            {
                if (proposal->hasSignature ())
                {
                    // we have the signature but don't know the
                    //  ledger so couldn't verify
                    proposal->setPrevLedger (mPrevLedgerHash);

                    if (proposal->checkSign ())
                    {
                        WriteLog (lsINFO, LedgerConsensus)
                            << "Applying stored proposal";
                        relay = peerPosition (proposal);
                    }
                }
                else if (proposal->isPrevLedger (mPrevLedgerHash))
                    relay = peerPosition (proposal);

                if (relay)
                {
                    WriteLog (lsWARNING, LedgerConsensus)
                        << "We should do delayed relay of this proposal,"
                        << " but we cannot";
                }
            }
        }
    }

    /** We have just decided to close the ledger. Start the consensus timer,
       stash the close time, inform peers, and take a position
    */
    void closeLedger ()
    {
        checkOurValidation ();
        mState = lcsESTABLISH;
        mConsensusStartTime
            = std::chrono::steady_clock::now ();
        mCloseTime = getApp().getOPs ().getCloseTimeNC ();
        getApp().getOPs ().setLastCloseTime (mCloseTime);
        statusChange (protocol::neCLOSING_LEDGER, *mPreviousLedger);
        getApp().getLedgerMaster().applyHeldTransactions ();
        takeInitialPosition (*getApp().getLedgerMaster ().getCurrentLedger ());
    }

    /**
      If we missed a consensus round, we may be missing a validation.
      This will send an older owed validation if we previously missed it.
    */
    void checkOurValidation ()
    {
        // This only covers some cases - Fix for the case where we can't ever acquire the consensus ledger
        if (!mHaveCorrectLCL || !mValPublic.isSet ()
            || !mValPrivate.isSet ()
            || getApp().getOPs ().isNeedNetworkLedger ())
        {
            return;
        }

        STValidation::pointer lastVal
            = getApp().getOPs ().getLastValidation ();

        if (lastVal)
        {
            if (lastVal->getFieldU32 (sfLedgerSequence)
                == mPreviousLedger->getLedgerSeq ())
            {
                return;
            }
            if (lastVal->getLedgerHash () == mPrevLedgerHash)
                return;
        }

        uint256 signingHash;
        STValidation::pointer v
            = std::make_shared<STValidation>
            (mPreviousLedger->getHash ()
            , getApp().getOPs ().getValidationTimeNC (), mValPublic, false);
        addLoad(v);
        v->setTrusted ();
        v->sign (signingHash, mValPrivate);
            // FIXME: wrong supression
        getApp().getHashRouter ().addSuppression (signingHash);
        getApp().getValidations ().addValidation (v, "localMissing");
        Blob validation = v->getSigned ();
        protocol::TMValidation val;
        val.set_validation (&validation[0], validation.size ());
        getApp().getOPs ().setLastValidation (v);
        WriteLog (lsWARNING, LedgerConsensus) << "Sending partial validation";
    }

    /** We have a new LCL and must accept it
    */
    void beginAccept (bool synchronous)
    {
        std::shared_ptr<SHAMap> consensusSet
            = mAcquired[mOurPosition->getCurrentHash ()];

        if (!consensusSet)
        {
            WriteLog (lsFATAL, LedgerConsensus)
                << "We don't have a consensus set";
            abort ();
            return;
        }

        getApp().getOPs ().newLCL
            (mPeerPositions.size (), mCurrentMSeconds, mNewLedgerHash);

        if (synchronous)
            accept (consensusSet);
        else
        {
            getApp().getJobQueue().addJob (jtACCEPT, "acceptLedger",
                std::bind (&LedgerConsensusImp::accept, shared_from_this (), consensusSet));
        }
    }

    void endConsensus ()
    {
        getApp().getOPs ().endConsensus (mHaveCorrectLCL);
    }

    /** Add our load fee to our validation
    */
    void addLoad(STValidation::ref val)
    {
        std::uint32_t fee = std::max(
            getApp().getFeeTrack().getLocalFee(),
            getApp().getFeeTrack().getClusterFee());
        std::uint32_t ref = getApp().getFeeTrack().getLoadBase();
        if (fee > ref)
            val->setFieldU32(sfLoadFee, fee);
    }
private:
    LocalTxs& m_localTX;
    FeeVote& m_feeVote;

    // VFALCO TODO Rename these to look pretty
    enum LCState
    {
        lcsPRE_CLOSE,       // We haven't closed our ledger yet,
                            //  but others might have
        lcsESTABLISH,       // Establishing consensus
        lcsFINISHED,        // We have closed on a transaction set
        lcsACCEPTED,        // We have accepted/validated
                            //  a new last closed ledger
    };

    LCState mState;
    std::uint32_t mCloseTime;      // The wall time this ledger closed
    uint256 mPrevLedgerHash, mNewLedgerHash, mAcquiringLedger;
    Ledger::pointer mPreviousLedger;
    LedgerProposal::pointer mOurPosition;
    RippleAddress mValPublic, mValPrivate;
    bool mProposing, mValidating, mHaveCorrectLCL, mConsensusFail;

    int mCurrentMSeconds, mClosePercent, mCloseResolution;
    bool mHaveCloseTimeConsensus;

    std::chrono::steady_clock::time_point   mConsensusStartTime;
    int                             mPreviousProposers;
    int                             mPreviousMSeconds;

    // Convergence tracking, trusted peers indexed by hash of public key
    hash_map<NodeID, LedgerProposal::pointer>  mPeerPositions;

    // Transaction Sets, indexed by hash of transaction tree
    hash_map<uint256, std::shared_ptr<SHAMap>> mAcquired;

    // Disputed transactions
    hash_map<uint256, DisputedTx::pointer> mDisputes;
    hash_set<uint256> mCompares;

    // Close time estimates
    std::map<std::uint32_t, int> mCloseTimes;

    // nodes that have bowed out of this consensus process
    NodeIDSet mDeadNodes;
};

//------------------------------------------------------------------------------

LedgerConsensus::~LedgerConsensus ()
{
}

std::shared_ptr <LedgerConsensus>
make_LedgerConsensus (LocalTxs& localtx,
    LedgerHash const &prevLCLHash, Ledger::ref previousLedger,
        std::uint32_t closeTime, FeeVote& feeVote)
{
    return std::make_shared <LedgerConsensusImp> (localtx,
        prevLCLHash, previousLedger, closeTime, feeVote);
}

/** Apply a transaction to a ledger

  @param engine       The transaction engine containing the ledger.
  @param txn          The transaction to be applied to ledger.
  @param openLedger   true if ledger is open
  @param retryAssured true if the transaction should be retried on failure.
  @return             One of resultSuccess, resultFail or resultRetry.
*/
static
int applyTransaction (TransactionEngine& engine
    , STTx::ref txn, bool openLedger, bool retryAssured)
{
    // Returns false if the transaction has need not be retried.
    TransactionEngineParams parms = openLedger ? tapOPEN_LEDGER : tapNONE;

    if (retryAssured)
    {
        parms = static_cast<TransactionEngineParams> (parms | tapRETRY);
    }

    if ((getApp().getHashRouter ().getFlags (txn->getTransactionID ())
        & SF_SIGGOOD) == SF_SIGGOOD)
    {
        parms = static_cast<TransactionEngineParams>
            (parms | tapNO_CHECK_SIGN);
    }
    WriteLog (lsDEBUG, LedgerConsensus) << "TXN "
        << txn->getTransactionID ()
        << (openLedger ? " open" : " closed")
        << (retryAssured ? "/retry" : "/final");
    WriteLog (lsTRACE, LedgerConsensus) << txn->getJson (0);

    try
    {
        auto result = engine.applyTransaction (*txn, parms);

        if (result.second)
        {
            WriteLog (lsDEBUG, LedgerConsensus)
            << "Transaction applied: " << transHuman (result.first);
            return LedgerConsensusImp::resultSuccess;
        }

        if (isTefFailure (result.first) || isTemMalformed (result.first) ||
            isTelLocal (result.first))
        {
            // failure
            WriteLog (lsDEBUG, LedgerConsensus)
                << "Transaction failure: " << transHuman (result.first);
            return LedgerConsensusImp::resultFail;
        }

        WriteLog (lsDEBUG, LedgerConsensus)
            << "Transaction retry: " << transHuman (result.first);
        return LedgerConsensusImp::resultRetry;
    }
    catch (...)
    {
        WriteLog (lsWARNING, LedgerConsensus) << "Throws";
        return LedgerConsensusImp::resultFail;
    }
}

/** Apply a set of transactions to a ledger

  @param set                   The set of transactions to apply
  @param applyLedger           The ledger to which the transactions should
                               be applied.
  @param checkLedger           A reference ledger for determining error
                               messages (typically new last closed ledger).
  @param retriableTransactions collect failed transactions in this set
  @param openLgr               true if applyLedger is open, else false.
*/
void applyTransactions (std::shared_ptr<SHAMap> const& set,
    Ledger::ref applyLedger, Ledger::ref checkLedger,
    CanonicalTXSet& retriableTransactions, bool openLgr)
{
    TransactionEngine engine (applyLedger);

    if (set)
    {
        for (std::shared_ptr<SHAMapItem> item = set->peekFirstItem (); !!item;
            item = set->peekNextItem (item->getTag ()))
        {
            // If the checkLedger doesn't have the transaction
            if (!checkLedger->hasTransaction (item->getTag ()))
            {
                // Then try to apply the transaction to applyLedger
                WriteLog (lsDEBUG, LedgerConsensus) <<
                    "Processing candidate transaction: " << item->getTag ();
                try
                {
                    SerialIter sit (item->peekSerializer ());
                    STTx::pointer txn
                        = std::make_shared<STTx>(sit);
                    if (applyTransaction (engine, txn,
                              openLgr, true) == LedgerConsensusImp::resultRetry)
                    {
                        // On failure, stash the failed transaction for
                        // later retry.
                        retriableTransactions.push_back (txn);
                    }
                }
                catch (...)
                {
                    WriteLog (lsWARNING, LedgerConsensus) << "  Throws";
                }
            }
        }
    }

    int changes;
    bool certainRetry = true;
    // Attempt to apply all of the retriable transactions
    for (int pass = 0; pass < LEDGER_TOTAL_PASSES; ++pass)
    {
        WriteLog (lsDEBUG, LedgerConsensus) << "Pass: " << pass << " Txns: "
            << retriableTransactions.size ()
            << (certainRetry ? " retriable" : " final");
        changes = 0;

        auto it = retriableTransactions.begin ();

        while (it != retriableTransactions.end ())
        {
            try
            {
                switch (applyTransaction (engine, it->second,
                        openLgr, certainRetry))
                {
                case LedgerConsensusImp::resultSuccess:
                    it = retriableTransactions.erase (it);
                    ++changes;
                    break;

                case LedgerConsensusImp::resultFail:
                    it = retriableTransactions.erase (it);
                    break;

                case LedgerConsensusImp::resultRetry:
                    ++it;
                }
            }
            catch (...)
            {
                WriteLog (lsWARNING, LedgerConsensus)
                    << "Transaction throws";
                it = retriableTransactions.erase (it);
            }
        }

        WriteLog (lsDEBUG, LedgerConsensus) << "Pass: "
            << pass << " finished " << changes << " changes";

        // A non-retry pass made no changes
        if (!changes && !certainRetry)
            return;

        // Stop retriable passes
        if ((!changes) || (pass >= LEDGER_RETRY_PASSES))
            certainRetry = false;
    }

    // If there are any transactions left, we must have
    // tried them in at least one final pass
    assert (retriableTransactions.empty() || !certainRetry);
}

} // ripple
