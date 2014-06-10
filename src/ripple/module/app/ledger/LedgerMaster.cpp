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

#include <ripple/basics/containers/RangeSet.h>
#include <cassert>

namespace ripple {

#define MIN_VALIDATION_RATIO    150     // 150/256ths of validations of previous ledger
#define MAX_LEDGER_GAP          100     // Don't catch up more than 100 ledgers  (cannot exceed 256)

SETUP_LOG (LedgerMaster)

class LedgerCleanerLog;
template <> char const* LogPartition::getPartitionName <LedgerCleanerLog> () { return "LedgerCleaner"; }

class LedgerMasterImp
    : public LedgerMaster
    , public beast::LeakChecked <LedgerMasterImp>
{
public:
    typedef std::function <void (Ledger::ref)> callback;

    typedef RippleRecursiveMutex LockType;
    typedef std::lock_guard <LockType> ScopedLockType;
    typedef beast::GenericScopedUnlock <LockType> ScopedUnlockType;

    beast::Journal m_journal;

    LockType m_mutex;

    LedgerHolder mCurrentLedger;        // The ledger we are currently processiong
    LedgerHolder mClosedLedger;         // The ledger that most recently closed
    LedgerHolder mValidLedger;          // The highest-sequence ledger we have fully accepted
    Ledger::pointer mPubLedger;         // The last ledger we have published
    Ledger::pointer mPathLedger;        // The last ledger we did pathfinding against

    LedgerHistory mLedgerHistory;

    CanonicalTXSet mHeldTransactions;

    LockType mCompleteLock;
    RangeSet mCompleteLedgers;

    std::unique_ptr <LedgerCleaner> mLedgerCleaner;

    int                         mMinValidations;    // The minimum validations to publish a ledger
    uint256                     mLastValidateHash;
    std::uint32_t               mLastValidateSeq;
    std::list<callback>         mOnValidate;        // Called when a ledger has enough validations

    bool                        mAdvanceThread;     // Publish thread is running
    bool                        mAdvanceWork;       // Publish thread has work to do
    int                         mFillInProgress;

    int                         mPathFindThread;    // Pathfinder jobs dispatched
    bool                        mPathFindNewLedger;
    bool                        mPathFindNewRequest;

    std::atomic <std::uint32_t> mPubLedgerClose;
    std::atomic <std::uint32_t> mPubLedgerSeq;
    std::atomic <std::uint32_t> mValidLedgerClose;
    std::atomic <std::uint32_t> mValidLedgerSeq;
    std::atomic <std::uint32_t> mBuildingLedgerSeq;

    //--------------------------------------------------------------------------

    explicit LedgerMasterImp (Stoppable& parent, beast::Journal journal)
        : LedgerMaster (parent)
        , m_journal (journal)
        , mHeldTransactions (uint256 ())
        , mLedgerCleaner (LedgerCleaner::New(*this, LogPartition::getJournal<LedgerCleanerLog>()))
        , mMinValidations (0)
        , mLastValidateSeq (0)
        , mAdvanceThread (false)
        , mAdvanceWork (false)
        , mFillInProgress (0)
        , mPathFindThread (0)
        , mPathFindNewRequest (false)
        , mPubLedgerClose (0)
        , mPubLedgerSeq (0)
        , mValidLedgerClose (0)
        , mValidLedgerSeq (0)
        , mBuildingLedgerSeq (0)
    {
    }

    ~LedgerMasterImp ()
    {
    }

    LedgerIndex getCurrentLedgerIndex ()
    {
        return mCurrentLedger.get ()->getLedgerSeq ();
    }

    LedgerIndex getValidLedgerIndex ()
    {
        return mValidLedgerSeq;
    }

    int getPublishedLedgerAge ()
    {
        std::uint32_t pubClose = mPubLedgerClose.load();
        if (!pubClose)
        {
            WriteLog (lsDEBUG, LedgerMaster) << "No published ledger";
            return 999999;
        }

        std::int64_t ret = getApp().getOPs ().getCloseTimeNC ();
        ret -= static_cast<std::int64_t> (pubClose);
        ret = (ret > 0) ? ret : 0;

        WriteLog (lsTRACE, LedgerMaster) << "Published ledger age is " << ret;
        return static_cast<int> (ret);
    }

    int getValidatedLedgerAge ()
    {
        std::uint32_t valClose = mValidLedgerClose.load();
        if (!valClose)
        {
            WriteLog (lsDEBUG, LedgerMaster) << "No validated ledger";
            return 999999;
        }

        std::int64_t ret = getApp().getOPs ().getCloseTimeNC ();
        ret -= static_cast<std::int64_t> (valClose);
        ret = (ret > 0) ? ret : 0;

        WriteLog (lsTRACE, LedgerMaster) << "Validated ledger age is " << ret;
        return static_cast<int> (ret);
    }

    bool isCaughtUp(std::string& reason)
    {
        if (getPublishedLedgerAge() > 180)
        {
            reason = "No recently-published ledger";
            return false;
        }
        std::uint32_t validClose = mValidLedgerClose.load();
        std::uint32_t pubClose = mPubLedgerClose.load();
        if (!validClose || !pubClose)
        {
            reason = "No published ledger";
            return false;
        }
        if (validClose  > (pubClose + 90))
        {
            reason = "Published ledger lags validated ledger";
            return false;
        }
        return true;
    }

    void setValidLedger(Ledger::ref l)
    {
        mValidLedger.set (l);
        mValidLedgerClose = l->getCloseTimeNC();
        mValidLedgerSeq = l->getLedgerSeq();
        getApp().getOPs().updateLocalTx (l);
    }

    void setPubLedger(Ledger::ref l)
    {
        mPubLedger = l;
        mPubLedgerClose = l->getCloseTimeNC();
        mPubLedgerSeq = l->getLedgerSeq();
    }

    void addHeldTransaction (Transaction::ref transaction)
    {
        // returns true if transaction was added
        ScopedLockType ml (m_mutex);
        mHeldTransactions.push_back (transaction->getSTransaction ());
    }

    void pushLedger (Ledger::pointer newLedger)
    {
        // Caller should already have properly assembled this ledger into "ready-to-close" form --
        // all candidate transactions must already be applied
        WriteLog (lsINFO, LedgerMaster) << "PushLedger: " << newLedger->getHash ();

        {
            ScopedLockType ml (m_mutex);

            Ledger::pointer closedLedger = mCurrentLedger.getMutable ();
            if (closedLedger)
            {
                closedLedger->setClosed ();
                closedLedger->setImmutable ();
                mClosedLedger.set (closedLedger);
	    }

            mCurrentLedger.set (newLedger);
        }

        if (getConfig().RUN_STANDALONE)
        {
            setFullLedger(newLedger, true, false);
            tryAdvance();
        }
        else
            checkAccept(newLedger);
    }

    void pushLedger (Ledger::pointer newLCL, Ledger::pointer newOL)
    {
        assert (newLCL->isClosed () && newLCL->isAccepted ());
        assert (!newOL->isClosed () && !newOL->isAccepted ());


        {
            ScopedLockType ml (m_mutex);
            mClosedLedger.set (newLCL);
            mCurrentLedger.set (newOL);
        }

        if (getConfig().RUN_STANDALONE)
        {
            setFullLedger(newLCL, true, false);
            tryAdvance();
        }
        else
        {
            mLedgerHistory.builtLedger (newLCL);
        }
    }

    void switchLedgers (Ledger::pointer lastClosed, Ledger::pointer current)
    {
        assert (lastClosed && current);

        {
            ScopedLockType ml (m_mutex);

            lastClosed->setClosed ();
            lastClosed->setAccepted ();

            mCurrentLedger.set (current);
            mClosedLedger.set (lastClosed);

            assert (!current->isClosed ());
        }
        checkAccept (lastClosed);
    }

    bool fixIndex (LedgerIndex ledgerIndex, LedgerHash const& ledgerHash)
    {
        return mLedgerHistory.fixIndex (ledgerIndex, ledgerHash);
    }

    bool storeLedger (Ledger::pointer ledger)
    {
        // Returns true if we already had the ledger
        return mLedgerHistory.addLedger (ledger, false);
    }

    void forceValid (Ledger::pointer ledger)
    {
        ledger->setValidated();
        setFullLedger(ledger, true, false);
    }

    /** Apply held transactions to the open ledger
        This is normally called as we close the ledger.
        The open ledger remains open to handle new transactions
        until a new open ledger is built.
    */
    void applyHeldTransactions ()
    {
        ScopedLockType sl (m_mutex);

        // Start with a mutable snapshot of the open ledger
        TransactionEngine engine (mCurrentLedger.getMutable ());

        int recovers = 0;

        for (auto const& it : mHeldTransactions)
        {
            try
            {
                TransactionEngineParams tepFlags = tapOPEN_LEDGER;

                if (getApp().getHashRouter ().addSuppressionFlags (it.first.getTXID (), SF_SIGGOOD))
                    tepFlags = static_cast<TransactionEngineParams> (tepFlags | tapNO_CHECK_SIGN);

                bool didApply;
                engine.applyTransaction (*it.second, tepFlags, didApply);

                if (didApply)
                    ++recovers;

                // If a transaction is recovered but hasn't been relayed,
                // it will become disputed in the consensus process, which
                // will cause it to be relayed.

            }
            catch (...)
            {
                WriteLog (lsWARNING, LedgerMaster) << "Held transaction throws";
            }
        }

        CondLog (recovers != 0, lsINFO, LedgerMaster) << "Recovered " << recovers << " held transactions";

        // VFALCO TODO recreate the CanonicalTxSet object instead of resetting it
        mHeldTransactions.reset (engine.getLedger()->getHash ());
        mCurrentLedger.set (engine.getLedger ());
    }

    LedgerIndex getBuildingLedger ()
    {
        // The ledger we are currently building, 0 of none
        return mBuildingLedgerSeq.load ();
    }

    void setBuildingLedger (LedgerIndex i)
    {
        mBuildingLedgerSeq.store (i);
    }

    TER doTransaction (SerializedTransaction::ref txn, TransactionEngineParams params, bool& didApply)
    {
        Ledger::pointer ledger;
        TransactionEngine engine;
        TER result;
        didApply = false;

        {
            ScopedLockType sl (m_mutex);
            ledger = mCurrentLedger.getMutable ();
            engine.setLedger (ledger);
            result = engine.applyTransaction (*txn, params, didApply);
        }
        if (didApply)
        {
           mCurrentLedger.set (ledger);
           getApp().getOPs ().pubProposedTransaction (ledger, txn, result);
        }
        return result;
    }

    bool haveLedgerRange (std::uint32_t from, std::uint32_t to)
    {
        ScopedLockType sl (mCompleteLock);
        std::uint32_t prevMissing = mCompleteLedgers.prevMissing (to + 1);
        return (prevMissing == RangeSet::absent) || (prevMissing < from);
    }

    bool haveLedger (std::uint32_t seq)
    {
        ScopedLockType sl (mCompleteLock);
        return mCompleteLedgers.hasValue (seq);
    }

    void clearLedger (std::uint32_t seq)
    {
        ScopedLockType sl (mCompleteLock);
        return mCompleteLedgers.clearValue (seq);
    }

    // returns Ledgers we have all the nodes for
    bool getFullValidatedRange (std::uint32_t& minVal, std::uint32_t& maxVal)
    {
        maxVal = mPubLedgerSeq.load();

        if (!maxVal)
            return false;

        {
            ScopedLockType sl (mCompleteLock);
            minVal = mCompleteLedgers.prevMissing (maxVal);
        }

        if (minVal == RangeSet::absent)
            minVal = maxVal;
        else
            ++minVal;

        return true;
    }

    // Returns Ledgers we have all the nodes for and are indexed
    bool getValidatedRange (std::uint32_t& minVal, std::uint32_t& maxVal)
    {
        maxVal = mPubLedgerSeq.load();

        if (!maxVal)
            return false;

        {
            ScopedLockType sl (mCompleteLock);
            minVal = mCompleteLedgers.prevMissing (maxVal);
        }

        if (minVal == RangeSet::absent)
            minVal = maxVal;
        else
            ++minVal;

        // Remove from the validated range any ledger sequences that may not be
        // fully updated in the database yet

        std::set<std::uint32_t> sPendingSaves = Ledger::getPendingSaves();

        if (!sPendingSaves.empty() && ((minVal != 0) || (maxVal != 0)))
        {
            // Ensure we shrink the tips as much as possible
            // If we have 7-9 and 8,9 are invalid, we don't want to see the 8 and shrink to just 9
            // because then we'll have nothing when we could have 7.
            while (sPendingSaves.count(maxVal) > 0)
                --maxVal;
            while (sPendingSaves.count(minVal) > 0)
                ++minVal;

            // Best effort for remaining exclusions
            for(auto v : sPendingSaves)
            {
                if ((v >= minVal) && (v <= maxVal))
                {
                    if (v > ((minVal + maxVal) / 2))
                        maxVal = v - 1;
                    else
                        minVal = v + 1;
                }
            }

            if (minVal > maxVal)
                minVal = maxVal = 0;
        }

        return true;
    }

    // Get the earliest ledger we will let peers fetch
    std::uint32_t getEarliestFetch ()
    {
        // The earliest ledger we will let people fetch is ledger zero,
        // unless that creates a larger range than allowed
        std::uint32_t e = getClosedLedger()->getLedgerSeq();
        if (e > getConfig().FETCH_DEPTH)
            e -= getConfig().FETCH_DEPTH;
        else
            e = 0;
        return e;
    }

    void tryFill (Job& job, Ledger::pointer ledger)
    {
        std::uint32_t seq = ledger->getLedgerSeq ();
        uint256 prevHash = ledger->getParentHash ();

        std::map< std::uint32_t, std::pair<uint256, uint256> > ledgerHashes;

        std::uint32_t minHas = ledger->getLedgerSeq ();
        std::uint32_t maxHas = ledger->getLedgerSeq ();

        while (! job.shouldCancel() && seq > 0)
        {
            {
                ScopedLockType ml (m_mutex);
                minHas = seq;
                --seq;

                if (haveLedger (seq))
                    break;
            }

            auto it (ledgerHashes.find (seq));

            if (it == ledgerHashes.end ())
            {
                if (getApp().isShutdown ())
                    return;

                {
                    ScopedLockType ml (mCompleteLock);
                    mCompleteLedgers.setRange (minHas, maxHas);
                }
                maxHas = minHas;
                ledgerHashes = Ledger::getHashesByIndex ((seq < 500)
                    ? 0
                    : (seq - 499), seq);
                it = ledgerHashes.find (seq);

                if (it == ledgerHashes.end ())
                    break;
            }

            if (it->second.first != prevHash)
                break;

            prevHash = it->second.second;
        }

        {
            ScopedLockType ml (mCompleteLock);
            mCompleteLedgers.setRange (minHas, maxHas);
        }
        {
            ScopedLockType ml (m_mutex);
            mFillInProgress = 0;
            tryAdvance();
        }
    }

    /** Request a fetch pack to get the ledger prior to 'nextLedger'
    */
    void getFetchPack (Ledger::ref nextLedger)
    {
        Peer::ptr target;
        int count = 0;

        Overlay::PeerSequence peerList = getApp().overlay ().getActivePeers ();
        for (auto const& peer : peerList)
        {
            if (peer->hasRange (nextLedger->getLedgerSeq() - 1, nextLedger->getLedgerSeq()))
            {
                if (count++ == 0)
                    target = peer;
                else if ((rand () % ++count) == 0)
                    target = peer;
            }
        }

        if (target)
        {
            protocol::TMGetObjectByHash tmBH;
            tmBH.set_query (true);
            tmBH.set_type (protocol::TMGetObjectByHash::otFETCH_PACK);
            tmBH.set_ledgerhash (nextLedger->getHash().begin (), 32);
            Message::pointer packet = std::make_shared<Message> (tmBH, protocol::mtGET_OBJECTS);

            target->send (packet);
            WriteLog (lsTRACE, LedgerMaster) << "Requested fetch pack for " << nextLedger->getLedgerSeq() - 1;
        }
        else
            WriteLog (lsDEBUG, LedgerMaster) << "No peer for fetch pack";
    }

    void fixMismatch (Ledger::ref ledger)
    {
        int invalidate = 0;
        uint256 hash;

        for (std::uint32_t lSeq = ledger->getLedgerSeq () - 1; lSeq > 0; --lSeq)
            if (haveLedger (lSeq))
            {
                try
                {
                    hash = ledger->getLedgerHash (lSeq);
                }
                catch (...)
                {
                    WriteLog (lsWARNING, LedgerMaster) <<
                        "fixMismatch encounters partial ledger";
                    clearLedger(lSeq);
                    return;
                }

                if (hash.isNonZero ())
                {
                    // try to close the seam
                    Ledger::pointer otherLedger = getLedgerBySeq (lSeq);

                    if (otherLedger && (otherLedger->getHash () == hash))
                    {
                        // we closed the seam
                        CondLog (invalidate != 0, lsWARNING, LedgerMaster) <<
                            "Match at " << lSeq << ", " << invalidate <<
                            " prior ledgers invalidated";
                        return;
                    }
                }

                clearLedger (lSeq);
                ++invalidate;
            }

        // all prior ledgers invalidated
        CondLog (invalidate != 0, lsWARNING, LedgerMaster) << "All " <<
            invalidate << " prior ledgers invalidated";
    }

    void setFullLedger (Ledger::pointer ledger, bool isSynchronous, bool isCurrent)
    {
        // A new ledger has been accepted as part of the trusted chain
        WriteLog (lsDEBUG, LedgerMaster) << "Ledger " << ledger->getLedgerSeq () << " accepted :" << ledger->getHash ();
        assert (ledger->peekAccountStateMap ()->getHash ().isNonZero ());

        ledger->setValidated();
        mLedgerHistory.addLedger(ledger, true);
        ledger->setFull();
        ledger->pendSaveValidated (isSynchronous, isCurrent);

        {

            {
                ScopedLockType ml (mCompleteLock);
                mCompleteLedgers.setValue (ledger->getLedgerSeq ());
            }

            ScopedLockType ml (m_mutex);

            if (ledger->getLedgerSeq() > mValidLedgerSeq)
                setValidLedger(ledger);
            if (!mPubLedger)
            {
                setPubLedger(ledger);
                getApp().getOrderBookDB().setup(ledger);
            }

            if ((ledger->getLedgerSeq () != 0) && haveLedger (ledger->getLedgerSeq () - 1))
            {
                // we think we have the previous ledger, double check
                Ledger::pointer prevLedger = getLedgerBySeq (ledger->getLedgerSeq () - 1);

                if (!prevLedger || (prevLedger->getHash () != ledger->getParentHash ()))
                {
                    WriteLog (lsWARNING, LedgerMaster) << "Acquired ledger invalidates previous ledger: " <<
                                                       (prevLedger ? "hashMismatch" : "missingLedger");
                    fixMismatch (ledger);
                }
            }
        }

        //--------------------------------------------------------------------------
        //
        {
            if (isCurrent)
                getApp ().getValidators ().ledgerClosed (ledger->getHash());
        }
        //
        //--------------------------------------------------------------------------
    }

    void failedSave(std::uint32_t seq, uint256 const& hash)
    {
        clearLedger(seq);
        getApp().getInboundLedgers().findCreate(hash, seq, InboundLedger::fcGENERIC);
    }

    // Check if the specified ledger can become the new last fully-validated ledger
    void checkAccept (uint256 const& hash, std::uint32_t seq)
    {

        if (seq != 0)
        {
            // Ledger is too old
            if (seq <= mValidLedgerSeq)
                return;

            // Ledger could match the ledger we're already building
            if (seq == mBuildingLedgerSeq)
                return;
        }

        Ledger::pointer ledger = mLedgerHistory.getLedgerByHash (hash);

        if (!ledger)
        {
            // FIXME: We should really only fetch if the ledger
            //has sufficient validations to accept it

            InboundLedger::pointer l =
                getApp().getInboundLedgers().findCreate(hash, 0, InboundLedger::fcGENERIC);
            if (l && l->isComplete() && !l->isFailed())
                ledger = l->getLedger();
            else
            {
                WriteLog (lsDEBUG, LedgerMaster) <<
                    "checkAccept triggers acquire " << to_string (hash);
            }
        }

        if (ledger)
            checkAccept (ledger);
    }

    /**
     * Determines how many validations are needed to fully-validated a ledger
     *
     * @return Number of validations needed
     */
    int getNeededValidations ()
    {
        if (getConfig ().RUN_STANDALONE)
            return 0;

        int minVal = mMinValidations;

        if (mLastValidateHash.isNonZero ())
        {
            int val = getApp().getValidations ().getTrustedValidationCount (mLastValidateHash);
            val *= MIN_VALIDATION_RATIO;
            val /= 256;

            if (val > minVal)
                minVal = val;
        }

        return minVal;
    }

    void checkAccept (Ledger::ref ledger)
    {
        if (ledger->getLedgerSeq() <= mValidLedgerSeq)
            return;

        // Can we advance the last fully-validated ledger? If so, can we publish?
        ScopedLockType ml (m_mutex);

        if (ledger->getLedgerSeq() <= mValidLedgerSeq)
            return;

        int minVal = getNeededValidations();
        int tvc = getApp().getValidations().getTrustedValidationCount(ledger->getHash());
        if (tvc < minVal) // nothing we can do
        {
            WriteLog (lsTRACE, LedgerMaster) << "Only " << tvc << " validations for " << ledger->getHash();
            return;
        }

        WriteLog (lsINFO, LedgerMaster) << "Advancing accepted ledger to " << ledger->getLedgerSeq() << " with >= " << minVal << " validations";

        mLastValidateHash = ledger->getHash();
        mLastValidateSeq = ledger->getLedgerSeq();

        ledger->setValidated();
        ledger->setFull();
        setValidLedger(ledger);
        if (!mPubLedger)
        {
            ledger->pendSaveValidated(true, true);
            setPubLedger(ledger);
            getApp().getOrderBookDB().setup(ledger);
        }

        std::uint64_t fee, fee2, ref;
        ref = getApp().getFeeTrack().getLoadBase();
        int count = getApp().getValidations().getFeeAverage(ledger->getHash(), ref, fee);
        int count2 = getApp().getValidations().getFeeAverage(ledger->getParentHash(), ref, fee2);

        if ((count + count2) == 0)
            getApp().getFeeTrack().setRemoteFee(ref);
        else
            getApp().getFeeTrack().setRemoteFee(((fee * count) + (fee2 * count2)) / (count + count2));

        tryAdvance ();
    }

    /** Report that the consensus process built a particular ledger */
    void consensusBuilt (Ledger::ref ledger) override
    {

        // Because we just built a ledger, we are no longer building one
        setBuildingLedger (0);

        // No need to process validations in standalone mode
        if (getConfig().RUN_STANDALONE)
            return;

        if (ledger->getLedgerSeq() <= mValidLedgerSeq)
        {
            WriteLog (lsINFO,  LedgerConsensus)
               << "Consensus built old ledger: "
               << ledger->getLedgerSeq() << " <= " << mValidLedgerSeq;
            return;
        }

        // See if this ledger can be the new fully-validated ledger
        checkAccept (ledger);

        if (ledger->getLedgerSeq() <= mValidLedgerSeq)
        {
            WriteLog (lsDEBUG, LedgerConsensus)
                << "Consensus ledger fully validated";
            return;
        }

        // This ledger cannot be the new fully-validated ledger, but
        // maybe we saved up validations for some other ledger that can be

        auto const val = getApp().getValidations().getCurrentTrustedValidations();

        // Track validation counts with sequence numbers
        class valSeq
        {
            public:

            valSeq () : valCount_ (0), ledgerSeq_ (0) { ; }

            void mergeValidation (LedgerSeq seq)
            {
                valCount_++;

                // If we didn't already know the sequence, now we do
                if (ledgerSeq_ == 0)
                    ledgerSeq_ = seq;
            }

            int valCount_;
            LedgerSeq ledgerSeq_;
        };

        // Count the number of current, trusted validations
        ripple::unordered_map <uint256, valSeq> count;
        for (auto const& v : val)
        {
            valSeq& vs = count[v->getLedgerHash()];
            vs.mergeValidation (v->getFieldU32 (sfLedgerSequence));
        }

        int neededValidations = getNeededValidations ();
        LedgerSeq maxSeq = mValidLedgerSeq;
        uint256 maxLedger = ledger->getHash();

        // Of the ledgers with sufficient validations,
        // find the one with the highest sequence
        for (auto& v : count)
            if (v.second.valCount_ > neededValidations)
            {
                // If we still don't know the sequence, get it
                if (v.second.ledgerSeq_ == 0)
                {
                    Ledger::pointer ledger = getLedgerByHash (v.first);
                    if (ledger)
                        v.second.ledgerSeq_ = ledger->getLedgerSeq();
                }

                if (v.second.ledgerSeq_ > maxSeq)
                {
                    maxSeq = v.second.ledgerSeq_;
                    maxLedger = v.first;
                }
            }

        if (maxSeq > mValidLedgerSeq)
        {
            WriteLog (lsDEBUG, LedgerConsensus)
                << "Consensus triggered check of ledger";
            checkAccept (maxLedger, maxSeq);
        }
    }

    void advanceThread()
    {
        ScopedLockType sl (m_mutex);
        assert (!mValidLedger.empty () && mAdvanceThread);

        WriteLog (lsTRACE, LedgerMaster) << "advanceThread<";

        try
        {
            doAdvance();
        }
        catch (...)
        {
            WriteLog (lsFATAL, LedgerMaster) << "doAdvance throws an exception";
        }

        mAdvanceThread = false;
        WriteLog (lsTRACE, LedgerMaster) << "advanceThread>";
    }

    // Try to publish ledgers, acquire missing ledgers
    void doAdvance ()
    {
        do
        {
            mAdvanceWork = false; // If there's work to do, we'll make progress
            bool progress = false;

            std::list<Ledger::pointer> pubLedgers = findNewLedgersToPublish ();
            if (pubLedgers.empty())
            {
                if (!getConfig().RUN_STANDALONE && !getApp().getFeeTrack().isLoadedLocal() &&
                    (getApp().getJobQueue().getJobCount(jtPUBOLDLEDGER) < 10) &&
                    (mValidLedgerSeq == mPubLedgerSeq))
                { // We are in sync, so can acquire
                    std::uint32_t missing;
                    {
                        ScopedLockType sl (mCompleteLock);
                        missing = mCompleteLedgers.prevMissing(mPubLedger->getLedgerSeq());
                    }
                    WriteLog (lsTRACE, LedgerMaster) << "tryAdvance discovered missing " << missing;
                    if ((missing != RangeSet::absent) && (missing > 0) &&
                        shouldAcquire(mValidLedgerSeq, getConfig().LEDGER_HISTORY, missing) &&
                        ((mFillInProgress == 0) || (missing > mFillInProgress)))
                    {
                        WriteLog (lsTRACE, LedgerMaster) << "advanceThread should acquire";
                        {
                            ScopedUnlockType sl(m_mutex);
                            Ledger::pointer nextLedger = mLedgerHistory.getLedgerBySeq(missing + 1);
                            if (nextLedger)
                            {
                                assert (nextLedger->getLedgerSeq() == (missing + 1));
                                Ledger::pointer ledger = getLedgerByHash(nextLedger->getParentHash());
                                if (!ledger)
                                {
                                    if (!getApp().getInboundLedgers().isFailure(nextLedger->getParentHash()))
                                    {
                                        InboundLedger::pointer acq =
                                            getApp().getInboundLedgers().findCreate(nextLedger->getParentHash(),
                                                                                    nextLedger->getLedgerSeq() - 1,
                                                                                    InboundLedger::fcHISTORY);
                                        if (acq->isComplete() && !acq->isFailed())
                                            ledger = acq->getLedger();
                                        else if ((missing > 40000) && getApp().getOPs().shouldFetchPack(missing))
                                        {
                                            WriteLog (lsTRACE, LedgerMaster) << "tryAdvance want fetch pack " << missing;
                                            getFetchPack(nextLedger);
                                        }
                                        else
                                            WriteLog (lsTRACE, LedgerMaster) << "tryAdvance no fetch pack for " << missing;
                                    }
                                    else
                                        WriteLog (lsDEBUG, LedgerMaster) << "tryAdvance found failed acquire";
                                }
                                if (ledger)
                                {
                                    assert(ledger->getLedgerSeq() == missing);
                                    WriteLog (lsTRACE, LedgerMaster) << "tryAdvance acquired " << ledger->getLedgerSeq();
                                    setFullLedger(ledger, false, false);
                                    if ((mFillInProgress == 0) && (Ledger::getHashByIndex(ledger->getLedgerSeq() - 1) == ledger->getParentHash()))
                                    { // Previous ledger is in DB
                                        ScopedLockType sl(m_mutex);
                                        mFillInProgress = ledger->getLedgerSeq();
                                        getApp().getJobQueue().addJob(jtADVANCE, "tryFill", std::bind (
                                            &LedgerMasterImp::tryFill, this,
                                            std::placeholders::_1, ledger));
                                    }
                                    progress = true;
                                }
                                else
                                {
                                    try
                                    {
                                        for (int i = 0; i < getConfig().getSize(siLedgerFetch); ++i)
                                        {
                                            std::uint32_t seq = missing - i;
                                            uint256 hash = nextLedger->getLedgerHash(seq);
                                            if (hash.isNonZero())
                                                getApp().getInboundLedgers().findCreate(hash,
                                                     seq, InboundLedger::fcHISTORY);
                                        }
                                    }
                                    catch (...)
                                    {
                                        WriteLog (lsWARNING, LedgerMaster) << "Threw while prefecthing";
                                    }
                                }
                            }
                            else
                            {
                                WriteLog (lsFATAL, LedgerMaster) << "Unable to find ledger following prevMissing " << missing;
                                WriteLog (lsFATAL, LedgerMaster) << "Pub:" << mPubLedgerSeq << " Val:" << mValidLedgerSeq;
                                WriteLog (lsFATAL, LedgerMaster) << "Ledgers: " << getApp().getLedgerMaster().getCompleteLedgers();
                                clearLedger (missing + 1);
                                progress = true;
                            }
                        }
                        if (mValidLedgerSeq != mPubLedgerSeq)
                        {
                            WriteLog (lsDEBUG, LedgerMaster) << "tryAdvance found last valid changed";
                            progress = true;
                        }
                    }
                }
                else
                    WriteLog (lsTRACE, LedgerMaster) << "tryAdvance not fetching history";
            }
            else
            {
                WriteLog (lsTRACE, LedgerMaster) <<
                    "tryAdvance found " << pubLedgers.size() <<
                    " ledgers to publish";
                for(auto ledger : pubLedgers)
                {
                    {
                        ScopedUnlockType sul (m_mutex);
                        WriteLog(lsDEBUG, LedgerMaster) <<
                            "tryAdvance publishing seq " << ledger->getLedgerSeq();

                        setFullLedger(ledger, true, true);
                        getApp().getOPs().pubLedger(ledger);
                    }

                    setPubLedger(ledger);
                    progress = true;
                }

                getApp().getOPs().clearNeedNetworkLedger();
                newPFWork ("pf:newLedger");
            }
            if (progress)
                mAdvanceWork = true;
        } while (mAdvanceWork);
    }

    std::list<Ledger::pointer> findNewLedgersToPublish ()
    {
        std::list<Ledger::pointer> ret;

        WriteLog (lsTRACE, LedgerMaster) << "findNewLedgersToPublish<";
        if (!mPubLedger)
        {
            WriteLog (lsINFO, LedgerMaster) << "First published ledger will be " << mValidLedgerSeq;
            ret.push_back (mValidLedger.get ());
        }
        else if (mValidLedgerSeq > (mPubLedgerSeq + MAX_LEDGER_GAP))
        {
            WriteLog (lsWARNING, LedgerMaster) << "Gap in validated ledger stream " << mPubLedgerSeq << " - " <<
                                               mValidLedgerSeq - 1;
            Ledger::pointer valLedger = mValidLedger.get ();
            ret.push_back (valLedger);
            setPubLedger (valLedger);
            getApp().getOrderBookDB().setup(valLedger);
        }
        else if (mValidLedgerSeq > mPubLedgerSeq)
        {
            int acqCount = 0;

            std::uint32_t pubSeq = mPubLedgerSeq + 1; // Next sequence to publish
            Ledger::pointer valLedger = mValidLedger.get ();
            std::uint32_t valSeq = valLedger->getLedgerSeq ();

            ScopedUnlockType sul(m_mutex);
            try
            {
                for (std::uint32_t seq = pubSeq; seq <= valSeq; ++seq)
                {
                    WriteLog (lsTRACE, LedgerMaster) << "Trying to fetch/publish valid ledger " << seq;

                    Ledger::pointer ledger;
                    uint256 hash = valLedger->getLedgerHash (seq); // This can throw

                    if (seq == valSeq)
                    { // We need to publish the ledger we just fully validated
                        ledger = valLedger;
                    }
                    else
                    {
                        if (hash.isZero ())
                        {
                            WriteLog (lsFATAL, LedgerMaster) << "Ledger: " << valSeq << " does not have hash for " << seq;
                            assert (false);
                        }

                        ledger = mLedgerHistory.getLedgerByHash (hash);
                    }

                    if (!ledger && (++acqCount < 4))
                    { // We can try to acquire the ledger we need
                        InboundLedger::pointer acq =
                            getApp().getInboundLedgers ().findCreate (hash, seq, InboundLedger::fcGENERIC);

                        if (!acq->isDone ())
                        {
                        }
                        else if (acq->isComplete () && !acq->isFailed ())
                        {
                            ledger = acq->getLedger();
                        }
                        else
                        {
                            WriteLog (lsWARNING, LedgerMaster) << "Failed to acquire a published ledger";
                            getApp().getInboundLedgers().dropLedger(hash);
                            acq = getApp().getInboundLedgers().findCreate(hash, seq, InboundLedger::fcGENERIC);
                            if (acq->isComplete())
                            {
                                if (acq->isFailed())
                                    getApp().getInboundLedgers().dropLedger(hash);
                                else
                                    ledger = acq->getLedger();
                            }
                        }
                    }

                    if (ledger && (ledger->getLedgerSeq() == pubSeq))
                    { // We acquired the next ledger we need to publish
                        ledger->setValidated();
                        ret.push_back (ledger);
                        ++pubSeq;
                    }

                }
            }
            catch (...)
            {
                WriteLog (lsERROR, LedgerMaster) << "findNewLedgersToPublish catches an exception";
            }
        }

        WriteLog (lsTRACE, LedgerMaster) << "findNewLedgersToPublish> " << ret.size();
        return ret;
    }

    void tryAdvance()
    {
        ScopedLockType ml (m_mutex);

        // Can't advance without at least one fully-valid ledger
        mAdvanceWork = true;
        if (!mAdvanceThread && !mValidLedger.empty ())
        {
            mAdvanceThread = true;
            getApp().getJobQueue ().addJob (jtADVANCE, "advanceLedger",
                                            std::bind (&LedgerMasterImp::advanceThread, this));
        }
    }

    // Return the hash of the valid ledger with a particular sequence, given a subsequent ledger known valid
    uint256 getLedgerHash(std::uint32_t desiredSeq, Ledger::ref knownGoodLedger)
    {
        assert(desiredSeq < knownGoodLedger->getLedgerSeq());

        uint256 hash = knownGoodLedger->getLedgerHash(desiredSeq);

        // Not directly in the given ledger
        if (hash.isZero ())
        {
            std::uint32_t seq = (desiredSeq + 255) % 256;
            assert(seq < desiredSeq);

            uint256 i = knownGoodLedger->getLedgerHash(seq);
            if (i.isNonZero())
            {
                Ledger::pointer l = getLedgerByHash(i);
                if (l)
                {
                    hash = l->getLedgerHash(desiredSeq);
                    assert (hash.isNonZero());
                }
            }
            else
                assert(false);
        }

        return hash;
    }

    void updatePaths (Job& job)
    {
        {
            ScopedLockType ml (m_mutex);
            if (getApp().getOPs().isNeedNetworkLedger () || mCurrentLedger.empty ())
            {
                --mPathFindThread;
                return;
            }
        }


        while (! job.shouldCancel())
        {
            Ledger::pointer lastLedger;
            {
                ScopedLockType ml (m_mutex);

                if (!mValidLedger.empty() &&
                    (!mPathLedger || (mPathLedger->getLedgerSeq() != mValidLedgerSeq)))
                { // We have a new valid ledger since the last full pathfinding
                    mPathLedger = mValidLedger.get ();
                    lastLedger = mPathLedger;
                }
                else if (mPathFindNewRequest)
                { // We have a new request but no new ledger
                    lastLedger = mCurrentLedger.get ();
                }
                else
                { // Nothing to do
                    --mPathFindThread;
                    return;
                }
            }

            if (!getConfig().RUN_STANDALONE)
            { // don't pathfind with a ledger that's more than 60 seconds old
                std::int64_t age = getApp().getOPs().getCloseTimeNC();
                age -= static_cast<std::int64_t> (lastLedger->getCloseTimeNC());
                if (age > 60)
                {
                    WriteLog (lsDEBUG, LedgerMaster) << "Published ledger too old for updating paths";
                    --mPathFindThread;
                    return;
                }
            }

            try
            {
                getApp().getPathRequests().updateAll (lastLedger, job.getCancelCallback ());
            }
            catch (SHAMapMissingNode&)
            {
                WriteLog (lsINFO, LedgerMaster) << "Missing node detected during pathfinding";
                getApp().getInboundLedgers().findCreate(lastLedger->getHash (), lastLedger->getLedgerSeq (),
                    InboundLedger::fcGENERIC);
            }
        }
    }

    void newPathRequest ()
    {
        ScopedLockType ml (m_mutex);
        mPathFindNewRequest = true;

        newPFWork("pf:newRequest");
    }

    bool isNewPathRequest ()
    {
        ScopedLockType ml (m_mutex);
        if (!mPathFindNewRequest)
            return false;
        mPathFindNewRequest = false;
        return true;
    }

    // If the order book is radically updated, we need to reprocess all pathfinding requests
    void newOrderBookDB ()
    {
        ScopedLockType ml (m_mutex);
        mPathLedger.reset();

        newPFWork("pf:newOBDB");
    }

    /** A thread needs to be dispatched to handle pathfinding work of some kind
    */
    void newPFWork (const char *name)
    {
        if (mPathFindThread < 2)
        {
            ++mPathFindThread;
            getApp().getJobQueue().addJob (jtUPDATE_PF, name,
                std::bind (&LedgerMasterImp::updatePaths, this,
                           std::placeholders::_1));
        }
    }

    LockType& peekMutex ()
    {
        return m_mutex;
    }

    // The current ledger is the ledger we believe new transactions should go in
    Ledger::pointer getCurrentLedger ()
    {
        return mCurrentLedger.get ();
    }

    // The finalized ledger is the last closed/accepted ledger
    Ledger::pointer getClosedLedger ()
    {
        return mClosedLedger.get ();
    }

    // The validated ledger is the last fully validated ledger
    Ledger::pointer getValidatedLedger ()
    {
        return mValidLedger.get ();
    }

    // This is the last ledger we published to clients and can lag the validated ledger
    Ledger::ref getPublishedLedger ()
    {
        return mPubLedger;
    }

    int getMinValidations ()
    {
        return mMinValidations;
    }

    void setMinValidations (int v)
    {
        mMinValidations = v;
    }

    std::string getCompleteLedgers ()
    {
        ScopedLockType sl (mCompleteLock);
        return mCompleteLedgers.toString ();
    }

    /** Find or acquire the ledger with the specified index and the specified hash
        Return a pointer to that ledger if it is immediately available
    */
    Ledger::pointer findAcquireLedger (std::uint32_t index, uint256 const& hash)
    {
        Ledger::pointer ledger (getLedgerByHash (hash));
        if (!ledger)
        {
            InboundLedger::pointer inboundLedger =
                getApp().getInboundLedgers().findCreate (hash, index, InboundLedger::fcGENERIC);
            if (inboundLedger && inboundLedger->isComplete() && !inboundLedger->isFailed())
                ledger = inboundLedger->getLedger();
        }
        return ledger;
    }

    uint256 getHashBySeq (std::uint32_t index)
    {
        uint256 hash = mLedgerHistory.getLedgerHash (index);

        if (hash.isNonZero ())
            return hash;

        return Ledger::getHashByIndex (index);
    }

    uint256 walkHashBySeq (std::uint32_t index)
    {
        uint256 ledgerHash;
        Ledger::pointer referenceLedger;

        referenceLedger = mValidLedger.get ();
        if (referenceLedger)
            ledgerHash = walkHashBySeq (index, referenceLedger);
        return ledgerHash;
    }

    /** Walk the chain of ledger hashed to determine the hash of the
        ledger with the specified index. The referenceLedger is used as
        the base of the chain and should be fully validated and must not
        precede the target index. This function may throw if nodes
        from the reference ledger or any prior ledger are not present
        in the node store.
    */
    uint256 walkHashBySeq (std::uint32_t index, Ledger::ref referenceLedger)
    {
        uint256 ledgerHash;
        if (!referenceLedger || (referenceLedger->getLedgerSeq() < index))
            return ledgerHash; // Nothing we can do. No validated ledger.

        // See if the hash for the ledger we need is in the reference ledger
        ledgerHash = referenceLedger->getLedgerHash (index);
        if (ledgerHash.isZero())
        {
            // No, Try to get another ledger that might have the hash we need
            // Compute the index and hash of a ledger that will have the hash we need
            LedgerIndex refIndex = (index + 255) & (~255);
            LedgerHash refHash = referenceLedger->getLedgerHash (refIndex);

            bool const nonzero (refHash.isNonZero ());
            assert (nonzero);
            if (nonzero)
            {
                // We found the hash and sequence of a better reference ledger
                Ledger::pointer ledger = findAcquireLedger (refIndex, refHash);
                if (ledger)
                {
                    ledgerHash = ledger->getLedgerHash (index);
                    assert (ledgerHash.isNonZero());
                }
            }
        }
        return ledgerHash;
    }

    Ledger::pointer getLedgerBySeq (std::uint32_t index)
    {
        Ledger::pointer ret = mLedgerHistory.getLedgerBySeq (index);
        if (ret)
            return ret;

        ret = mCurrentLedger.get ();
        if (ret && (ret->getLedgerSeq () == index))
            return ret;

        ret = mClosedLedger.get ();
        if (ret && (ret->getLedgerSeq () == index))
            return ret;

        clearLedger (index);
        return Ledger::pointer();
    }

    Ledger::pointer getLedgerByHash (uint256 const& hash)
    {
        if (hash.isZero ())
            return mCurrentLedger.get ();

        Ledger::pointer ret = mLedgerHistory.getLedgerByHash (hash);
        if (ret)
            return ret;

        ret = mCurrentLedger.get ();
        if (ret && (ret->getHash () == hash))
            return ret;

        ret = mClosedLedger.get ();
        if (ret && (ret->getHash () == hash))
            return ret;

        return Ledger::pointer ();
    }

    void doLedgerCleaner(const Json::Value& parameters)
    {
        mLedgerCleaner->doClean (parameters);
    }

    void setLedgerRangePresent (std::uint32_t minV, std::uint32_t maxV)
    {
        ScopedLockType sl (mCompleteLock);
        mCompleteLedgers.setRange (minV, maxV);
    }
    void tune (int size, int age)
    {
        mLedgerHistory.tune (size, age);
    }

    void sweep ()
    {
        mLedgerHistory.sweep ();
    }

    float getCacheHitRate ()
    {
        return mLedgerHistory.getCacheHitRate ();
    }

    void addValidateCallback (callback& c)
    {
        mOnValidate.push_back (c);
    }

    beast::PropertyStream::Source& getPropertySource ()
    {
        return *mLedgerCleaner;
    }
};

//------------------------------------------------------------------------------

LedgerMaster::LedgerMaster (Stoppable& parent)
    : Stoppable ("LedgerMaster", parent)
{
}

LedgerMaster::~LedgerMaster ()
{
}

bool LedgerMaster::shouldAcquire (
    std::uint32_t currentLedger, std::uint32_t ledgerHistory, std::uint32_t candidateLedger)
{
    bool ret;

    if (candidateLedger >= currentLedger)
        ret = true;
    else
        ret = (currentLedger - candidateLedger) <= ledgerHistory;

    WriteLog (lsTRACE, LedgerMaster) << "Missing ledger " << candidateLedger << (ret ? " should" : " should NOT") << " be acquired";
    return ret;
}


LedgerMaster* LedgerMaster::New (Stoppable& parent, beast::Journal journal)
{
    return new LedgerMasterImp (parent, journal);
}

} // ripple
