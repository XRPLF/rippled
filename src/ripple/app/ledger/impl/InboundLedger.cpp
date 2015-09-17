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
#include <ripple/shamap/SHAMapNodeID.h>
#include <ripple/app/ledger/AccountStateSF.h>
#include <ripple/app/ledger/InboundLedger.h>
#include <ripple/app/ledger/InboundLedgers.h>
#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/ledger/TransactionStateSF.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/basics/Log.h>
#include <ripple/core/JobQueue.h>
#include <ripple/overlay/Overlay.h>
#include <ripple/resource/Fees.h>
#include <ripple/protocol/HashPrefix.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/nodestore/Database.h>

namespace ripple {

enum
{
    // Number of peers to start with
    peerCountStart = 4

    // Number of peers to add on a timeout
    ,peerCountAdd = 2

    // millisecond for each ledger timeout
    ,ledgerAcquireTimeoutMillis = 2500

    // how many timeouts before we giveup
    ,ledgerTimeoutRetriesMax = 10

    // how many timeouts before we get aggressive
    ,ledgerBecomeAggressiveThreshold = 6

    // How many nodes to consider a fetch "small"
    ,fetchSmallNodes = 32
};

InboundLedger::InboundLedger (
    Application& app, uint256 const& hash, std::uint32_t seq, fcReason reason, clock_type& clock)
    : PeerSet (app, hash, ledgerAcquireTimeoutMillis, false, clock,
        deprecatedLogs().journal("InboundLedger"))
    , mHaveHeader (false)
    , mHaveState (false)
    , mHaveTransactions (false)
    , mAborted (false)
    , mSignaled (false)
    , mByHash (true)
    , mSeq (seq)
    , mReason (reason)
    , mReceiveDispatched (false)
{

    if (m_journal.trace) m_journal.trace <<
        "Acquiring ledger " << mHash;
}

void InboundLedger::update (std::uint32_t seq)
{
    ScopedLockType sl (mLock);

    if ((seq != 0) && (mSeq == 0))
    {
        // If we didn't know the sequence number, but now do, save it
        mSeq = seq;
    }

    // Prevent this from being swept
    touch ();
}

bool InboundLedger::checkLocal ()
{
    ScopedLockType sl (mLock);

    if (!isDone () && tryLocal())
    {
        done();
        return true;
    }
    return false;
}

InboundLedger::~InboundLedger ()
{
    // Save any received AS data not processed. It could be useful
    // for populating a different ledger
    for (auto& entry : mReceivedData)
    {
        if (entry.second->type () == protocol::liAS_NODE)
            app_.getInboundLedgers().gotStaleData(entry.second);
    }

}

void InboundLedger::init (ScopedLockType& collectionLock)
{
    ScopedLockType sl (mLock);
    collectionLock.unlock ();

    if (!tryLocal ())
    {
        addPeers ();
        setTimer ();
    }
    else if (!isFailed ())
    {
        if (m_journal.debug) m_journal.debug <<
            "Acquiring ledger we already have locally: " << getHash ();
        mLedger->setClosed ();
        mLedger->setImmutable ();

        if (mReason != fcHISTORY)
            app_.getLedgerMaster ().storeLedger (mLedger);

        // Check if this could be a newer fully-validated ledger
        if (mReason == fcVALIDATION ||
            mReason == fcCURRENT ||
            mReason == fcCONSENSUS)
        {
            app_.getLedgerMaster ().checkAccept (mLedger);
        }
    }
}

/** See how much of the ledger data, if any, is
    in our node store
*/
bool InboundLedger::tryLocal ()
{
    // return value: true = no more work to do

    if (!mHaveHeader)
    {
        // Nothing we can do without the ledger header
        auto node = app_.getNodeStore ().fetch (mHash);

        if (!node)
        {
            Blob data;

            if (!app_.getLedgerMaster ().getFetchPack (mHash, data))
                return false;

            if (m_journal.trace) m_journal.trace <<
                "Ledger header found in fetch pack";
            mLedger = std::make_shared<Ledger> (
                data.data(), data.size(), true, getConfig(), app_.family());
            app_.getNodeStore ().store (
                hotLEDGER, std::move (data), mHash);
        }
        else
        {
            mLedger = std::make_shared<Ledger>(
                node->getData().data(), node->getData().size(),
                true, getConfig(), app_.family());
        }

        if (mLedger->getHash () != mHash)
        {
            // We know for a fact the ledger can never be acquired
            if (m_journal.warning) m_journal.warning <<
                mHash << " cannot be a ledger";
            mFailed = true;
            return true;
        }

        mHaveHeader = true;
    }

    if (!mHaveTransactions)
    {
        if (mLedger->info().txHash.isZero ())
        {
            if (m_journal.trace) m_journal.trace <<
                "No TXNs to fetch";
            mHaveTransactions = true;
        }
        else
        {
            TransactionStateSF filter(app_);

            if (mLedger->txMap().fetchRoot (
                mLedger->info().txHash, &filter))
            {
                auto h (mLedger->getNeededTransactionHashes (1, &filter));

                if (h.empty ())
                {
                    if (m_journal.trace) m_journal.trace <<
                        "Had full txn map locally";
                    mHaveTransactions = true;
                }
            }
        }
    }

    if (!mHaveState)
    {
        if (mLedger->info().accountHash.isZero ())
        {
            if (m_journal.fatal) m_journal.fatal <<
                "We are acquiring a ledger with a zero account hash";
            mFailed = true;
            return true;
        }
        else
        {
            AccountStateSF filter(app_);

            if (mLedger->stateMap().fetchRoot (
                mLedger->info().accountHash, &filter))
            {
                auto h (mLedger->getNeededAccountStateHashes (1, &filter));

                if (h.empty ())
                {
                    if (m_journal.trace) m_journal.trace <<
                        "Had full AS map locally";
                    mHaveState = true;
                }
            }
        }
    }

    if (mHaveTransactions && mHaveState)
    {
        if (m_journal.debug) m_journal.debug <<
            "Had everything locally";
        mComplete = true;
        mLedger->setClosed ();
        mLedger->setImmutable ();
    }

    return mComplete;
}

/** Called with a lock by the PeerSet when the timer expires
*/
void InboundLedger::onTimer (bool wasProgress, ScopedLockType&)
{
    mRecentNodes.clear ();

    if (isDone())
    {
        if (m_journal.info) m_journal.info <<
            "Already done " << mHash;
        return;
    }

    if (getTimeouts () > ledgerTimeoutRetriesMax)
    {
        if (mSeq != 0)
        {
            if (m_journal.warning) m_journal.warning <<
                getTimeouts() << " timeouts for ledger " << mSeq;
        }
        else
        {
            if (m_journal.warning) m_journal.warning <<
                getTimeouts() << " timeouts for ledger " << mHash;
        }
        setFailed ();
        done ();
        return;
    }

    if (!wasProgress)
    {
        checkLocal();

        mAggressive = true;
        mByHash = true;

        std::size_t pc = getPeerCount ();
        WriteLog (lsDEBUG, InboundLedger) <<
            "No progress(" << pc <<
            ") for ledger " << mHash;

        // addPeers triggers if the reason is not fcHISTORY
        // So if the reason IS fcHISTORY, need to trigger after we add
        // otherwise, we need to trigger before we add
        // so each peer gets triggered once
        if (mReason != fcHISTORY)
            trigger (Peer::ptr ());
        addPeers ();
        if (mReason == fcHISTORY)
            trigger (Peer::ptr ());
    }
}

/** Add more peers to the set, if possible */
void InboundLedger::addPeers ()
{
    app_.overlay().selectPeers (*this,
        (getPeerCount() > 0) ? peerCountStart : peerCountAdd,
        ScoreHasLedger (getHash(), mSeq));
}

std::weak_ptr<PeerSet> InboundLedger::pmDowncast ()
{
    return std::dynamic_pointer_cast<PeerSet> (shared_from_this ());
}

/** Dispatch acquire completion
*/
static void LADispatch (
    InboundLedger::pointer la,
    std::vector< std::function<void (InboundLedger::pointer)> > trig)
{
    if (la->isComplete() && !la->isFailed())
    {
        la->app().getLedgerMaster().checkAccept(la->getLedger());
        la->app().getLedgerMaster().tryAdvance();
    }
    else
        la->app().getInboundLedgers().logFailure (la->getHash(), la->getSeq());

    for (unsigned int i = 0; i < trig.size (); ++i)
        trig[i] (la);
}

void InboundLedger::done ()
{
    if (mSignaled)
        return;

    mSignaled = true;
    touch ();

    if (m_journal.trace) m_journal.trace <<
        "Done acquiring ledger " << mHash;

    assert (isComplete () || isFailed ());

    std::vector< std::function<void (InboundLedger::pointer)> > triggers;
    {
        ScopedLockType sl (mLock);
        triggers.swap (mOnComplete);
    }

    if (isComplete () && !isFailed () && mLedger)
    {
        mLedger->setClosed ();
        mLedger->setImmutable ();
        if (mReason != fcHISTORY)
            app_.getLedgerMaster ().storeLedger (mLedger);
        app_.getInboundLedgers().onLedgerFetched(mReason);
    }

    // We hold the PeerSet lock, so must dispatch
    auto that = shared_from_this ();
    app_.getJobQueue ().addJob (
        jtLEDGER_DATA, "triggers",
        [that, triggers] (Job&) { LADispatch(that, triggers); });
}

bool InboundLedger::addOnComplete (
    std::function <void (InboundLedger::pointer)> triggerFunc)
{
    ScopedLockType sl (mLock);

    if (isDone ())
        return false;

    mOnComplete.push_back (triggerFunc);
    return true;
}

/** Request more nodes, perhaps from a specific peer
*/
void InboundLedger::trigger (Peer::ptr const& peer)
{
    ScopedLockType sl (mLock);

    if (isDone ())
    {
        if (m_journal.debug) m_journal.debug <<
            "Trigger on ledger: " << mHash << (mAborted ? " aborted" : "") <<
                (mComplete ? " completed" : "") << (mFailed ? " failed" : "");
        return;
    }

    if (m_journal.trace)
    {
        if (peer)
            m_journal.trace <<
                "Trigger acquiring ledger " << mHash << " from " << peer;
        else
            m_journal.trace <<
                "Trigger acquiring ledger " << mHash;

        if (mComplete || mFailed)
            m_journal.trace <<
                "complete=" << mComplete << " failed=" << mFailed;
        else
            m_journal.trace <<
                "header=" << mHaveHeader << " tx=" << mHaveTransactions <<
                    " as=" << mHaveState;
    }

    if (!mHaveHeader)
    {
        tryLocal ();

        if (mFailed)
        {
            if (m_journal.warning) m_journal.warning <<
                " failed local for " << mHash;
            return;
        }
    }

    protocol::TMGetLedger tmGL;
    tmGL.set_ledgerhash (mHash.begin (), mHash.size ());

    if (getTimeouts () != 0)
    { // Be more aggressive if we've timed out at least once
        tmGL.set_querytype (protocol::qtINDIRECT);

        if (!isProgress () && !mFailed && mByHash && (
            getTimeouts () > ledgerBecomeAggressiveThreshold))
        {
            auto need = getNeededHashes ();

            if (!need.empty ())
            {
                protocol::TMGetObjectByHash tmBH;
                tmBH.set_query (true);
                tmBH.set_ledgerhash (mHash.begin (), mHash.size ());
                bool typeSet = false;
                for (auto& p : need)
                {
                    if (m_journal.warning) m_journal.warning
                        << "Want: " << p.second;

                    if (!typeSet)
                    {
                        tmBH.set_type (p.first);
                        typeSet = true;
                    }

                    if (p.first == tmBH.type ())
                    {
                        protocol::TMIndexedObject* io = tmBH.add_objects ();
                        io->set_hash (p.second.begin (), p.second.size ());
                    }
                }

                Message::pointer packet (std::make_shared <Message> (
                    tmBH, protocol::mtGET_OBJECTS));
                {
                    for (auto it = mPeers.begin (), end = mPeers .end ();
                         it != end; ++it)
                    {
                        Peer::ptr iPeer (
                            app_.overlay ().findPeerByShortID (it->first));

                        if (iPeer)
                        {
                            mByHash = false;
                            iPeer->send (packet);
                        }
                    }
                }
                if (m_journal.info) m_journal.info <<
                    "Attempting by hash fetch for ledger " << mHash;
            }
            else
            {
                if (m_journal.info) m_journal.info <<
                    "getNeededHashes says acquire is complete";
                mHaveHeader = true;
                mHaveTransactions = true;
                mHaveState = true;
                mComplete = true;
            }
        }
    }

    // We can't do much without the header data because we don't know the
    // state or transaction root hashes.
    if (!mHaveHeader && !mFailed)
    {
        tmGL.set_itype (protocol::liBASE);
        if (m_journal.trace) m_journal.trace
             << "Sending header request to "
             << (peer ? "selected peer" : "all peers");
        sendRequest (tmGL, peer);
        return;
    }

    if (mLedger)
        tmGL.set_ledgerseq (mLedger->info().seq);

    // If the peer has high latency, query extra deep
    if (peer && peer->isHighLatency ())
        tmGL.set_querydepth (2);
    else
        tmGL.set_querydepth (1);

    // Get the state data first because it's the most likely to be useful
    // if we wind up abandoning this fetch.
    if (mHaveHeader && !mHaveState && !mFailed)
    {
        assert (mLedger);

        if (!mLedger->stateMap().isValid ())
        {
            mFailed = true;
        }
        else if (mLedger->stateMap().getHash ().isZero ())
        {
            // we need the root node
            tmGL.set_itype (protocol::liAS_NODE);
            *tmGL.add_nodeids () = SHAMapNodeID ().getRawString ();
            if (m_journal.trace) m_journal.trace
                << "Sending AS root request to "
                << (peer ? "selected peer" : "all peers");
            sendRequest (tmGL, peer);
            return;
        }
        else
        {
            std::vector<SHAMapNodeID> nodeIDs;
            std::vector<uint256> nodeHashes;
            // VFALCO Why 256? Make this a constant
            nodeIDs.reserve (256);
            nodeHashes.reserve (256);
            AccountStateSF filter(app_);

            // Release the lock while we process the large state map
            sl.unlock();
            mLedger->stateMap().getMissingNodes (
                nodeIDs, nodeHashes, 256, &filter);
            sl.lock();

            // Make sure nothing happened while we released the lock
            if (!mFailed && !mComplete && !mHaveState)
            {
                if (nodeIDs.empty ())
                {
                    if (!mLedger->stateMap().isValid ())
                        mFailed = true;
                    else
                    {
                        mHaveState = true;

                        if (mHaveTransactions)
                            mComplete = true;
                    }
                }
                else
                {
                    // VFALCO Why 128? Make this a constant
                    if (!mAggressive)
                        filterNodes (nodeIDs, nodeHashes, 128, !isProgress ());

                    if (!nodeIDs.empty ())
                    {
                        tmGL.set_itype (protocol::liAS_NODE);
                        for (auto const& id : nodeIDs)
                        {
                            * (tmGL.add_nodeids ()) = id.getRawString ();
                        }

                        // If we're not querying for a lot of entries,
                        // query extra deep
                        if (nodeIDs.size() <= fetchSmallNodes)
                            tmGL.set_querydepth (tmGL.querydepth() + 1);

                        if (m_journal.trace) m_journal.trace <<
                            "Sending AS node " << nodeIDs.size () <<
                                " request to " << (
                                    peer ? "selected peer" : "all peers");
                        if (nodeIDs.size () == 1 && m_journal.trace)
                            m_journal.trace << "AS node: " << nodeIDs[0];
                        sendRequest (tmGL, peer);
                        return;
                    }
                    else
                        if (m_journal.trace) m_journal.trace <<
                            "All AS nodes filtered";
                }
            }
        }
    }

    if (mHaveHeader && !mHaveTransactions && !mFailed)
    {
        assert (mLedger);

        if (!mLedger->txMap().isValid ())
        {
            mFailed = true;
        }
        else if (mLedger->txMap().getHash ().isZero ())
        {
            // we need the root node
            tmGL.set_itype (protocol::liTX_NODE);
            * (tmGL.add_nodeids ()) = SHAMapNodeID ().getRawString ();
            if (m_journal.trace) m_journal.trace <<
                "Sending TX root request to " << (
                    peer ? "selected peer" : "all peers");
            sendRequest (tmGL, peer);
            return;
        }
        else
        {
            std::vector<SHAMapNodeID> nodeIDs;
            std::vector<uint256> nodeHashes;
            nodeIDs.reserve (256);
            nodeHashes.reserve (256);
            TransactionStateSF filter(app_);
            mLedger->txMap().getMissingNodes (
                nodeIDs, nodeHashes, 256, &filter);

            if (nodeIDs.empty ())
            {
                if (!mLedger->txMap().isValid ())
                    mFailed = true;
                else
                {
                    mHaveTransactions = true;

                    if (mHaveState)
                        mComplete = true;
                }
            }
            else
            {
                if (!mAggressive)
                    filterNodes (nodeIDs, nodeHashes, 128, !isProgress ());

                if (!nodeIDs.empty ())
                {
                    tmGL.set_itype (protocol::liTX_NODE);
                    for (auto const& id : nodeIDs)
                    {
                        * (tmGL.add_nodeids ()) = id.getRawString ();
                    }
                    if (m_journal.trace) m_journal.trace <<
                        "Sending TX node " << nodeIDs.size () <<
                        " request to " << (
                            peer ? "selected peer" : "all peers");
                    sendRequest (tmGL, peer);
                    return;
                }
                else
                    if (m_journal.trace) m_journal.trace <<
                        "All TX nodes filtered";
            }
        }
    }

    if (mComplete || mFailed)
    {
        if (m_journal.debug) m_journal.debug <<
            "Done:" << (mComplete ? " complete" : "") <<
                (mFailed ? " failed " : " ") <<
            mLedger->info().seq;
        sl.unlock ();
        done ();
    }
}

void InboundLedger::filterNodes (std::vector<SHAMapNodeID>& nodeIDs,
    std::vector<uint256>& nodeHashes, int max, bool aggressive)
{
    // ask for new nodes in preference to ones we've already asked for
    assert (nodeIDs.size () == nodeHashes.size ());

    std::vector<bool> duplicates;
    duplicates.reserve (nodeIDs.size ());

    int dupCount = 0;

    for (auto const& nodeHash : nodeHashes)
    {
        if (mRecentNodes.count (nodeHash) != 0)
        {
            duplicates.push_back (true);
            ++dupCount;
        }
        else
            duplicates.push_back (false);
    }

    if (dupCount == nodeIDs.size ())
    {
        // all duplicates
        if (!aggressive)
        {
            nodeIDs.clear ();
            nodeHashes.clear ();
            if (m_journal.trace) m_journal.trace <<
                "filterNodes: all are duplicates";
            return;
        }
    }
    else if (dupCount > 0)
    {
        // some, but not all, duplicates
        int insertPoint = 0;

        for (unsigned int i = 0; i < nodeIDs.size (); ++i)
            if (!duplicates[i])
            {
                // Keep this node
                if (insertPoint != i)
                {
                    nodeIDs[insertPoint] = nodeIDs[i];
                    nodeHashes[insertPoint] = nodeHashes[i];
                }

                ++insertPoint;
            }

        if (m_journal.trace) m_journal.trace <<
            "filterNodes " << nodeIDs.size () << " to " << insertPoint;
        nodeIDs.resize (insertPoint);
        nodeHashes.resize (insertPoint);
    }

    if (nodeIDs.size () > max)
    {
        nodeIDs.resize (max);
        nodeHashes.resize (max);
    }

    for (auto const& nodeHash : nodeHashes)
    {
        mRecentNodes.insert (nodeHash);
    }
}

/** Take ledger header data
    Call with a lock
*/
// data must not have hash prefix
bool InboundLedger::takeHeader (std::string const& data)
{
    // Return value: true=normal, false=bad data
    if (m_journal.trace) m_journal.trace <<
        "got header acquiring ledger " << mHash;

    if (mComplete || mFailed || mHaveHeader)
        return true;

    mLedger = std::make_shared<Ledger>(
        data.data(), data.size(), false, getConfig(), app_.family());

    if (mLedger->getHash () != mHash)
    {
        if (m_journal.warning) m_journal.warning <<
            "Acquire hash mismatch";
        if (m_journal.warning) m_journal.warning <<
            mLedger->getHash () << "!=" << mHash;
        mLedger.reset ();
        return false;
    }

    mHaveHeader = true;

    Serializer s (data.size () + 4);
    s.add32 (HashPrefix::ledgerMaster);
    s.addRaw (data.data(), data.size());
    app_.getNodeStore ().store (
        hotLEDGER, std::move (s.modData ()), mHash);

    progress ();

    if (mLedger->info().txHash.isZero ())
        mHaveTransactions = true;

    if (mLedger->info().accountHash.isZero ())
        mHaveState = true;

    mLedger->setAcquiring ();
    return true;
}

/** Process TX data received from a peer
    Call with a lock
*/
bool InboundLedger::takeTxNode (const std::vector<SHAMapNodeID>& nodeIDs,
    const std::vector< Blob >& data, SHAMapAddNode& san)
{
    if (!mHaveHeader)
    {
        if (m_journal.warning) m_journal.warning <<
            "TX node without header";
        san.incInvalid();
        return false;
    }

    if (mHaveTransactions || mFailed)
    {
        san.incDuplicate();
        return true;
    }

    auto nodeIDit = nodeIDs.cbegin ();
    auto nodeDatait = data.begin ();
    TransactionStateSF tFilter(app_);

    while (nodeIDit != nodeIDs.cend ())
    {
        if (nodeIDit->isRoot ())
        {
            san += mLedger->txMap().addRootNode (
                mLedger->info().txHash, *nodeDatait, snfWIRE, &tFilter);
            if (!san.isGood())
                return false;
        }
        else
        {
            san +=  mLedger->txMap().addKnownNode (
                *nodeIDit, *nodeDatait, &tFilter);
            if (!san.isGood())
                return false;
        }

        ++nodeIDit;
        ++nodeDatait;
    }

    if (!mLedger->txMap().isSynching ())
    {
        mHaveTransactions = true;

        if (mHaveState)
        {
            mComplete = true;
            done ();
        }
    }

    progress ();
    return true;
}

/** Process AS data received from a peer
    Call with a lock
*/
bool InboundLedger::takeAsNode (const std::vector<SHAMapNodeID>& nodeIDs,
    const std::vector< Blob >& data, SHAMapAddNode& san)
{
    if (m_journal.trace) m_journal.trace <<
        "got ASdata (" << nodeIDs.size () << ") acquiring ledger " << mHash;
    if (nodeIDs.size () == 1 && m_journal.trace) m_journal.trace <<
        "got AS node: " << nodeIDs.front ();

    ScopedLockType sl (mLock);

    if (!mHaveHeader)
    {
        if (m_journal.warning) m_journal.warning <<
            "Don't have ledger header";
        san.incInvalid();
        return false;
    }

    if (mHaveState || mFailed)
    {
        san.incDuplicate();
        return true;
    }

    auto nodeIDit = nodeIDs.cbegin ();
    auto nodeDatait = data.begin ();
    AccountStateSF tFilter(app_);

    while (nodeIDit != nodeIDs.cend ())
    {
        if (nodeIDit->isRoot ())
        {
            san += mLedger->stateMap().addRootNode (
                mLedger->info().accountHash, *nodeDatait, snfWIRE, &tFilter);
            if (!san.isGood ())
            {
                if (m_journal.warning) m_journal.warning <<
                    "Bad ledger header";
                return false;
            }
        }
        else
        {
            san += mLedger->stateMap().addKnownNode (
                *nodeIDit, *nodeDatait, &tFilter);
            if (!san.isGood ())
            {
                if (m_journal.warning) m_journal.warning <<
                    "Unable to add AS node";
                return false;
            }
        }

        ++nodeIDit;
        ++nodeDatait;
    }

    if (!mLedger->stateMap().isSynching ())
    {
        mHaveState = true;

        if (mHaveTransactions)
        {
            mComplete = true;
            done ();
        }
    }

    progress ();
    return true;
}

/** Process AS root node received from a peer
    Call with a lock
*/
bool InboundLedger::takeAsRootNode (Blob const& data, SHAMapAddNode& san)
{
    if (mFailed || mHaveState)
    {
        san.incDuplicate();
        return true;
    }

    if (!mHaveHeader)
    {
        assert(false);
        san.incInvalid();
        return false;
    }

    AccountStateSF tFilter(app_);
    san += mLedger->stateMap().addRootNode (
        mLedger->info().accountHash, data, snfWIRE, &tFilter);
    return san.isGood();
}

/** Process AS root node received from a peer
    Call with a lock
*/
bool InboundLedger::takeTxRootNode (Blob const& data, SHAMapAddNode& san)
{
    if (mFailed || mHaveTransactions)
    {
        san.incDuplicate();
        return true;
    }

    if (!mHaveHeader)
    {
        assert(false);
        san.incInvalid();
        return false;
    }

    TransactionStateSF tFilter(app_);
    san += mLedger->txMap().addRootNode (
        mLedger->info().txHash, data, snfWIRE, &tFilter);
    return san.isGood();
}

std::vector<InboundLedger::neededHash_t> InboundLedger::getNeededHashes ()
{
    std::vector<neededHash_t> ret;

    if (!mHaveHeader)
    {
        ret.push_back (std::make_pair (
            protocol::TMGetObjectByHash::otLEDGER, mHash));
        return ret;
    }

    if (!mHaveState)
    {
        AccountStateSF filter(app_);
        // VFALCO NOTE What's the number 4?
        for (auto const& h : mLedger->getNeededAccountStateHashes (4, &filter))
        {
            ret.push_back (std::make_pair (
                protocol::TMGetObjectByHash::otSTATE_NODE, h));
        }
    }

    if (!mHaveTransactions)
    {
        TransactionStateSF filter(app_);
        // VFALCO NOTE What's the number 4?
        for (auto const& h : mLedger->getNeededTransactionHashes (4, &filter))
        {
            ret.push_back (std::make_pair (
                protocol::TMGetObjectByHash::otTRANSACTION_NODE, h));
        }
    }

    return ret;
}

/** Stash a TMLedgerData received from a peer for later processing
    Returns 'true' if we need to dispatch
*/
// VFALCO TODO Why isn't the shared_ptr passed by const& ?
bool InboundLedger::gotData (std::weak_ptr<Peer> peer,
    std::shared_ptr<protocol::TMLedgerData> data)
{
    ScopedLockType sl (mReceivedDataLock);

    mReceivedData.push_back (PeerDataPairType (peer, data));

    if (mReceiveDispatched)
        return false;

    mReceiveDispatched = true;
    return true;
}

/** Process one TMLedgerData
    Returns the number of useful nodes
*/
// VFALCO NOTE, it is not necessary to pass the entire Peer,
//              we can get away with just a Resource::Consumer endpoint.
//
//        TODO Change peer to Consumer
//
int InboundLedger::processData (std::shared_ptr<Peer> peer,
    protocol::TMLedgerData& packet)
{
    ScopedLockType sl (mLock);

    if (packet.type () == protocol::liBASE)
    {
        if (packet.nodes_size () < 1)
        {
            if (m_journal.warning) m_journal.warning <<
                "Got empty header data";
            peer->charge (Resource::feeInvalidRequest);
            return -1;
        }

        SHAMapAddNode san;

        if (!mHaveHeader)
        {
            if (takeHeader (packet.nodes (0).nodedata ()))
                san.incUseful ();
            else
            {
                if (m_journal.warning) m_journal.warning <<
                    "Got invalid header data";
                peer->charge (Resource::feeInvalidRequest);
                return -1;
            }
        }


        if (!mHaveState && (packet.nodes ().size () > 1) &&
            !takeAsRootNode (strCopy (packet.nodes (1).nodedata ()), san))
        {
            if (m_journal.warning) m_journal.warning <<
                "Included AS root invalid";
        }

        if (!mHaveTransactions && (packet.nodes ().size () > 2) &&
            !takeTxRootNode (strCopy (packet.nodes (2).nodedata ()), san))
        {
            if (m_journal.warning) m_journal.warning <<
                "Included TX root invalid";
        }

        if (!san.isInvalid ())
            progress ();
        else
            if (m_journal.debug) m_journal.debug <<
                "Peer sends invalid base data";

        return san.getGood ();
    }

    if ((packet.type () == protocol::liTX_NODE) || (
        packet.type () == protocol::liAS_NODE))
    {
        if (packet.nodes ().size () == 0)
        {
            if (m_journal.info) m_journal.info <<
                "Got response with no nodes";
            peer->charge (Resource::feeInvalidRequest);
            return -1;
        }

        std::vector<SHAMapNodeID> nodeIDs;
        nodeIDs.reserve(packet.nodes().size());
        std::vector< Blob > nodeData;
        nodeData.reserve(packet.nodes().size());

        for (int i = 0; i < packet.nodes ().size (); ++i)
        {
            const protocol::TMLedgerNode& node = packet.nodes (i);

            if (!node.has_nodeid () || !node.has_nodedata ())
            {
                if (m_journal.warning) m_journal.warning <<
                    "Got bad node";
                peer->charge (Resource::feeInvalidRequest);
                return -1;
            }

            nodeIDs.push_back (SHAMapNodeID (node.nodeid ().data (),
                node.nodeid ().size ()));
            nodeData.push_back (Blob (node.nodedata ().begin (),
                node.nodedata ().end ()));
        }

        SHAMapAddNode ret;

        if (packet.type () == protocol::liTX_NODE)
        {
            takeTxNode (nodeIDs, nodeData, ret);
            if (m_journal.debug) m_journal.debug <<
                "Ledger TX node stats: " << ret.get();
        }
        else
        {
            takeAsNode (nodeIDs, nodeData, ret);
            if (m_journal.debug) m_journal.debug <<
                "Ledger AS node stats: " << ret.get();
        }

        if (!ret.isInvalid ())
            progress ();
        else
            if (m_journal.debug) m_journal.debug <<
                "Peer sends invalid node data";

        return ret.getGood ();
    }

    return -1;
}

/** Process pending TMLedgerData
    Query the 'best' peer
*/
void InboundLedger::runData ()
{
    std::shared_ptr<Peer> chosenPeer;
    int chosenPeerCount = -1;

    std::vector <PeerDataPairType> data;
    do
    {
        data.clear();
        {
            ScopedLockType sl (mReceivedDataLock);

            if (mReceivedData.empty ())
            {
                mReceiveDispatched = false;
                break;
            }
            data.swap(mReceivedData);
        }

        // Select the peer that gives us the most nodes that are useful,
        // breaking ties in favor of the peer that responded first.
        for (auto& entry : data)
        {
            Peer::ptr peer = entry.first.lock();
            if (peer)
            {
                int count = processData (peer, *(entry.second));
                if (count > chosenPeerCount)
                {
                    chosenPeer = peer;
                    chosenPeerCount = count;
                }
            }
        }

    } while (1);

    if (chosenPeer)
        trigger (chosenPeer);
}

Json::Value InboundLedger::getJson (int)
{
    Json::Value ret (Json::objectValue);

    ScopedLockType sl (mLock);

    ret[jss::hash] = to_string (mHash);

    if (mComplete)
        ret[jss::complete] = true;

    if (mFailed)
        ret[jss::failed] = true;

    if (!mComplete && !mFailed)
        ret[jss::peers] = static_cast<int>(mPeers.size());

    ret[jss::have_header] = mHaveHeader;

    if (mHaveHeader)
    {
        ret[jss::have_state] = mHaveState;
        ret[jss::have_transactions] = mHaveTransactions;
    }

    if (mAborted)
        ret[jss::aborted] = true;

    ret[jss::timeouts] = getTimeouts ();

    if (mHaveHeader && !mHaveState)
    {
        Json::Value hv (Json::arrayValue);

        // VFALCO Why 16?
        auto v = mLedger->getNeededAccountStateHashes (16, nullptr);
        for (auto const& h : v)
        {
            hv.append (to_string (h));
        }
        ret[jss::needed_state_hashes] = hv;
    }

    if (mHaveHeader && !mHaveTransactions)
    {
        Json::Value hv (Json::arrayValue);
        // VFALCO Why 16?
        auto v = mLedger->getNeededTransactionHashes (16, nullptr);
        for (auto const& h : v)
        {
            hv.append (to_string (h));
        }
        ret[jss::needed_transaction_hashes] = hv;
    }

    return ret;
}

} // ripple
