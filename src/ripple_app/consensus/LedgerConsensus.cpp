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

// #define TRUST_NETWORK

#define LC_DEBUG

SETUP_LOG (LedgerConsensus)

LedgerConsensus::LedgerConsensus (uint256 const& prevLCLHash, Ledger::ref previousLedger, uint32 closeTime)
    :  mState (lcsPRE_CLOSE), mCloseTime (closeTime), mPrevLedgerHash (prevLCLHash), mPreviousLedger (previousLedger),
       mValPublic (getConfig ().VALIDATION_PUB), mValPrivate (getConfig ().VALIDATION_PRIV), mConsensusFail (false),
       mCurrentMSeconds (0), mClosePercent (0), mHaveCloseTimeConsensus (false),
       mConsensusStartTime (boost::posix_time::microsec_clock::universal_time ())
{
    WriteLog (lsDEBUG, LedgerConsensus) << "Creating consensus object";
    WriteLog (lsTRACE, LedgerConsensus) << "LCL:" << previousLedger->getHash () << ", ct=" << closeTime;
    mPreviousProposers = getApp().getOPs ().getPreviousProposers ();
    mPreviousMSeconds = getApp().getOPs ().getPreviousConvergeTime ();
    assert (mPreviousMSeconds);

    mCloseResolution = ContinuousLedgerTiming::getNextLedgerTimeResolution (
                           mPreviousLedger->getCloseResolution (), mPreviousLedger->getCloseAgree (), previousLedger->getLedgerSeq () + 1);

    if (mValPublic.isSet () && mValPrivate.isSet () && !getApp().getOPs ().isNeedNetworkLedger ())
    {
        WriteLog (lsINFO, LedgerConsensus) << "Entering consensus process, validating";
        mValidating = true;
        mProposing = getApp().getOPs ().getOperatingMode () == NetworkOPs::omFULL;
    }
    else
    {
        WriteLog (lsINFO, LedgerConsensus) << "Entering consensus process, watching";
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
            WriteLog (lsINFO, LedgerConsensus) << "Entering consensus with: " << previousLedger->getHash ();
            WriteLog (lsINFO, LedgerConsensus) << "Correct LCL is: " << prevLCLHash;
        }
    }
    else
        getApp().getOPs ().setProposing (mProposing, mValidating);
}

void LedgerConsensus::checkOurValidation ()
{
    // This only covers some cases - Fix for the case where we can't ever acquire the consensus ledger
    if (!mHaveCorrectLCL || !mValPublic.isSet () || !mValPrivate.isSet () || getApp().getOPs ().isNeedNetworkLedger ())
        return;

    SerializedValidation::pointer lastVal = getApp().getOPs ().getLastValidation ();

    if (lastVal)
    {
        if (lastVal->getFieldU32 (sfLedgerSequence) == mPreviousLedger->getLedgerSeq ())
            return;

        if (lastVal->getLedgerHash () == mPrevLedgerHash)
            return;
    }

    uint256 signingHash;
    SerializedValidation::pointer v = boost::make_shared<SerializedValidation>
                                      (mPreviousLedger->getHash (), getApp().getOPs ().getValidationTimeNC (), mValPublic, false);
    addLoad(v);
    v->setTrusted ();
    v->sign (signingHash, mValPrivate);
    getApp().getHashRouter ().addSuppression (signingHash); // FIXME: wrong supression
    getApp().getValidations ().addValidation (v, "localMissing");
    Blob validation = v->getSigned ();
    protocol::TMValidation val;
    val.set_validation (&validation[0], validation.size ());
#if 0
    getApp().getPeers ().relayMessage (NULL,
                                      boost::make_shared<PackedMessage> (val, protocol::mtVALIDATION));
#endif
    getApp().getOPs ().setLastValidation (v);
    WriteLog (lsWARNING, LedgerConsensus) << "Sending partial validation";
}

/** Check if our last closed ledger matches the network's
*/
void LedgerConsensus::checkLCL ()
{
    uint256 netLgr = mPrevLedgerHash;
    int netLgrCount = 0;

    uint256 favoredLedger = mPrevLedgerHash;            // Don't jump forward
    uint256 priorLedger;

    if (mHaveCorrectLCL)
        priorLedger = mPreviousLedger->getParentHash (); // don't jump back

    boost::unordered_map<uint256, currentValidationCount> vals =
        getApp().getValidations ().getCurrentValidations (favoredLedger, priorLedger);

    typedef std::map<uint256, currentValidationCount>::value_type u256_cvc_pair;
    BOOST_FOREACH (u256_cvc_pair & it, vals)

    if ((it.second.first > netLgrCount) ||
            ((it.second.first == netLgrCount) && (it.first == mPrevLedgerHash)))
    {
        netLgr = it.first;
        netLgrCount = it.second.first;
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

        WriteLog (lsWARNING, LedgerConsensus) << "View of consensus changed during " << status << " (" << netLgrCount << ") status="
                                              << status << ", " << (mHaveCorrectLCL ? "CorrectLCL" : "IncorrectLCL");
        WriteLog (lsWARNING, LedgerConsensus) << mPrevLedgerHash << " to " << netLgr;
        WriteLog (lsWARNING, LedgerConsensus) << mPreviousLedger->getJson (0);

        if (ShouldLog (lsDEBUG, LedgerConsensus))
        {
            BOOST_FOREACH (u256_cvc_pair & it, vals)
            WriteLog (lsDEBUG, LedgerConsensus) << "V: " << it.first << ", " << it.second.first;
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
void LedgerConsensus::handleLCL (uint256 const& lclHash)
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
    Ledger::pointer newLCL = getApp().getLedgerMaster ().getLedgerByHash (lclHash);

    if (newLCL)
    {
        assert (newLCL->isClosed ());
        assert (newLCL->isImmutable ());
        assert (newLCL->getHash () == lclHash);
        mPreviousLedger = newLCL;
        mPrevLedgerHash = lclHash;
    }
    else if (!mAcquiringLedger || (mAcquiringLedger->getHash () != mPrevLedgerHash))
    {
        // need to start acquiring the correct consensus LCL
        WriteLog (lsWARNING, LedgerConsensus) << "Need consensus ledger " << mPrevLedgerHash;

        if (mAcquiringLedger)
            getApp().getInboundLedgers ().dropLedger (mAcquiringLedger->getHash ());

        mAcquiringLedger = getApp().getInboundLedgers ().findCreateConsensusLedger (mPrevLedgerHash);
        mHaveCorrectLCL = false;
        return;
    }
    else
        return;

    WriteLog (lsINFO, LedgerConsensus) << "Have the consensus ledger " << mPrevLedgerHash;
    mHaveCorrectLCL = true;

    mCloseResolution = ContinuousLedgerTiming::getNextLedgerTimeResolution (
                           mPreviousLedger->getCloseResolution (), mPreviousLedger->getCloseAgree (),
                           mPreviousLedger->getLedgerSeq () + 1);
}

/** Take an initial position on what we think the consensus should be
    based on the transactions that made it into our open ledger
*/
void LedgerConsensus::takeInitialPosition (Ledger& initialLedger)
{
    SHAMap::pointer initialSet;

    if ((getConfig ().RUN_STANDALONE || (mProposing && mHaveCorrectLCL))
            && ((mPreviousLedger->getLedgerSeq () % 256) == 0))
    {
        // previous ledger was flag ledger
        SHAMap::pointer preSet = initialLedger.peekTransactionMap ()->snapShot (true);
        getApp().getFeeVote ().doVoting (mPreviousLedger, preSet);
        getApp().getFeatureTable ().doVoting (mPreviousLedger, preSet);
        initialSet = preSet->snapShot (false);
    }
    else
        initialSet = initialLedger.peekTransactionMap ()->snapShot (false);

    uint256 txSet = initialSet->getHash ();
    WriteLog (lsINFO, LedgerConsensus) << "initial position " << txSet;
    mapComplete (txSet, initialSet, false);

    if (mValidating)
        mOurPosition = boost::make_shared<LedgerProposal>
                       (mValPublic, mValPrivate, initialLedger.getParentHash (), txSet, mCloseTime);
    else
        mOurPosition = boost::make_shared<LedgerProposal> (initialLedger.getParentHash (), txSet, mCloseTime);

    BOOST_FOREACH (u256_lct_pair & it, mDisputes)
    {
        it.second->setOurVote (initialLedger.hasTransaction (it.first));
    }

    // if any peers have taken a contrary position, process disputes
    boost::unordered_set<uint256> found;
    BOOST_FOREACH (u160_prop_pair & it, mPeerPositions)
    {
        uint256 set = it.second->getCurrentHash ();

        if (found.insert (set).second)
        {
            boost::unordered_map<uint256, SHAMap::pointer>::iterator iit = mAcquired.find (set);

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

/** Determine if we still need to acquire a transaction set from the network.
    If a transaction set is popular, we probably have it. If it's unpopular,
    we probably don't need it (and the peer that initially made us
    retrieve it has probably already changed its position)
*/
bool LedgerConsensus::stillNeedTXSet (uint256 const& hash)
{
    if (mAcquired.find (hash) != mAcquired.end ())
        return false;

    BOOST_FOREACH (u160_prop_pair & it, mPeerPositions)
    {
        if (it.second->getCurrentHash () == hash)
            return true;
    }
    return false;
}

/** Compare two proposed transaction sets and create disputed
    transctions structures for any mismatches
*/
void LedgerConsensus::createDisputes (SHAMap::ref m1, SHAMap::ref m2)
{
    if (m1->getHash() == m2->getHash())
        return;

    WriteLog (lsDEBUG, LedgerConsensus) << "createDisputes " << m1->getHash() << " to " << m2->getHash();
    SHAMap::Delta differences;
    m1->compare (m2, differences, 16384);

    int dc = 0;
    typedef std::map<uint256, SHAMap::DeltaItem>::value_type u256_diff_pair;
    BOOST_FOREACH (u256_diff_pair & pos, differences)
    {
        ++dc;
        // create disputed transactions (from the ledger that has them)
        if (pos.second.first)
        {
            // transaction is in first map
            assert (!pos.second.second);
            addDisputedTransaction (pos.first, pos.second.first->peekData ());
        }
        else if (pos.second.second)
        {
            // transaction is in second map
            assert (!pos.second.first);
            addDisputedTransaction (pos.first, pos.second.second->peekData ());
        }
        else // No other disagreement over a transaction should be possible
            assert (false);
    }
    WriteLog (lsDEBUG, LedgerConsensus) << dc << " differences found";
}

/** We have a complete transaction set, typically one acuired from the network
*/
void LedgerConsensus::mapComplete (uint256 const& hash, SHAMap::ref map, bool acquired)
{
    CondLog (acquired, lsINFO, LedgerConsensus) << "We have acquired TXS " << hash;

    if (!map)
    {
        // this is an invalid/corrupt map
        mAcquired[hash] = map;
        mAcquiring.erase (hash);
        WriteLog (lsWARNING, LedgerConsensus) << "A trusted node directed us to acquire an invalid TXN map";
        return;
    }

    assert (hash == map->getHash ());

    boost::unordered_map<uint256, SHAMap::pointer>::iterator it = mAcquired.find (hash);

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

    if (mOurPosition && (!mOurPosition->isBowOut ()) && (hash != mOurPosition->getCurrentHash ()))
    {
        // this could create disputed transactions
        boost::unordered_map<uint256, SHAMap::pointer>::iterator it2 = mAcquired.find (mOurPosition->getCurrentHash ());

        if (it2 != mAcquired.end ())
        {
            assert ((it2->first == mOurPosition->getCurrentHash ()) && it2->second);
            mCompares.insert(hash);
            createDisputes (it2->second, map);
        }
        else
            assert (false); // We don't have our own position?!
    }
    else
        WriteLog (lsDEBUG, LedgerConsensus) << "Not ready to create disputes";

    mAcquired[hash] = map;
    mAcquiring.erase (hash);

    // Adjust tracking for each peer that takes this position
    std::vector<uint160> peers;
    BOOST_FOREACH (u160_prop_pair & it, mPeerPositions)
    {
        if (it.second->getCurrentHash () == map->getHash ())
            peers.push_back (it.second->getPeerID ());
    }

    if (!peers.empty ())
        adjustCount (map, peers);
    else
    {
        CondLog (acquired, lsWARNING, LedgerConsensus) << "By the time we got the map " << hash << " no peers were proposing it";
    }

    sendHaveTxSet (hash, true);
}

/** Let peers know that we a particular transactions set so they
    can fetch it from us.
*/
void LedgerConsensus::sendHaveTxSet (uint256 const& hash, bool direct)
{
    protocol::TMHaveTransactionSet msg;
    msg.set_hash (hash.begin (), 256 / 8);
    msg.set_status (direct ? protocol::tsHAVE : protocol::tsCAN_GET);
    PackedMessage::pointer packet = boost::make_shared<PackedMessage> (msg, protocol::mtHAVE_SET);
    getApp().getPeers ().relayMessage (NULL, packet);
}

/** Adjust the counts on all disputed transactions based on the set of peers taking this position
*/
void LedgerConsensus::adjustCount (SHAMap::ref map, const std::vector<uint160>& peers)
{
    BOOST_FOREACH (u256_lct_pair & it, mDisputes)
    {
        bool setHas = map->hasItem (it.second->getTransactionID ());
        BOOST_FOREACH (const uint160 & pit, peers)
            it.second->setVote (pit, setHas);
    }
}

/** Send a node status change message to our peers
*/
void LedgerConsensus::statusChange (protocol::NodeEvent event, Ledger& ledger)
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

    uint32 uMin, uMax;
    if (!getApp().getOPs ().getFullValidatedRange (uMin, uMax))
    {
        uMin = 0;
        uMax = 0;
    }
    s.set_firstseq (uMin);
    s.set_lastseq (uMax);

    PackedMessage::pointer packet = boost::make_shared<PackedMessage> (s, protocol::mtSTATUS_CHANGE);
    getApp().getPeers ().relayMessage (NULL, packet);
    WriteLog (lsTRACE, LedgerConsensus) << "send status change to peer";
}

int LedgerConsensus::startup ()
{
    return 1;
}

void LedgerConsensus::statePreClose ()
{
    // it is shortly before ledger close time
    bool anyTransactions = getApp().getLedgerMaster ().getCurrentLedger ()->peekTransactionMap ()->getHash ().isNonZero ();
    int proposersClosed = mPeerPositions.size ();
    int proposersValidated = getApp().getValidations ().getTrustedValidationCount (mPrevLedgerHash);

    // This ledger is open. This computes how long since the last ledger closed
    int sinceClose;
    int idleInterval = 0;

    if (mHaveCorrectLCL && mPreviousLedger->getCloseAgree ())
    {
        // we can use consensus timing
        sinceClose = 1000 * (getApp().getOPs ().getCloseTimeNC () - mPreviousLedger->getCloseTimeNC ());
        idleInterval = 2 * mPreviousLedger->getCloseResolution ();

        if (idleInterval < LEDGER_IDLE_INTERVAL)
            idleInterval = LEDGER_IDLE_INTERVAL;
    }
    else
    {
        sinceClose = 1000 * (getApp().getOPs ().getCloseTimeNC () - getApp().getOPs ().getLastCloseTime ());
        idleInterval = LEDGER_IDLE_INTERVAL;
    }

    if (ContinuousLedgerTiming::shouldClose (anyTransactions, mPreviousProposers, proposersClosed, proposersValidated,
            mPreviousMSeconds, sinceClose, mCurrentMSeconds, idleInterval))
    {
        closeLedger ();
    }
}

/** We have just decided to close the ledger. Start the consensus timer,
   stash the close time, inform peers, and take a position
*/
void LedgerConsensus::closeLedger ()
{
    checkOurValidation ();
    mState = lcsESTABLISH;
    mConsensusStartTime = boost::posix_time::microsec_clock::universal_time ();
    mCloseTime = getApp().getOPs ().getCloseTimeNC ();
    getApp().getOPs ().setLastCloseTime (mCloseTime);
    statusChange (protocol::neCLOSING_LEDGER, *mPreviousLedger);
    takeInitialPosition (*getApp().getLedgerMaster ().closeLedger (true));
}

/** We are establishing a consensus
*/
void LedgerConsensus::stateEstablish ()
{

    // Give everyone a chance to take an initial position
    if (mCurrentMSeconds < LEDGER_MIN_CONSENSUS)
        return;

    updateOurPositions ();

    if (!mHaveCloseTimeConsensus)
    {
        CondLog (haveConsensus (false), lsINFO, LedgerConsensus) << "We have TX consensus but not CT consensus";
    }
    else if (haveConsensus (true))
    {
        WriteLog (lsINFO, LedgerConsensus) << "Converge cutoff (" << mPeerPositions.size () << " participants)";
        mState = lcsFINISHED;
        beginAccept (false);
    }
}

void LedgerConsensus::stateFinished ()
{
    // we are processing the finished ledger
    // logic of calculating next ledger advances us out of this state
    // nothing to do
}

void LedgerConsensus::stateAccepted ()
{
    // we have accepted a new ledger
    endConsensus ();
}

void LedgerConsensus::timerEntry ()
{
    if ((mState != lcsFINISHED) && (mState != lcsACCEPTED))
        checkLCL ();

    mCurrentMSeconds =
        (boost::posix_time::microsec_clock::universal_time () - mConsensusStartTime).total_milliseconds ();
    mClosePercent = mCurrentMSeconds * 100 / mPreviousMSeconds;

    switch (mState)
    {
    case lcsPRE_CLOSE:
        statePreClose ();
        return;

    case lcsESTABLISH:
        stateEstablish ();

        if (mState != lcsFINISHED) return;

        fallthru ();

    case lcsFINISHED:
        stateFinished ();

        if (mState != lcsACCEPTED) return;

        fallthru ();

    case lcsACCEPTED:
        stateAccepted ();
        return;
    }

    assert (false);
}

void LedgerConsensus::updateOurPositions ()
{
    boost::posix_time::ptime peerCutoff = boost::posix_time::second_clock::universal_time ();
    boost::posix_time::ptime ourCutoff = peerCutoff - boost::posix_time::seconds (PROPOSE_INTERVAL);
    peerCutoff -= boost::posix_time::seconds (PROPOSE_FRESHNESS);

    bool changes = false;
    SHAMap::pointer ourPosition;
    //  std::vector<uint256> addedTx, removedTx;

    // Verify freshness of peer positions and compute close times
    std::map<uint32, int> closeTimes;
    boost::unordered_map<uint160, LedgerProposal::pointer>::iterator it = mPeerPositions.begin ();

    while (it != mPeerPositions.end ())
    {
        if (it->second->isStale (peerCutoff))
        {
            // proposal is stale
            uint160 peerID = it->second->getPeerID ();
            WriteLog (lsWARNING, LedgerConsensus) << "Removing stale proposal from " << peerID;
            BOOST_FOREACH (u256_lct_pair & it, mDisputes)
            it.second->unVote (peerID);
            it = mPeerPositions.erase (it);
        }
        else
        {
            // proposal is still fresh
            ++closeTimes[roundCloseTime (it->second->getCloseTime ())];
            ++it;
        }
    }

    BOOST_FOREACH (u256_lct_pair & it, mDisputes)
    {
        // Because the threshold for inclusion increases, time can change our position on a dispute
        if (it.second->updateVote (mClosePercent, mProposing))
        {
            if (!changes)
            {
                ourPosition = mAcquired[mOurPosition->getCurrentHash ()]->snapShot (true);
                assert (ourPosition);
                changes = true;
            }

            if (it.second->getOurVote ()) // now a yes
            {
                ourPosition->addItem (SHAMapItem (it.first, it.second->peekTransaction ()), true, false);
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

    uint32 closeTime = 0;
    mHaveCloseTimeConsensus = false;

    if (mPeerPositions.empty ())
    {
        // no other times
        mHaveCloseTimeConsensus = true;
        closeTime = roundCloseTime (mOurPosition->getCloseTime ());
    }
    else
    {
        int threshVote = mPeerPositions.size ();        // Threshold for non-zero vote
        int threshConsensus = mPeerPositions.size ();   // Threshold to declare consensus

        if (mProposing)
        {
            ++closeTimes[roundCloseTime (mOurPosition->getCloseTime ())];
            ++threshVote;
            ++threshConsensus;
        }

        threshVote = ((threshVote * neededWeight) + (neededWeight / 2)) / 100;
        threshConsensus = ((threshConsensus * AV_CT_CONSENSUS_PCT) + (AV_CT_CONSENSUS_PCT / 2)) / 100;

        if (threshVote == 0)
            threshVote = 1;

        if (threshConsensus == 0)
            threshConsensus = 1;

        WriteLog (lsINFO, LedgerConsensus) << "Proposers:" << mPeerPositions.size () << " nw:" << neededWeight
                                           << " thrV:" << threshVote << " thrC:" << threshConsensus;

        for (std::map<uint32, int>::iterator it = closeTimes.begin (), end = closeTimes.end (); it != end; ++it)
        {
            WriteLog (lsDEBUG, LedgerConsensus) << "CCTime: seq" << mPreviousLedger->getLedgerSeq () + 1 << ": " <<
                                                it->first << " has " << it->second << ", " << threshVote << " required";

            if (it->second >= threshVote)
            {
                WriteLog (lsDEBUG, LedgerConsensus) << "Close time consensus reached: " << it->first;
                closeTime = it->first;
                threshVote = it->second;

                if (threshVote >= threshConsensus)
                    mHaveCloseTimeConsensus = true;
            }
        }

        CondLog (!mHaveCloseTimeConsensus, lsDEBUG, LedgerConsensus) << "No CT consensus: Proposers:" << mPeerPositions.size ()
                << " Proposing:" << (mProposing ? "yes" : "no") << " Thresh:" << threshConsensus << " Pos:" << closeTime;
    }

    if (!changes &&
            ((closeTime != roundCloseTime (mOurPosition->getCloseTime ())) ||
             mOurPosition->isStale (ourCutoff)))
    {
        // close time changed or our position is stale
        ourPosition = mAcquired[mOurPosition->getCurrentHash ()]->snapShot (true);
        assert (ourPosition);
        changes = true; // We pretend our position changed to force a new proposal
    }

    if (changes)
    {
        uint256 newHash = ourPosition->getHash ();
        WriteLog (lsINFO, LedgerConsensus) << "Position change: CTime " << closeTime << ", tx " << newHash;

        if (mOurPosition->changePosition (newHash, closeTime))
        {
            if (mProposing)
                propose ();

            mapComplete (newHash, ourPosition, false);
        }
    }
}

/** Check if we've reached consensus
*/
bool LedgerConsensus::haveConsensus (bool forReal)
{
    // CHECKME: should possibly count unacquired TX sets as disagreeing
    int agree = 0, disagree = 0;
    uint256 ourPosition = mOurPosition->getCurrentHash ();

    BOOST_FOREACH (u160_prop_pair & it, mPeerPositions)
    {
        if (!it.second->isBowOut ())
        {
            if (it.second->getCurrentHash () == ourPosition)
                ++agree;
            else
            {
                WriteLog (lsDEBUG, LedgerConsensus) << it.first.GetHex () << " has " << it.second->getCurrentHash ().GetHex ();
                ++disagree;
                if (mCompares.count(it.second->getCurrentHash()) == 0)
                { // Make sure we have generated disputes
                    uint256 hash = it.second->getCurrentHash();
                    WriteLog (lsDEBUG, LedgerConsensus) << "We have not compared to " << hash;
                    boost::unordered_map<uint256, SHAMap::pointer>::iterator it1 = mAcquired.find (hash);
                    boost::unordered_map<uint256, SHAMap::pointer>::iterator it2 = mAcquired.find (mOurPosition->getCurrentHash ());
                    if ((it1 != mAcquired.end()) && (it2 != mAcquired.end()) && (it1->second) && (it2->second))
                    {
                        mCompares.insert(hash);
                        createDisputes(it2->second, it1->second);
                    }
                }
            }
        }
    }
    int currentValidations = getApp().getValidations ().getNodesAfter (mPrevLedgerHash);

    WriteLog (lsDEBUG, LedgerConsensus) << "Checking for TX consensus: agree=" << agree << ", disagree=" << disagree;

    return ContinuousLedgerTiming::haveConsensus (mPreviousProposers, agree + disagree, agree, currentValidations,
            mPreviousMSeconds, mCurrentMSeconds, forReal, mConsensusFail);
}

/** Get a transaction tree, fetching it from the network is required and requested
*/
SHAMap::pointer LedgerConsensus::getTransactionTree (uint256 const& hash, bool doAcquire)
{
    boost::unordered_map<uint256, SHAMap::pointer>::iterator it = mAcquired.find (hash);

    if (it != mAcquired.end ())
        return it->second;

    if (mState == lcsPRE_CLOSE)
    {
        SHAMap::pointer currentMap = getApp().getLedgerMaster ().getCurrentLedger ()->peekTransactionMap ();

        if (currentMap->getHash () == hash)
        {
            WriteLog (lsDEBUG, LedgerConsensus) << "Map " << hash << " is our current";
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
                SHAMap::pointer empty = boost::make_shared<SHAMap> (smtTRANSACTION);
                mapComplete (hash, empty, false);
                return empty;
            }

            acquiring = boost::make_shared<TransactionAcquire> (hash);
            startAcquiring (acquiring);
        }
    }

    return SHAMap::pointer ();
}

/** Begin acquiring a transaction set
*/
void LedgerConsensus::startAcquiring (TransactionAcquire::pointer acquire)
{
    boost::unordered_map< uint256, std::vector< boost::weak_ptr<Peer> > >::iterator it =
        mPeerData.find (acquire->getHash ());

    if (it != mPeerData.end ())
    {
        // Add any peers we already know have his transaction set
        std::vector< boost::weak_ptr<Peer> >& peerList = it->second;
        std::vector< boost::weak_ptr<Peer> >::iterator pit = peerList.begin ();

        while (pit != peerList.end ())
        {
            Peer::pointer pr = pit->lock ();

            if (!pr)
                pit = peerList.erase (pit);
            else
            {
                acquire->peerHas (pr);
                ++pit;
            }
        }
    }

    std::vector<Peer::pointer> peerList = getApp().getPeers ().getPeerVector ();
    BOOST_FOREACH (Peer::ref peer, peerList)
    {
        if (peer->hasTxSet (acquire->getHash ()))
            acquire->peerHas (peer);
    }

    acquire->setTimer ();
}

/** Make and send a proposal
*/
void LedgerConsensus::propose ()
{
    WriteLog (lsTRACE, LedgerConsensus) << "We propose: " <<
                                        (mOurPosition->isBowOut () ? std::string ("bowOut") : mOurPosition->getCurrentHash ().GetHex ());
    protocol::TMProposeSet prop;

    prop.set_currenttxhash (mOurPosition->getCurrentHash ().begin (), 256 / 8);
    prop.set_previousledger (mOurPosition->getPrevLedger ().begin (), 256 / 8);
    prop.set_proposeseq (mOurPosition->getProposeSeq ());
    prop.set_closetime (mOurPosition->getCloseTime ());

    Blob pubKey = mOurPosition->getPubKey ();
    Blob sig = mOurPosition->sign ();
    prop.set_nodepubkey (&pubKey[0], pubKey.size ());
    prop.set_signature (&sig[0], sig.size ());
    getApp().getPeers ().relayMessage (NULL,
            boost::make_shared<PackedMessage> (prop, protocol::mtPROPOSE_LEDGER));
}

/** Add a disputed transaction (one that at least one node wants in the consensus set and
    at least one node does not) to our tracking
*/
void LedgerConsensus::addDisputedTransaction (uint256 const& txID, Blob const& tx)
{
    if (mDisputes.find (txID) != mDisputes.end ())
        return;

    WriteLog (lsDEBUG, LedgerConsensus) << "Transaction " << txID << " is disputed";

    bool ourVote = false;

    if (mOurPosition)
    {
        boost::unordered_map<uint256, SHAMap::pointer>::iterator mit = mAcquired.find (mOurPosition->getCurrentHash ());

        if (mit != mAcquired.end ())
            ourVote = mit->second->hasItem (txID);
        else
            assert (false); // We don't have our own position?
    }

    DisputedTx::pointer txn = boost::make_shared<DisputedTx> (txID, tx, ourVote);
    mDisputes[txID] = txn;

    BOOST_FOREACH (u160_prop_pair & pit, mPeerPositions)
    {
        boost::unordered_map<uint256, SHAMap::pointer>::const_iterator cit =
            mAcquired.find (pit.second->getCurrentHash ());

        if ((cit != mAcquired.end ()) && cit->second)
            txn->setVote (pit.first, cit->second->hasItem (txID));
    }

    // If we didn't relay this transaction recently, relay it
    if (getApp().getHashRouter ().setFlag (txID, SF_RELAYED))
    {
        protocol::TMTransaction msg;
        msg.set_rawtransaction (& (tx.front ()), tx.size ());
        msg.set_status (protocol::tsNEW);
        msg.set_receivetimestamp (getApp().getOPs ().getNetworkTimeNC ());
        PackedMessage::pointer packet = boost::make_shared<PackedMessage> (msg, protocol::mtTRANSACTION);
        getApp().getPeers ().relayMessage (NULL, packet);
    }
}

/** A server has taken a new position, adjust our tracking
*/
bool LedgerConsensus::peerPosition (LedgerProposal::ref newPosition)
{
    uint160 peerID = newPosition->getPeerID ();

    if (mDeadNodes.find (peerID) != mDeadNodes.end ())
    {
        WriteLog (lsINFO, LedgerConsensus) << "Position from dead node: " << peerID.GetHex ();
        return false;
    }

    LedgerProposal::pointer& currentPosition = mPeerPositions[peerID];

    if (currentPosition)
    {
        assert (peerID == currentPosition->getPeerID ());

        if (newPosition->getProposeSeq () <= currentPosition->getProposeSeq ())
            return false;
    }

    if (newPosition->getProposeSeq () == 0)
    {
        // new initial close time estimate
        WriteLog (lsTRACE, LedgerConsensus) << "Peer reports close time as " << newPosition->getCloseTime ();
        ++mCloseTimes[newPosition->getCloseTime ()];
    }
    else if (newPosition->getProposeSeq () == LedgerProposal::seqLeave)
    {
        // peer bows out
        WriteLog (lsINFO, LedgerConsensus) << "Peer bows out: " << peerID.GetHex ();
        BOOST_FOREACH (u256_lct_pair & it, mDisputes)
        it.second->unVote (peerID);
        mPeerPositions.erase (peerID);
        mDeadNodes.insert (peerID);
        return true;
    }


    WriteLog (lsTRACE, LedgerConsensus) << "Processing peer proposal "
                                        << newPosition->getProposeSeq () << "/" << newPosition->getCurrentHash ();
    currentPosition = newPosition;

    SHAMap::pointer set = getTransactionTree (newPosition->getCurrentHash (), true);

    if (set)
    {
        BOOST_FOREACH (u256_lct_pair & it, mDisputes)
        it.second->setVote (peerID, set->hasItem (it.first));
    }
    else
    {
        WriteLog (lsDEBUG, LedgerConsensus) << "Don't have tx set for peer";
        //      BOOST_FOREACH(u256_lct_pair& it, mDisputes)
        //          it.second->unVote(peerID);
    }

    return true;
}

/** A peer has informed us that it can give us a transaction set
*/
bool LedgerConsensus::peerHasSet (Peer::ref peer, uint256 const& hashSet, protocol::TxSetStatus status)
{
    if (status != protocol::tsHAVE) // Indirect requests are for future support
        return true;

    std::vector< boost::weak_ptr<Peer> >& set = mPeerData[hashSet];
    BOOST_FOREACH (boost::weak_ptr<Peer>& iit, set)

    if (iit.lock () == peer)
        return false;

    set.push_back (peer);
    boost::unordered_map<uint256, TransactionAcquire::pointer>::iterator acq = mAcquiring.find (hashSet);

    if (acq != mAcquiring.end ())
    {
        TransactionAcquire::pointer ta = acq->second; // make sure it doesn't go away
        ta->peerHas (peer);
    }

    return true;
}

/** A peer has sent us some nodes from a transaction set
*/
SHAMapAddNode LedgerConsensus::peerGaveNodes (Peer::ref peer, uint256 const& setHash,
        const std::list<SHAMapNode>& nodeIDs, const std::list< Blob >& nodeData)
{
    boost::unordered_map<uint256, TransactionAcquire::pointer>::iterator acq = mAcquiring.find (setHash);

    if (acq == mAcquiring.end ())
    {
        WriteLog (lsDEBUG, LedgerConsensus) << "Got TX data for set no longer acquiring: " << setHash;
        return SHAMapAddNode ();
    }

    TransactionAcquire::pointer set = acq->second; // We must keep the set around during the function
    return set->takeNodes (nodeIDs, nodeData, peer);
}

/** We have a new LCL and must accept it
*/
void LedgerConsensus::beginAccept (bool synchronous)
{
    SHAMap::pointer consensusSet = mAcquired[mOurPosition->getCurrentHash ()];

    if (!consensusSet)
    {
        WriteLog (lsFATAL, LedgerConsensus) << "We don't have a consensus set";
        abort ();
        return;
    }

    getApp().getOPs ().newLCL (mPeerPositions.size (), mCurrentMSeconds, mNewLedgerHash);

    if (synchronous)
        accept (consensusSet, LoadEvent::pointer ());
    else
    { // FIXME: Post to JobQueue, not I/O service
        getApp().getIOService ().post (BIND_TYPE (&LedgerConsensus::accept, shared_from_this (), consensusSet,
                                      getApp().getJobQueue ().getLoadEvent (jtACCEPTLEDGER, "LedgerConsensus::beginAccept")));
    }
}

/** If we radically changed our consensus context for some reason, we need to
    replay recent proposals so that they're not lost.
*/
void LedgerConsensus::playbackProposals ()
{
    boost::unordered_map < uint160,
          std::list<LedgerProposal::pointer> > & storedProposals = getApp().getOPs ().peekStoredProposals ();

    for (boost::unordered_map< uint160, std::list<LedgerProposal::pointer> >::iterator
            it = storedProposals.begin (), end = storedProposals.end (); it != end; ++it)
    {
        bool relay = false;
        BOOST_FOREACH (LedgerProposal::ref proposal, it->second)
        {
            if (proposal->hasSignature ())
            {
                // we have the signature but don't know the ledger so couldn't verify
                proposal->setPrevLedger (mPrevLedgerHash);

                if (proposal->checkSign ())
                {
                    WriteLog (lsINFO, LedgerConsensus) << "Applying stored proposal";
                    relay = peerPosition (proposal);
                }
            }
            else if (proposal->isPrevLedger (mPrevLedgerHash))
                relay = peerPosition (proposal);

            if (relay)
            {
                WriteLog (lsWARNING, LedgerConsensus) << "We should do delayed relay of this proposal, but we cannot";
            }

#if 0 // FIXME: We can't do delayed relay because we don't have the signature
            std::set<uint64> peers

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
                PackedMessage::pointer message = boost::make_shared<PackedMessage> (set, protocol::mtPROPOSE_LEDGER);
                getApp().getPeers ().relayMessageBut (peers, message);
            }

#endif
        }
    }
}

// VFALCO TODO clean these macros up and put them somewhere. Try to eliminate them if possible.
#define LCAT_SUCCESS    0
#define LCAT_FAIL       1
#define LCAT_RETRY      2

/** Apply a transaction to a ledger
*/
int LedgerConsensus::applyTransaction (TransactionEngine& engine, SerializedTransaction::ref txn, Ledger::ref ledger,
                                       bool openLedger, bool retryAssured)
{
    // Returns false if the transaction has need not be retried.
    TransactionEngineParams parms = openLedger ? tapOPEN_LEDGER : tapNONE;

    if (retryAssured)
        parms = static_cast<TransactionEngineParams> (parms | tapRETRY);

    if (getApp().getHashRouter ().setFlag (txn->getTransactionID (), SF_SIGGOOD))
        parms = static_cast<TransactionEngineParams> (parms | tapNO_CHECK_SIGN);

    WriteLog (lsDEBUG, LedgerConsensus) << "TXN " << txn->getTransactionID ()
                                        << (openLedger ? " open" : " closed")
                                        << (retryAssured ? "/retry" : "/final");
    WriteLog (lsTRACE, LedgerConsensus) << txn->getJson (0);

    // VFALCO TODO figure out what this "trust network" is all about and why it needs exceptions.
#ifndef TRUST_NETWORK

    try
    {
#endif

        bool didApply;
        TER result = engine.applyTransaction (*txn, parms, didApply);

        if (didApply)
        {
            WriteLog (lsDEBUG, LedgerConsensus) << "Transaction success: " << transHuman (result);
            return LCAT_SUCCESS;
        }

        if (isTefFailure (result) || isTemMalformed (result) || isTelLocal (result))
        {
            // failure
            WriteLog (lsDEBUG, LedgerConsensus) << "Transaction failure: " << transHuman (result);
            return LCAT_FAIL;
        }

        WriteLog (lsDEBUG, LedgerConsensus) << "Transaction retry: " << transHuman (result);
        assert (!ledger->hasTransaction (txn->getTransactionID ()));
        return LCAT_RETRY;

#ifndef TRUST_NETWORK
    }
    catch (...)
    {
        WriteLog (lsWARNING, LedgerConsensus) << "Throws";
        return false;
    }

#endif
}

/** Apply a set of transactions to a ledger
*/
void LedgerConsensus::applyTransactions (SHAMap::ref set, Ledger::ref applyLedger,
        Ledger::ref checkLedger, CanonicalTXSet& failedTransactions, bool openLgr)
{
    TransactionEngine engine (applyLedger);

    for (SHAMapItem::pointer item = set->peekFirstItem (); !!item; item = set->peekNextItem (item->getTag ()))
        if (!checkLedger->hasTransaction (item->getTag ()))
        {
            WriteLog (lsINFO, LedgerConsensus) << "Processing candidate transaction: " << item->getTag ();
#ifndef TRUST_NETWORK

            try
            {
#endif
                SerializerIterator sit (item->peekSerializer ());
                SerializedTransaction::pointer txn = boost::make_shared<SerializedTransaction> (boost::ref (sit));

                if (applyTransaction (engine, txn, applyLedger, openLgr, true) == LCAT_RETRY)
                    failedTransactions.push_back (txn);

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
        WriteLog (lsDEBUG, LedgerConsensus) << "Pass: " << pass << " Txns: " << failedTransactions.size ()
                                            << (certainRetry ? " retriable" : " final");
        changes = 0;

        CanonicalTXSet::iterator it = failedTransactions.begin ();

        while (it != failedTransactions.end ())
        {
            try
            {
                switch (applyTransaction (engine, it->second, applyLedger, openLgr, certainRetry))
                {
                case LCAT_SUCCESS:
                    it = failedTransactions.erase (it);
                    ++changes;
                    break;

                case LCAT_FAIL:
                    it = failedTransactions.erase (it);
                    break;

                case LCAT_RETRY:
                    ++it;
                }
            }
            catch (...)
            {
                WriteLog (lsWARNING, LedgerConsensus) << "Transaction throws";
                it = failedTransactions.erase (it);
            }
        }

        WriteLog (lsDEBUG, LedgerConsensus) << "Pass: " << pass << " finished " << changes << " changes";

        // A non-retry pass made no changes
        if (!changes && !certainRetry)
            return;

        // Stop retriable passes
        if ((!changes) || (pass >= LEDGER_RETRY_PASSES))
            certainRetry = false;
    }
}

uint32 LedgerConsensus::roundCloseTime (uint32 closeTime)
{
    return Ledger::roundCloseTime (closeTime, mCloseResolution);
}

/** We have a new last closed ledger, process it
*/
void LedgerConsensus::accept (SHAMap::ref set, LoadEvent::pointer)
{
    if (set->getHash ().isNonZero ()) // put our set where others can get it later
        getApp().getOPs ().takePosition (mPreviousLedger->getLedgerSeq (), set);

    {
        Application::ScopedLockType lock (getApp ().getMasterLock (), __FILE__, __LINE__);

        assert (set->getHash () == mOurPosition->getCurrentHash ());

        getApp().getOPs ().peekStoredProposals ().clear (); // these are now obsolete

        uint32 closeTime = roundCloseTime (mOurPosition->getCloseTime ());
        bool closeTimeCorrect = true;

        if (closeTime == 0)
        {
            // we agreed to disagree
            closeTimeCorrect = false;
            closeTime = mPreviousLedger->getCloseTimeNC () + 1;
        }

        WriteLog (lsDEBUG, LedgerConsensus) << "Report: Prop=" << (mProposing ? "yes" : "no") << " val=" << (mValidating ? "yes" : "no") <<
                                            " corLCL=" << (mHaveCorrectLCL ? "yes" : "no") << " fail=" << (mConsensusFail ? "yes" : "no");
        WriteLog (lsDEBUG, LedgerConsensus) << "Report: Prev = " << mPrevLedgerHash << ":" << mPreviousLedger->getLedgerSeq ();
        WriteLog (lsDEBUG, LedgerConsensus) << "Report: TxSt = " << set->getHash () << ", close " << closeTime << (closeTimeCorrect ? "" : "X");

        CanonicalTXSet failedTransactions (set->getHash ());

        Ledger::pointer newLCL = boost::make_shared<Ledger> (false, boost::ref (*mPreviousLedger));

        // Set up to write SHAMap changes to our database, perform updates, extract changes
        newLCL->peekTransactionMap ()->armDirty ();
        newLCL->peekAccountStateMap ()->armDirty ();
        WriteLog (lsDEBUG, LedgerConsensus) << "Applying consensus set transactions to the last closed ledger";
        applyTransactions (set, newLCL, newLCL, failedTransactions, false);
        newLCL->updateSkipList ();
        newLCL->setClosed ();
        boost::shared_ptr<SHAMap::NodeMap> acctNodes = newLCL->peekAccountStateMap ()->disarmDirty ();
        boost::shared_ptr<SHAMap::NodeMap> txnNodes = newLCL->peekTransactionMap ()->disarmDirty ();

        // write out dirty nodes (temporarily done here)
        int fc;

        while ((fc = SHAMap::flushDirty (*acctNodes, 256, hotACCOUNT_NODE, newLCL->getLedgerSeq ())) > 0)
        {
            WriteLog (lsTRACE, LedgerConsensus) << "Flushed " << fc << " dirty state nodes";
        }

        while ((fc = SHAMap::flushDirty (*txnNodes, 256, hotTRANSACTION_NODE, newLCL->getLedgerSeq ())) > 0)
        {
            WriteLog (lsTRACE, LedgerConsensus) << "Flushed " << fc << " dirty transaction nodes";
        }

        newLCL->setAccepted (closeTime, mCloseResolution, closeTimeCorrect);
        newLCL->updateHash ();
        newLCL->setImmutable ();
        getApp().getLedgerMaster().storeLedger(newLCL);

        WriteLog (lsDEBUG, LedgerConsensus) << "Report: NewL  = " << newLCL->getHash () << ":" << newLCL->getLedgerSeq ();
        uint256 newLCLHash = newLCL->getHash ();

        if (ShouldLog (lsTRACE, LedgerConsensus))
        {
            WriteLog (lsTRACE, LedgerConsensus) << "newLCL";
            Json::Value p;
            newLCL->addJson (p, LEDGER_JSON_DUMP_TXRP | LEDGER_JSON_DUMP_STATE);
            WriteLog (lsTRACE, LedgerConsensus) << p;
        }

        statusChange (protocol::neACCEPTED_LEDGER, *newLCL);

        if (mValidating && !mConsensusFail)
        {
            uint256 signingHash;
            SerializedValidation::pointer v = boost::make_shared<SerializedValidation>
                                              (newLCLHash, getApp().getOPs ().getValidationTimeNC (), mValPublic, mProposing);
            v->setFieldU32 (sfLedgerSequence, newLCL->getLedgerSeq ());
            addLoad(v);

            if (((newLCL->getLedgerSeq () + 1) % 256) == 0) // next ledger is flag ledger
            {
                getApp().getFeeVote ().doValidation (newLCL, *v);
                getApp().getFeatureTable ().doValidation (newLCL, *v);
            }

            v->sign (signingHash, mValPrivate);
            v->setTrusted ();
            getApp().getHashRouter ().addSuppression (signingHash); // suppress it if we receive it - FIXME: wrong suppression
            getApp().getValidations ().addValidation (v, "local");
            getApp().getOPs ().setLastValidation (v);
            Blob validation = v->getSigned ();
            protocol::TMValidation val;
            val.set_validation (&validation[0], validation.size ());
            int j = getApp().getPeers ().relayMessage (NULL,
                    boost::make_shared<PackedMessage> (val, protocol::mtVALIDATION));
            WriteLog (lsINFO, LedgerConsensus) << "CNF Val " << newLCLHash << " to " << j << " peers";
        }
        else
            WriteLog (lsINFO, LedgerConsensus) << "CNF newLCL " << newLCLHash;

        Ledger::pointer newOL = boost::make_shared<Ledger> (true, boost::ref (*newLCL));
        LedgerMaster::ScopedLockType sl (getApp().getLedgerMaster ().peekMutex (), __FILE__, __LINE__);

        // Apply disputed transactions that didn't get in
        TransactionEngine engine (newOL);
        BOOST_FOREACH (u256_lct_pair & it, mDisputes)
        {
            if (!it.second->getOurVote ())
            {
                // we voted NO
                try
                {
                    WriteLog (lsDEBUG, LedgerConsensus) << "Test applying disputed transaction that did not get in";
                    SerializerIterator sit (it.second->peekTransaction ());
                    SerializedTransaction::pointer txn = boost::make_shared<SerializedTransaction> (boost::ref (sit));

                    if (applyTransaction (engine, txn, newOL, true, false))
                        failedTransactions.push_back (txn);
                }
                catch (...)
                {
                    WriteLog (lsDEBUG, LedgerConsensus) << "Failed to apply transaction we voted NO on";
                }
            }
        }

        WriteLog (lsDEBUG, LedgerConsensus) << "Applying transactions from current open ledger";
        applyTransactions (getApp().getLedgerMaster ().getCurrentLedger ()->peekTransactionMap (), newOL, newLCL,
                           failedTransactions, true);
        getApp().getLedgerMaster ().pushLedger (newLCL, newOL);
        mNewLedgerHash = newLCL->getHash ();
        mState = lcsACCEPTED;
        sl.unlock ();

        if (mValidating)
        {
            // see how close our close time is to other node's close time reports
            WriteLog (lsINFO, LedgerConsensus) << "We closed at " << lexicalCastThrow <std::string> (mCloseTime);
            uint64 closeTotal = mCloseTime;
            int closeCount = 1;

            for (std::map<uint32, int>::iterator it = mCloseTimes.begin (), end = mCloseTimes.end (); it != end; ++it)
            {
                // FIXME: Use median, not average
                WriteLog (lsINFO, LedgerConsensus) << lexicalCastThrow <std::string> (it->second) << " time votes for "
                                                   << lexicalCastThrow <std::string> (it->first);
                closeCount += it->second;
                closeTotal += static_cast<uint64> (it->first) * static_cast<uint64> (it->second);
            }

            closeTotal += (closeCount / 2);
            closeTotal /= closeCount;
            int offset = static_cast<int> (closeTotal) - static_cast<int> (mCloseTime);
            WriteLog (lsINFO, LedgerConsensus) << "Our close offset is estimated at " << offset << " (" << closeCount << ")";
            getApp().getOPs ().closeTimeOffset (offset);
        }
    }
}

void LedgerConsensus::endConsensus ()
{
    getApp().getOPs ().endConsensus (mHaveCorrectLCL);
}

/** Add our fee to our validation
*/
void LedgerConsensus::addLoad(SerializedValidation::ref val)
{
    uint32 fee = std::max(
        getApp().getFeeTrack().getLocalFee(),
        getApp().getFeeTrack().getClusterFee());
    uint32 ref = getApp().getFeeTrack().getLoadBase();
    if (fee > ref)
        val->setFieldU32(sfLoadFee, fee);
}

/** Simulate a consensus round without any network traffic
*/
void LedgerConsensus::simulate ()
{
    WriteLog (lsINFO, LedgerConsensus) << "Simulating consensus";
    closeLedger ();
    mCurrentMSeconds = 100;
    beginAccept (true);
    endConsensus ();
    WriteLog (lsINFO, LedgerConsensus) << "Simulation complete";
}

Json::Value LedgerConsensus::getJson (bool full)
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
            typedef boost::unordered_map<uint160, LedgerProposal::pointer>::value_type pp_t;
            Json::Value ppj (Json::objectValue);
            BOOST_FOREACH (pp_t & pp, mPeerPositions)
            {
                ppj[pp.first.GetHex ()] = pp.second->getJson ();
            }
            ret["peer_positions"] = ppj;
        }

        if (!mAcquired.empty ())
        {
            // acquired
            typedef boost::unordered_map<uint256, SHAMap::pointer>::value_type ac_t;
            Json::Value acq (Json::objectValue);
            BOOST_FOREACH (ac_t & at, mAcquired)
            {
                if (at.second)
                    acq[at.first.GetHex ()] = "acquired";
                else
                    acq[at.first.GetHex ()] = "failed";
            }
            ret["acquired"] = acq;
        }

        if (!mAcquiring.empty ())
        {
            typedef boost::unordered_map<uint256, TransactionAcquire::pointer>::value_type ac_t;
            Json::Value acq (Json::arrayValue);
            BOOST_FOREACH (ac_t & at, mAcquiring)
            {
                acq.append (at.first.GetHex ());
            }
            ret["acquiring"] = acq;
        }

        if (!mDisputes.empty ())
        {
            typedef boost::unordered_map<uint256, DisputedTx::pointer>::value_type d_t;
            Json::Value dsj (Json::objectValue);
            BOOST_FOREACH (d_t & dt, mDisputes)
            {
                dsj[dt.first.GetHex ()] = dt.second->getJson ();
            }
            ret["disputes"] = dsj;
        }

        if (!mCloseTimes.empty ())
        {
            typedef std::map<uint32, int>::value_type ct_t;
            Json::Value ctj (Json::objectValue);
            BOOST_FOREACH (ct_t & ct, mCloseTimes)
            {
                ctj[lexicalCastThrow <std::string> (ct.first)] = ct.second;
            }
            ret["close_times"] = ctj;
        }

        if (!mDeadNodes.empty ())
        {
            Json::Value dnj (Json::arrayValue);
            BOOST_FOREACH (const uint160 & dn, mDeadNodes)
            {
                dnj.append (dn.GetHex ());
            }
            ret["dead_nodes"] = dnj;
        }
    }

    return ret;
}

// vim:ts=4
