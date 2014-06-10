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

#include <ripple/overlay/predicates.h>

namespace ripple {

SETUP_LOG (LedgerConsensus)

// #define TRUST_NETWORK

class LedgerConsensusImp
    : public LedgerConsensus
    , public std::enable_shared_from_this <LedgerConsensusImp>
    , public CountedObject <LedgerConsensusImp>
{
public:
    enum {resultSuccess, resultFail, resultRetry};

    static char const* getCountedObjectName () { return "LedgerConsensus"; }

    LedgerConsensusImp (clock_type& clock, LocalTxs& localtx,
        LedgerHash const & prevLCLHash, Ledger::ref previousLedger,
            std::uint32_t closeTime, FeeVote& feeVote)
        : m_clock (clock)
        , m_localTX (localtx)
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
            (boost::posix_time::microsec_clock::universal_time ())
    {
        WriteLog (lsDEBUG, LedgerConsensus) << "Creating consensus object";
        WriteLog (lsTRACE, LedgerConsensus)
            << "LCL:" << previousLedger->getHash () << ", ct=" << closeTime;
        mPreviousProposers = getApp().getOPs ().getPreviousProposers ();
        mPreviousMSeconds = getApp().getOPs ().getPreviousConvergeTime ();
        assert (mPreviousMSeconds);

        mCloseResolution = ContinuousLedgerTiming::getNextLedgerTimeResolution (
            mPreviousLedger->getCloseResolution (),
            mPreviousLedger->getCloseAgree (),
            previousLedger->getLedgerSeq () + 1);

        if (mValPublic.isSet () && mValPrivate.isSet ()
            && !getApp().getOPs ().isNeedNetworkLedger ())
        {
            WriteLog (lsINFO, LedgerConsensus)
                << "Entering consensus process, validating";
            mValidating = true;
            mProposing =
                getApp().getOPs ().getOperatingMode () == NetworkOPs::omFULL;
        }
        else
        {
            WriteLog (lsINFO, LedgerConsensus)
                << "Entering consensus process, watching";
            mProposing = mValidating = false;
        }

        mHaveCorrectLCL = (mPreviousLedger->getHash () == mPrevLedgerHash);

        if (!mHaveCorrectLCL)
        {
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
        else
            getApp().getOPs ().setProposing (mProposing, mValidating);
    }
    int startup ()
    {
        return 1;
    }

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
            ret["state"] = "open";
            break;

        case lcsESTABLISH:
            ret["state"] = "consensus";
            break;

        case lcsFINISHED:
            ret["state"] = "finished";
            break;

        case lcsACCEPTED:
            ret["state"] = "accepted";
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

            if (!mAcquiring.empty ())
            {
                Json::Value acq (Json::arrayValue);
                for (auto& at : mAcquiring)
                {
                    acq.append (to_string (at.first));
                }
                ret["acquiring"] = acq;
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

    /** Get a transaction tree,
        fetching it from the network is required and requested
    */
    SHAMap::pointer getTransactionTree (uint256 const& hash, bool doAcquire)
    {
        auto it = mAcquired.find (hash);

        if (it != mAcquired.end ())
            return it->second;

        if (mState == lcsPRE_CLOSE)
        {
            SHAMap::pointer currentMap
                = getApp().getLedgerMaster ().getCurrentLedger ()
                    ->peekTransactionMap ();

            if (currentMap->getHash () == hash)
            {
                WriteLog (lsDEBUG, LedgerConsensus)
                    << "Map " << hash << " is our current";
                currentMap = currentMap->snapShot (false);
                mapComplete (hash, currentMap, false);
                return currentMap;
            }
        }

        if (doAcquire)
        {
            TransactionAcquire::pointer& acquiring = mAcquiring[hash];

            if (!acquiring)
            {
                if (hash.isZero ())
                {
                    SHAMap::pointer empty = std::make_shared<SHAMap> (
                        smtTRANSACTION, std::ref (getApp().getFullBelowCache()));
                    mapComplete (hash, empty, false);
                    return empty;
                }

                acquiring = std::make_shared<TransactionAcquire> (hash, std::ref (m_clock));
                startAcquiring (acquiring);
            }
        }

        return SHAMap::pointer ();
    }

    /** We have a complete transaction set, typically acquired from the network
    */
    void mapComplete (uint256 const& hash, SHAMap::ref map, bool acquired)
    {
        CondLog (acquired, lsINFO, LedgerConsensus)
            << "We have acquired TXS " << hash;

        if (!map)
        {
            // this is an invalid/corrupt map
            mAcquired[hash] = map;
            mAcquiring.erase (hash);
            WriteLog (lsWARNING, LedgerConsensus)
                << "A trusted node directed us to acquire an invalid TXN map";
            return;
        }

        assert (hash == map->getHash ());

        auto it = mAcquired.find (hash);

        if (mAcquired.find (hash) != mAcquired.end ())
        {
            if (it->second)
            {
                mAcquiring.erase (hash);
                return; // we already have this map
            }

            // We previously failed to acquire this map, now we have it
            mAcquired.erase (hash);
        }

        if (mOurPosition && (!mOurPosition->isBowOut ())
            && (hash != mOurPosition->getCurrentHash ()))
        {
            // this could create disputed transactions
            auto it2 = mAcquired.find (mOurPosition->getCurrentHash ());

            if (it2 != mAcquired.end ())
            {
                assert ((it2->first == mOurPosition->getCurrentHash ())
                    && it2->second);
                mCompares.insert(hash);
                createDisputes (it2->second, map);
            }
            else
                assert (false); // We don't have our own position?!
        }
        else
            WriteLog (lsDEBUG, LedgerConsensus)
                << "Not ready to create disputes";

        mAcquired[hash] = map;
        mAcquiring.erase (hash);

        // Adjust tracking for each peer that takes this position
        std::vector<uint160> peers;
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

        sendHaveTxSet (hash, true);
    }

    /** Determine if we still need to acquire a transaction set from  network.
    If a transaction set is popular, we probably have it. If it's unpopular,
    we probably don't need it (and the peer that initially made us
    retrieve it has probably already changed its position)
    */
    bool stillNeedTXSet (uint256 const& hash)
    {
        if (mAcquired.find (hash) != mAcquired.end ())
            return false;

        for (auto const& it : mPeerPositions)
        {
            if (it.second->getCurrentHash () == hash)
                return true;
        }
        return false;
    }

    /** Check if our last closed ledger matches the network's
    */
    void checkLCL ()
    {
        uint256 netLgr = mPrevLedgerHash;
        int netLgrCount = 0;

        uint256 favoredLedger = mPrevLedgerHash; // Don't jump forward
        uint256 priorLedger;

        if (mHaveCorrectLCL)
            priorLedger = mPreviousLedger->getParentHash (); // don't jump back

        ripple::unordered_map<uint256, currentValidationCount> vals =
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
                << mPreviousLedger->getJson (0);

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

    /** Change our view of the last closed ledger
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

            mProposing = false;
            //      mValidating = false;
            mPeerPositions.clear ();
            mDisputes.clear ();
            mCloseTimes.clear ();
            mDeadNodes.clear ();
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

                mAcquiringLedger = mPrevLedgerHash;
                getApp().getJobQueue().addJob (jtADVANCE, "getConsensusLedger",
                    std::bind (
                        &InboundLedgers::findCreate,
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




    void timerEntry ()
    {
        if ((mState != lcsFINISHED) && (mState != lcsACCEPTED))
            checkLCL ();

        mCurrentMSeconds =
            (boost::posix_time::microsec_clock::universal_time ()
            - mConsensusStartTime).total_milliseconds ();
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

    // state handlers
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
            sinceClose = 1000 * (getApp().getOPs ().getCloseTimeNC ()
                - getApp().getOPs ().getLastCloseTime ());
            idleInterval = LEDGER_IDLE_INTERVAL;
        }

        idleInterval = std::max (idleInterval, LEDGER_IDLE_INTERVAL);
        idleInterval = std::max (idleInterval, 2 * mPreviousLedger->getCloseResolution ());

        if (ContinuousLedgerTiming::shouldClose (anyTransactions
            , mPreviousProposers, proposersClosed, proposersValidated
            , mPreviousMSeconds, sinceClose, mCurrentMSeconds
            , idleInterval))
        {
            closeLedger ();
        }
    }
    /** We are establishing a consensus
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

        return ContinuousLedgerTiming::haveConsensus (mPreviousProposers,
            agree + disagree, agree, currentValidations
            , mPreviousMSeconds, mCurrentMSeconds, forReal, mConsensusFail);
    }

    /** A server has taken a new position, adjust our tracking
    */
    bool peerPosition (LedgerProposal::ref newPosition)
    {
        uint160 peerID = newPosition->getPeerID ();

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

        SHAMap::pointer set
            = getTransactionTree (newPosition->getCurrentHash (), true);

        if (set)
        {
            for (auto& it : mDisputes)
                it.second->setVote (peerID, set->hasItem (it.first));
        }
        else
        {
            WriteLog (lsDEBUG, LedgerConsensus)
                << "Don't have tx set for peer";
            //      BOOST_FOREACH(u256_lct_pair& it, mDisputes)
            //          it.second->unVote(peerID);
        }

        return true;
    }

    /** A peer has informed us that it can give us a transaction set
    */
    bool peerHasSet (Peer::ptr const& peer, uint256 const& hashSet
        , protocol::TxSetStatus status)
    {
        if (status != protocol::tsHAVE) // Indirect requests for future support
            return true;

        std::vector< std::weak_ptr<Peer> >& set = mPeerData[hashSet];
        for (std::weak_ptr<Peer>& iit : set)
            if (iit.lock () == peer)
                return false;
        set.push_back (peer);

        auto acq (mAcquiring.find (hashSet));

        if (acq != mAcquiring.end ())
            getApp().getJobQueue().addJob(jtTXN_DATA, "peerHasTxnData",
                std::bind(&TransactionAcquire::peerHasVoid, acq->second, peer));

        return true;
    }

    /** A peer has sent us some nodes from a transaction set
    */
    SHAMapAddNode peerGaveNodes (Peer::ptr const& peer
        , uint256 const& setHash, const std::list<SHAMapNode>& nodeIDs
        , const std::list< Blob >& nodeData)
    {
        auto acq (mAcquiring.find (setHash));

        if (acq == mAcquiring.end ())
        {
            WriteLog (lsDEBUG, LedgerConsensus)
                << "Got TX data for set no longer acquiring: " << setHash;
            return SHAMapAddNode ();
        }
        // We must keep the set around during the function
        TransactionAcquire::pointer set = acq->second;
        return set->takeNodes (nodeIDs, nodeData, peer);
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
    */
    void accept (SHAMap::pointer set)
    {

        {
            Application::ScopedLockType lock
                (getApp ().getMasterLock ());

            // put our set where others can get it later
            if (set->getHash ().isNonZero ())
               getApp().getOPs ().takePosition (
                   mPreviousLedger->getLedgerSeq (), set);

            assert (set->getHash () == mOurPosition->getCurrentHash ());
            // these are now obsolete
            getApp().getOPs ().peekStoredProposals ().clear ();

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

            CanonicalTXSet failedTransactions (set->getHash ());

            Ledger::pointer newLCL
                = std::make_shared<Ledger> (false
                , std::ref (*mPreviousLedger));

            // Set up to write SHAMap changes to our database,
            //   perform updates, extract changes
            newLCL->peekTransactionMap ()->armDirty ();
            newLCL->peekAccountStateMap ()->armDirty ();
            WriteLog (lsDEBUG, LedgerConsensus)
                << "Applying consensus set transactions to the"
                << " last closed ledger";
            applyTransactions (set, newLCL, newLCL, failedTransactions, false);
            newLCL->updateSkipList ();
            newLCL->setClosed ();
            std::shared_ptr<SHAMap::DirtySet> acctNodes
                = newLCL->peekAccountStateMap ()->disarmDirty ();
            std::shared_ptr<SHAMap::DirtySet> txnNodes
                = newLCL->peekTransactionMap ()->disarmDirty ();

            // write out dirty nodes (temporarily done here)
            int fc;

            while ((fc = newLCL->peekAccountStateMap()->flushDirty (
                *acctNodes, 256, hotACCOUNT_NODE, newLCL->getLedgerSeq ())) > 0)
            {
                WriteLog (lsTRACE, LedgerConsensus)
                    << "Flushed " << fc << " dirty state nodes";
            }

            while ((fc = newLCL->peekTransactionMap()->flushDirty (
                *txnNodes, 256, hotTRANSACTION_NODE, newLCL->getLedgerSeq ())) > 0)
            {
                WriteLog (lsTRACE, LedgerConsensus)
                    << "Flushed " << fc << " dirty transaction nodes";
            }

            newLCL->setAccepted (closeTime, mCloseResolution, closeTimeCorrect);

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

            statusChange (protocol::neACCEPTED_LEDGER, *newLCL);

            if (mValidating && !mConsensusFail)
            {
                uint256 signingHash;
                SerializedValidation::pointer v =
                    std::make_shared<SerializedValidation>
                    (newLCLHash, getApp().getOPs ().getValidationTimeNC ()
                    , mValPublic, mProposing);
                v->setFieldU32 (sfLedgerSequence, newLCL->getLedgerSeq ());
                addLoad(v);

                if (((newLCL->getLedgerSeq () + 1) % 256) == 0)
                // next ledger is flag ledger
                {
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
                getApp ().overlay ().foreach (send_always (
                    std::make_shared <Message> (
                        val, protocol::mtVALIDATION)));
                WriteLog (lsINFO, LedgerConsensus)
                    << "CNF Val " << newLCLHash;
            }
            else
                WriteLog (lsINFO, LedgerConsensus)
                    << "CNF newLCL " << newLCLHash;

            // See if we can accept a ledger as fully-validated
            getApp().getLedgerMaster().consensusBuilt (newLCL);

            Ledger::pointer newOL = std::make_shared<Ledger>
                (true, std::ref (*newLCL));
            LedgerMaster::ScopedLockType sl
                (getApp().getLedgerMaster ().peekMutex ());

            // Apply disputed transactions that didn't get in
            TransactionEngine engine (newOL);
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
                        SerializerIterator sit (it.second->peekTransaction ());
                        SerializedTransaction::pointer txn
                            = std::make_shared<SerializedTransaction>
                            (std::ref (sit));

                        if (applyTransaction (engine, txn, newOL, true, false))
                        {
                            failedTransactions.push_back (txn);
                        }
                    }
                    catch (...)
                    {
                        WriteLog (lsDEBUG, LedgerConsensus)
                            << "Failed to apply transaction we voted NO on";
                    }
                }
            }

            WriteLog (lsDEBUG, LedgerConsensus)
                << "Applying transactions from current open ledger";
            applyTransactions (getApp().getLedgerMaster ().getCurrentLedger
                ()->peekTransactionMap (), newOL, newLCL,
                failedTransactions, true);

            {
                TransactionEngine engine (newOL);
                m_localTX.apply (engine);
            }

            getApp().getLedgerMaster ().pushLedger (newLCL, newOL);
            mNewLedgerHash = newLCL->getHash ();
            mState = lcsACCEPTED;
            sl.unlock ();

            if (mValidating)
            {
                // see how close our close time is to other node's
                //  close time reports
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
    }

    /** Begin acquiring a transaction set
    */
    void startAcquiring (TransactionAcquire::pointer acquire)
    {
        auto it = mPeerData.find (acquire->getHash ());

        if (it != mPeerData.end ())
        {
            // Add any peers we already know have his transaction set
            std::vector< std::weak_ptr<Peer> >& peerList = it->second;
            std::vector< std::weak_ptr<Peer> >::iterator pit
                = peerList.begin ();

            while (pit != peerList.end ())
            {
                Peer::ptr pr = pit->lock ();

                if (!pr)
                {
                    pit = peerList.erase (pit);
                }
                else
                {
                    acquire->peerHas (pr);
                    ++pit;
                }
            }
        }

        struct build_acquire_list
        {
            typedef void return_type;

            TransactionAcquire::pointer const& acquire;

            build_acquire_list (TransactionAcquire::pointer const& acq)
                : acquire(acq)
            { }

            return_type operator() (Peer::ptr const& peer) const
            {
                if (peer->hasTxSet (acquire->getHash ()))
                    acquire->peerHas (peer);
            }
        };

        getApp().overlay ().foreach (build_acquire_list (acquire));

        acquire->setTimer ();
    }

    // Where is this function?
    SHAMap::pointer find (uint256 const & hash);

    /** Compare two proposed transaction sets and create disputed
        transctions structures for any mismatches
    */
    void createDisputes (SHAMap::ref m1, SHAMap::ref m2)
    {
        if (m1->getHash() == m2->getHash())
            return;

        WriteLog (lsDEBUG, LedgerConsensus) << "createDisputes "
            << m1->getHash() << " to " << m2->getHash();
        SHAMap::Delta differences;
        m1->compare (m2, differences, 16384);

        int dc = 0;
        for (auto& pos : differences)
        {
            ++dc;
            // create disputed transactions (from the ledger that has them)
            if (pos.second.first)
            {
                // transaction is in first map
                assert (!pos.second.second);
                addDisputedTransaction (pos.first
                    , pos.second.first->peekData ());
            }
            else if (pos.second.second)
            {
                // transaction is in second map
                assert (!pos.second.first);
                addDisputedTransaction (pos.first
                    , pos.second.second->peekData ());
            }
            else // No other disagreement over a transaction should be possible
                assert (false);
        }
        WriteLog (lsDEBUG, LedgerConsensus) << dc << " differences found";
    }

    /** Add a disputed transaction (one that at least one node wants
    in the consensus set and at least one node does not) to our tracking
    */
    void addDisputedTransaction (uint256 const& txID, Blob const& tx)
    {
        if (mDisputes.find (txID) != mDisputes.end ())
            return;

        WriteLog (lsDEBUG, LedgerConsensus) << "Transaction "
            << txID << " is disputed";

        bool ourVote = false;

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

    /** Adjust the counts on all disputed transactions based
        on the set of peers taking this position
    */
    void adjustCount (SHAMap::ref map, const std::vector<uint160>& peers)
    {
        for (auto& it : mDisputes)
        {
            bool setHas = map->hasItem (it.second->getTransactionID ());
            for (auto const& pit : peers)
                it.second->setVote (pit, setHas);
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
        getApp ().overlay ().foreach (send_always (
            std::make_shared<Message> (
                prop, protocol::mtPROPOSE_LEDGER)));
    }

    /** Let peers know that we a particular transactions set so they
        can fetch it from us.
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

    /** Apply a set of transactions to a ledger
    */
    void applyTransactions (SHAMap::ref set, Ledger::ref applyLedger,
        Ledger::ref checkLedger, CanonicalTXSet& failedTransactions,
        bool openLgr)
    {
        TransactionEngine engine (applyLedger);

        for (SHAMapItem::pointer item = set->peekFirstItem (); !!item;
            item = set->peekNextItem (item->getTag ()))
            if (!checkLedger->hasTransaction (item->getTag ()))
            {
                WriteLog (lsINFO, LedgerConsensus)
                << "Processing candidate transaction: " << item->getTag ();
#ifndef TRUST_NETWORK

                try
                {
#endif
                    SerializerIterator sit (item->peekSerializer ());
                    SerializedTransaction::pointer txn
                        = std::make_shared<SerializedTransaction>
                        (std::ref (sit));

                    if (applyTransaction (engine, txn,
                        applyLedger, openLgr, true) == resultRetry)
                    {
                        failedTransactions.push_back (txn);
                    }

#ifndef TRUST_NETWORK
                }
                catch (...)
                {
                    WriteLog (lsWARNING, LedgerConsensus) << "  Throws";
                }

#endif
            }

        int changes;
        bool certainRetry = true;

        for (int pass = 0; pass < LEDGER_TOTAL_PASSES; ++pass)
        {
            WriteLog (lsDEBUG, LedgerConsensus) << "Pass: " << pass << " Txns: "
                << failedTransactions.size ()
                << (certainRetry ? " retriable" : " final");
            changes = 0;

            auto it = failedTransactions.begin ();

            while (it != failedTransactions.end ())
            {
                try
                {
                    switch (applyTransaction (engine, it->second,
                        applyLedger, openLgr, certainRetry))
                    {
                    case resultSuccess:
                        it = failedTransactions.erase (it);
                        ++changes;
                        break;

                    case resultFail:
                        it = failedTransactions.erase (it);
                        break;

                    case resultRetry:
                        ++it;
                    }
                }
                catch (...)
                {
                    WriteLog (lsWARNING, LedgerConsensus)
                        << "Transaction throws";
                    it = failedTransactions.erase (it);
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
    }

    /** Apply a transaction to a ledger
    */
    int applyTransaction (TransactionEngine& engine
        , SerializedTransaction::ref txn, Ledger::ref ledger
        , bool openLedger, bool retryAssured)
    {
        // Returns false if the transaction has need not be retried.
        TransactionEngineParams parms = openLedger ? tapOPEN_LEDGER : tapNONE;

        if (retryAssured)
        {
            parms = static_cast<TransactionEngineParams> (parms | tapRETRY);
        }

        if (getApp().getHashRouter ().setFlag (txn->getTransactionID ()
            , SF_SIGGOOD))
        {
            parms = static_cast<TransactionEngineParams>
                (parms | tapNO_CHECK_SIGN);
        }
        WriteLog (lsDEBUG, LedgerConsensus) << "TXN "
            << txn->getTransactionID ()
            << (openLedger ? " open" : " closed")
            << (retryAssured ? "/retry" : "/final");
        WriteLog (lsTRACE, LedgerConsensus) << txn->getJson (0);

        // VFALCO TODO figure out what this "trust network"
        //  is all about and why it needs exceptions.
#ifndef TRUST_NETWORK

        try
        {
#endif

            bool didApply;
            TER result = engine.applyTransaction (*txn, parms, didApply);

            if (didApply)
            {
                WriteLog (lsDEBUG, LedgerConsensus)
                << "Transaction success: " << transHuman (result);
                return resultSuccess;
            }

            if (isTefFailure (result) || isTemMalformed (result) || 
                isTelLocal (result))
            {
                // failure
                WriteLog (lsDEBUG, LedgerConsensus)
                    << "Transaction failure: " << transHuman (result);
                return resultFail;
            }

            WriteLog (lsDEBUG, LedgerConsensus)
                << "Transaction retry: " << transHuman (result);
            assert (!ledger->hasTransaction (txn->getTransactionID ()));
            return resultRetry;

#ifndef TRUST_NETWORK
        }
        catch (...)
        {
            WriteLog (lsWARNING, LedgerConsensus) << "Throws";
            return false;
        }

#endif
    }

    std::uint32_t roundCloseTime (std::uint32_t closeTime)
    {
        return Ledger::roundCloseTime (closeTime, mCloseResolution);
    }

    /** Send a node status change message to our peers
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
        uint256 hash = ledger.getParentHash ();
        s.set_ledgerhashprevious (hash.begin (), hash.size ());
        hash = ledger.getHash ();
        s.set_ledgerhash (hash.begin (), hash.size ());

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
    */
    void takeInitialPosition (Ledger& initialLedger)
    {
        SHAMap::pointer initialSet;

        if ((getConfig ().RUN_STANDALONE || (mProposing && mHaveCorrectLCL))
                && ((mPreviousLedger->getLedgerSeq () % 256) == 0))
        {
            // previous ledger was flag ledger
            SHAMap::pointer preSet
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
        mapComplete (txSet, initialSet, false);

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
        boost::unordered_set<uint256> found;

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

    // For a given number of participants and required percent
    // for consensus, how many participants must agree?
    static int computePercent (int size, int percent)
    {
        int result = ((size * percent) + (percent / 2)) / 100;
        return (result == 0) ? 1 : result;
    }

    void updateOurPositions ()
    {
        boost::posix_time::ptime peerCutoff
            = boost::posix_time::second_clock::universal_time ();
        boost::posix_time::ptime ourCutoff
            = peerCutoff - boost::posix_time::seconds (PROPOSE_INTERVAL);
        peerCutoff -= boost::posix_time::seconds (PROPOSE_FRESHNESS);

        bool changes = false;
        SHAMap::pointer ourPosition;
        //  std::vector<uint256> addedTx, removedTx;

        // Verify freshness of peer positions and compute close times
        std::map<std::uint32_t, int> closeTimes;
        auto it = mPeerPositions.begin ();

        while (it != mPeerPositions.end ())
        {
            if (it->second->isStale (peerCutoff))
            {
                // proposal is stale
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

                mapComplete (newHash, ourPosition, false);
            }
        }
    }

    /** If we radically changed our consensus context for some reason,
        we need to replay recent proposals so that they're not lost.
    */
    void playbackProposals ()
    {
        ripple::unordered_map < uint160,
              std::list<LedgerProposal::pointer> > & storedProposals
              = getApp().getOPs ().peekStoredProposals ();

        for (auto it = storedProposals.begin ()
            , end = storedProposals.end (); it != end; ++it)
        {
            bool relay = false;
            for (auto proposal : it->second)
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

    #if 0
    // FIXME: We can't do delayed relay because we don't have the signature
                std::set<Peer::ShortId> peers

                if (relay && getApp().getHashRouter ().swapSet (proposal.getSuppress (), set, SF_RELAYED))
                {
                    WriteLog (lsDEBUG, LedgerConsensus) << "Stored proposal delayed relay";
                    protocol::TMProposeSet set;
                    set.set_proposeseq
                    set.set_currenttxhash (, 256 / 8);
                    previousledger
                    closetime
                    nodepubkey
                    signature
                    getApp ().overlay ().foreach (send_if_not (
                        std::make_shared<Message> (
                            set, protocol::mtPROPOSE_LEDGER),
                                peer_in_set(peers)));
                }

    #endif
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
            = boost::posix_time::microsec_clock::universal_time ();
        mCloseTime = getApp().getOPs ().getCloseTimeNC ();
        getApp().getOPs ().setLastCloseTime (mCloseTime);
        statusChange (protocol::neCLOSING_LEDGER, *mPreviousLedger);
        getApp().getLedgerMaster().applyHeldTransactions ();
        takeInitialPosition (*getApp().getLedgerMaster ().getCurrentLedger ());
    }

    void checkOurValidation ()
    {
        // This only covers some cases - Fix for the case where we can't ever acquire the consensus ledger
        if (!mHaveCorrectLCL || !mValPublic.isSet ()
            || !mValPrivate.isSet ()
            || getApp().getOPs ().isNeedNetworkLedger ())
        {
            return;
        }

        SerializedValidation::pointer lastVal
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
        SerializedValidation::pointer v
            = std::make_shared<SerializedValidation>
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
    #if 0
        getApp ().overlay ().visit (RelayMessage (
            std::make_shared <Message> (
                val, protocol::mtVALIDATION)));
    #endif
        getApp().getOPs ().setLastValidation (v);
        WriteLog (lsWARNING, LedgerConsensus) << "Sending partial validation";
    }

    /** We have a new LCL and must accept it
    */
    void beginAccept (bool synchronous)
    {
        SHAMap::pointer consensusSet
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
    void addLoad(SerializedValidation::ref val)
    {
        std::uint32_t fee = std::max(
            getApp().getFeeTrack().getLocalFee(),
            getApp().getFeeTrack().getClusterFee());
        std::uint32_t ref = getApp().getFeeTrack().getLoadBase();
        if (fee > ref)
            val->setFieldU32(sfLoadFee, fee);
    }
private:
    clock_type& m_clock;
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

    boost::posix_time::ptime        mConsensusStartTime;
    int                             mPreviousProposers;
    int                             mPreviousMSeconds;

    // Convergence tracking, trusted peers indexed by hash of public key
    ripple::unordered_map<uint160, LedgerProposal::pointer> mPeerPositions;

    // Transaction Sets, indexed by hash of transaction tree
    ripple::unordered_map<uint256, SHAMap::pointer> mAcquired;
    ripple::unordered_map<uint256, TransactionAcquire::pointer> mAcquiring;

    // Peer sets
    ripple::unordered_map<uint256
        , std::vector< std::weak_ptr<Peer> > > mPeerData;

    // Disputed transactions
    ripple::unordered_map<uint256, DisputedTx::pointer> mDisputes;
    boost::unordered_set<uint256> mCompares;

    // Close time estimates
    std::map<std::uint32_t, int> mCloseTimes;

    // nodes that have bowed out of this consensus process
    boost::unordered_set<uint160> mDeadNodes;
};

//------------------------------------------------------------------------------

LedgerConsensus::~LedgerConsensus ()
{
}

std::shared_ptr <LedgerConsensus>
make_LedgerConsensus (LedgerConsensus::clock_type& clock, LocalTxs& localtx,
    LedgerHash const &prevLCLHash, Ledger::ref previousLedger,
        std::uint32_t closeTime, FeeVote& feeVote)
{
    return std::make_shared <LedgerConsensusImp> (clock, localtx,
        prevLCLHash, previousLedger, closeTime, feeVote);
}

} // ripple
