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
#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/ledger/InboundLedgers.h>
#include <ripple/app/ledger/LedgerHistory.h>
#include <ripple/app/ledger/OpenLedger.h>
#include <ripple/app/ledger/OrderBookDB.h>
#include <ripple/app/ledger/PendingSaves.h>
#include <ripple/app/ledger/impl/LedgerCleaner.h>
#include <ripple/app/tx/apply.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/AmendmentTable.h>
#include <ripple/app/misc/HashRouter.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/app/misc/CanonicalTXSet.h>
#include <ripple/app/misc/SHAMapStore.h>
#include <ripple/app/misc/Validations.h>
#include <ripple/app/paths/PathRequests.h>
#include <ripple/basics/Log.h>
#include <ripple/basics/RangeSet.h>
#include <ripple/basics/TaggedCache.h>
#include <ripple/basics/UptimeTimer.h>
#include <ripple/core/LoadFeeTrack.h>
#include <ripple/core/TimeKeeper.h>
#include <ripple/overlay/Overlay.h>
#include <ripple/overlay/Peer.h>
#include <ripple/protocol/digest.h>
#include <ripple/protocol/HashPrefix.h>
#include <ripple/resource/Fees.h>
#include <algorithm>
#include <cassert>
#include <beast/cxx14/memory.h> // <memory>
#include <vector>

namespace ripple {

// 150/256ths of validations of previous ledger
#define MIN_VALIDATION_RATIO    150

// Don't catch up more than 100 ledgers (cannot exceed 256)
#define MAX_LEDGER_GAP          100

// Don't acquire history if ledger is too old
#define MAX_LEDGER_AGE_ACQUIRE  60

class LedgerMasterImp
    : public LedgerMaster
{
public:
    using LockType = RippleRecursiveMutex;
    using ScopedLockType = std::lock_guard <LockType>;
    using ScopedUnlockType = beast::GenericScopedUnlock <LockType>;

    Application& app_;
    beast::Journal m_journal;

    LockType m_mutex;

    // The ledger that most recently closed.
    LedgerHolder mClosedLedger;

    // The highest-sequence ledger we have fully accepted.
    LedgerHolder mValidLedger;

    // The last ledger we have published.
    Ledger::pointer mPubLedger;

    // The last ledger we did pathfinding against.
    Ledger::pointer mPathLedger;

    // The last ledger we handled fetching    history
    Ledger::pointer mHistLedger;

    // Fully validated ledger, whether or not we have the ledger resident.
    std::pair <uint256, LedgerIndex> mLastValidLedger;

    LedgerHistory mLedgerHistory;

    CanonicalTXSet mHeldTransactions;

    // A set of transactions to replay during the next close
    std::unique_ptr<LedgerReplay> replayData;

    LockType mCompleteLock;
    RangeSet mCompleteLedgers;

    std::unique_ptr <LedgerCleaner> mLedgerCleaner;

    int mMinValidations;    // The minimum validations to publish a ledger.
    bool mStrictValCount;   // Don't raise the minimum
    uint256 mLastValidateHash;
    std::uint32_t mLastValidateSeq;

    // Publish thread is running.
    bool                        mAdvanceThread;

    // Publish thread has work to do.
    bool                        mAdvanceWork;
    int                         mFillInProgress;

    int     mPathFindThread;    // Pathfinder jobs dispatched
    bool    mPathFindNewRequest;

    std::atomic <std::uint32_t> mPubLedgerClose;
    std::atomic <std::uint32_t> mPubLedgerSeq;
    std::atomic <std::uint32_t> mValidLedgerSign;
    std::atomic <std::uint32_t> mValidLedgerSeq;
    std::atomic <std::uint32_t> mBuildingLedgerSeq;

    // The server is in standalone mode
    bool const standalone_;

    // How many ledgers before the current ledger do we allow peers to request?
    std::uint32_t const fetch_depth_;

    // How much history do we want to keep
    std::uint32_t const ledger_history_;

    int const ledger_fetch_size_;

    TaggedCache<uint256, Blob> fetch_packs_;

    std::uint32_t fetch_seq_;

    //--------------------------------------------------------------------------

    LedgerMasterImp (Application& app, Stopwatch& stopwatch,
        Stoppable& parent,
        beast::insight::Collector::ptr const& collector, beast::Journal journal)
        : LedgerMaster (parent)
        , app_ (app)
        , m_journal (journal)
        , mLastValidLedger (std::make_pair (uint256(), 0))
        , mLedgerHistory (collector, app)
        , mHeldTransactions (uint256 ())
        , mLedgerCleaner (make_LedgerCleaner (
            app, *this, app_.journal("LedgerCleaner")))
        , mMinValidations (0)
        , mStrictValCount (false)
        , mLastValidateSeq (0)
        , mAdvanceThread (false)
        , mAdvanceWork (false)
        , mFillInProgress (0)
        , mPathFindThread (0)
        , mPathFindNewRequest (false)
        , mPubLedgerClose (0)
        , mPubLedgerSeq (0)
        , mValidLedgerSign (0)
        , mValidLedgerSeq (0)
        , mBuildingLedgerSeq (0)
        , standalone_ (app_.config().RUN_STANDALONE)
        , fetch_depth_ (app_.getSHAMapStore ().clampFetchDepth (
            app_.config().FETCH_DEPTH))
        , ledger_history_ (app_.config().LEDGER_HISTORY)
        , ledger_fetch_size_ (app_.config().getSize (siLedgerFetch))
        , fetch_packs_ ("FetchPack", 65536, 45, stopwatch,
            app_.journal("TaggedCache"))
        , fetch_seq_ (0)
    {
    }

    ~LedgerMasterImp ()
    {
    }

    LedgerIndex getCurrentLedgerIndex () override
    {
        return app_.openLedger().current()->info().seq;
    }

    LedgerIndex getValidLedgerIndex () override
    {
        return mValidLedgerSeq;
    }

    bool isCompatible (Ledger::pointer ledger,
        beast::Journal::Stream s, const char* reason) override
    {
        if (mStrictValCount)
        {
            // If we're only using validation count, then we can't
            // reject a ledger even if it's ioncompatible
            return true;
        }

        auto validLedger = getValidatedLedger();

        if (validLedger &&
            ! areCompatible (*validLedger, *ledger, s, reason))
        {
            return false;
        }

        {
            ScopedLockType sl (m_mutex);

            if ((mLastValidLedger.second != 0) &&
                ! areCompatible (mLastValidLedger.first,
                    mLastValidLedger.second, *ledger, s, reason))
            {
                return false;
            }
        }

        return true;
    }

    int getPublishedLedgerAge () override
    {
        std::uint32_t pubClose = mPubLedgerClose.load();
        if (!pubClose)
        {
            JLOG (m_journal.debug) << "No published ledger";
            return 999999;
        }

        // VFALCO int widening?
        auto ret = app_.timeKeeper().closeTime().time_since_epoch().count();
        ret -= static_cast<std::int64_t> (pubClose);
        ret = (ret > 0) ? ret : 0;

        JLOG (m_journal.trace) << "Published ledger age is " << ret;
        return static_cast<int> (ret);
    }

    int getValidatedLedgerAge () override
    {
        std::uint32_t valClose = mValidLedgerSign.load();
        if (!valClose)
        {
            JLOG (m_journal.debug) << "No validated ledger";
            return 999999;
        }

        auto ret = app_.timeKeeper().closeTime().time_since_epoch().count();
        ret -= static_cast<std::int64_t> (valClose);
        ret = (ret > 0) ? ret : 0;

        JLOG (m_journal.trace) << "Validated ledger age is " << ret;
        return static_cast<int> (ret);
    }

    bool isCaughtUp(std::string& reason) override
    {
        if (getPublishedLedgerAge() > 180)
        {
            reason = "No recently-published ledger";
            return false;
        }
        std::uint32_t validClose = mValidLedgerSign.load();
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
        std::vector <std::uint32_t> times;
        std::uint32_t signTime;
        if (! app_.config().RUN_STANDALONE)
            times = app_.getValidations().getValidationTimes(
                l->getHash());
        if (! times.empty () && times.size() >= mMinValidations)
        {
            // Calculate the sample median
            std::sort (times.begin (), times.end ());
            signTime = (times[times.size() / 2] +
                times[(times.size() - 1) / 2]) / 2;
        }
        else
        {
            signTime = l->info().closeTime;
        }

        mValidLedger.set (l);
        mValidLedgerSign = signTime;
        mValidLedgerSeq = l->info().seq;
        app_.getOPs().updateLocalTx (l);
        app_.getSHAMapStore().onLedgerClosed (getValidatedLedger());
        mLedgerHistory.validatedLedger (l);
        app_.getAmendmentTable().doValidatedLedger (l);
    }

    void setPubLedger(Ledger::ref l)
    {
        mPubLedger = l;
        mPubLedgerClose = l->info().closeTime;
        mPubLedgerSeq = l->info().seq;
    }

    void addHeldTransaction (Transaction::ref transaction) override
    {
        // returns true if transaction was added
        ScopedLockType ml (m_mutex);
        mHeldTransactions.insert (transaction->getSTransaction ());
    }

    void switchLCL (Ledger::pointer lastClosed) override
    {
        assert (lastClosed);

        lastClosed->setClosed ();

        {
            ScopedLockType ml (m_mutex);

            mClosedLedger.set (lastClosed);
        }

        if (standalone_)
        {
            setFullLedger (lastClosed, true, false);
            tryAdvance();
        }
        else
        {
            checkAccept (lastClosed);
        }
    }

    bool
    fixIndex (LedgerIndex ledgerIndex, LedgerHash const& ledgerHash) override
    {
        return mLedgerHistory.fixIndex (ledgerIndex, ledgerHash);
    }

    bool storeLedger (Ledger::pointer ledger) override
    {
        // Returns true if we already had the ledger
        return mLedgerHistory.addLedger (ledger, false);
    }

    void forceValid (Ledger::pointer ledger) override
    {
        ledger->setValidated();
        setFullLedger(ledger, true, false);
    }

    /** Apply held transactions to the open ledger
        This is normally called as we close the ledger.
        The open ledger remains open to handle new transactions
        until a new open ledger is built.
    */
    void applyHeldTransactions () override
    {
        ScopedLockType sl (m_mutex);

        app_.openLedger().modify(
            [&](OpenView& view, beast::Journal j)
            {
                bool any = false;
                for (auto const& it : mHeldTransactions)
                {
                    ApplyFlags flags = tapNONE;
                    if (app_.getHashRouter().addSuppressionFlags (
                            it.first.getTXID (), SF_SIGGOOD))
                        flags = flags | tapNO_CHECK_SIGN;

                    auto const result = apply(app_, view,
                        *it.second, flags, app_.getHashRouter(
                            ).sigVerify(), app_.config(), j);
                    if (result.second)
                        any = true;
                }
                return any;
            });

        // VFALCO TODO recreate the CanonicalTxSet object instead of resetting
        // it.
        // VFALCO NOTE The hash for an open ledger is undefined so we use
        // something that is a reasonable substitute.
        mHeldTransactions.reset (
            app_.openLedger().current()->info().parentHash);
    }

    LedgerIndex getBuildingLedger () override
    {
        // The ledger we are currently building, 0 of none
        return mBuildingLedgerSeq.load ();
    }

    void setBuildingLedger (LedgerIndex i) override
    {
        mBuildingLedgerSeq.store (i);
    }

    bool haveLedger (std::uint32_t seq) override
    {
        ScopedLockType sl (mCompleteLock);
        return mCompleteLedgers.hasValue (seq);
    }

    void clearLedger (std::uint32_t seq) override
    {
        ScopedLockType sl (mCompleteLock);
        return mCompleteLedgers.clearValue (seq);
    }

    // returns Ledgers we have all the nodes for
    bool getFullValidatedRange (std::uint32_t& minVal, std::uint32_t& maxVal) override
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
    bool getValidatedRange (std::uint32_t& minVal, std::uint32_t& maxVal) override
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

        auto const pendingSaves =
            app_.pendingSaves().getSnapshot();

        if (!pendingSaves.empty() && ((minVal != 0) || (maxVal != 0)))
        {
            // Ensure we shrink the tips as much as possible. If we have 7-9 and
            // 8,9 are invalid, we don't want to see the 8 and shrink to just 9
            // because then we'll have nothing when we could have 7.
            while (pendingSaves.count(maxVal) > 0)
                --maxVal;
            while (pendingSaves.count(minVal) > 0)
                ++minVal;

            // Best effort for remaining exclusions
            for(auto v : pendingSaves)
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
    std::uint32_t getEarliestFetch () override
    {
        // The earliest ledger we will let people fetch is ledger zero,
        // unless that creates a larger range than allowed
        std::uint32_t e = getClosedLedger()->info().seq;

        if (e > fetch_depth_)
            e -= fetch_depth_;
        else
            e = 0;
        return e;
    }

    void tryFill (Job& job, Ledger::pointer ledger)
    {
        std::uint32_t seq = ledger->info().seq;
        uint256 prevHash = ledger->info().parentHash;

        std::map< std::uint32_t, std::pair<uint256, uint256> > ledgerHashes;

        std::uint32_t minHas = ledger->info().seq;
        std::uint32_t maxHas = ledger->info().seq;

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
                if (app_.isShutdown ())
                    return;

                {
                    ScopedLockType ml (mCompleteLock);
                    mCompleteLedgers.setRange (minHas, maxHas);
                }
                maxHas = minHas;
                ledgerHashes = getHashesByIndex ((seq < 500)
                    ? 0
                    : (seq - 499), seq, app_);
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

    /** Request a fetch pack to get to the specified ledger
    */
    void getFetchPack (LedgerHash missingHash, LedgerIndex missingIndex)
    {
        uint256 haveHash = getLedgerHashForHistory (missingIndex + 1);

        if (haveHash.isZero())
        {
            JLOG (m_journal.error) << "No hash for fetch pack";
            return;
        }

        // Select target Peer based on highest score.  The score is randomized
        // but biased in favor of Peers with low latency.
        Peer::ptr target;
        {
            int maxScore = 0;
            auto peerList = app_.overlay ().getActivePeers();
            for (auto const& peer : peerList)
            {
                if (peer->hasRange (missingIndex, missingIndex + 1))
                {
                    int score = peer->getScore (true);
                    if (! target || (score > maxScore))
                    {
                        target = peer;
                        maxScore = score;
                    }
                }
            }
        }

        if (target)
        {
            protocol::TMGetObjectByHash tmBH;
            tmBH.set_query (true);
            tmBH.set_type (protocol::TMGetObjectByHash::otFETCH_PACK);
            tmBH.set_ledgerhash (haveHash.begin(), 32);
            auto packet = std::make_shared<Message> (
                tmBH, protocol::mtGET_OBJECTS);

            target->send (packet);
            JLOG (m_journal.trace) << "Requested fetch pack for "
                                             << missingIndex;
        }
        else
            JLOG (m_journal.debug) << "No peer for fetch pack";
    }

    void fixMismatch (Ledger::ref ledger) override
    {
        int invalidate = 0;
        boost::optional<uint256> hash;

        for (std::uint32_t lSeq = ledger->info().seq - 1; lSeq > 0; --lSeq)
        {
            if (haveLedger (lSeq))
            {
                try
                {
                    hash = hashOfSeq(*ledger, lSeq, m_journal);
                }
                catch (...)
                {
                    JLOG (m_journal.warning) <<
                        "fixMismatch encounters partial ledger";
                    clearLedger(lSeq);
                    return;
                }

                if (hash)
                {
                    // try to close the seam
                    Ledger::pointer otherLedger = getLedgerBySeq (lSeq);

                    if (otherLedger && (otherLedger->getHash () == *hash))
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
        }

        // all prior ledgers invalidated
        CondLog (invalidate != 0, lsWARNING, LedgerMaster) << "All " <<
            invalidate << " prior ledgers invalidated";
    }

    void setFullLedger (
        Ledger::pointer ledger, bool isSynchronous, bool isCurrent) override
    {
        // A new ledger has been accepted as part of the trusted chain
        JLOG (m_journal.debug) << "Ledger " << ledger->info().seq
                                         << "accepted :" << ledger->getHash ();
        assert (ledger->stateMap().getHash ().isNonZero ());

        ledger->setValidated();
        ledger->setFull();

        if (isCurrent)
            mLedgerHistory.addLedger(ledger, true);

        {
            // Check the SQL database's entry for the sequence before this
            // ledger, if it's not this ledger's parent, invalidate it
            uint256 prevHash = getHashByIndex (ledger->info().seq - 1, app_);
            if (prevHash.isNonZero () && prevHash != ledger->info().parentHash)
                clearLedger (ledger->info().seq - 1);
        }


        pendSaveValidated (app_, ledger, isSynchronous, isCurrent);

        {

            {
                ScopedLockType ml (mCompleteLock);
                mCompleteLedgers.setValue (ledger->info().seq);
            }

            ScopedLockType ml (m_mutex);

            if (ledger->info().seq > mValidLedgerSeq)
                setValidLedger(ledger);
            if (!mPubLedger)
            {
                setPubLedger(ledger);
                app_.getOrderBookDB().setup(ledger);
            }

            if (ledger->info().seq != 0 && haveLedger (ledger->info().seq - 1))
            {
                // we think we have the previous ledger, double check
                auto prevLedger = getLedgerBySeq (ledger->info().seq - 1);

                if (!prevLedger ||
                    (prevLedger->getHash () != ledger->info().parentHash))
                {
                    JLOG (m_journal.warning)
                            << "Acquired ledger invalidates previous ledger: "
                            << (prevLedger ? "hashMismatch" : "missingLedger");
                    fixMismatch (ledger);
                }
            }
        }
    }

    void failedSave(std::uint32_t seq, uint256 const& hash) override
    {
        clearLedger(seq);
        app_.getInboundLedgers().acquire(
            hash, seq, InboundLedger::fcGENERIC);
    }

    // Check if the specified ledger can become the new last fully-validated
    // ledger.
    void checkAccept (uint256 const& hash, std::uint32_t seq) override
    {

        int valCount;

        if (seq != 0)
        {
            // Ledger is too old
            if (seq < mValidLedgerSeq)
                return;

            valCount =
                app_.getValidations().getTrustedValidationCount (hash);

            if (valCount >= mMinValidations)
            {
                ScopedLockType ml (m_mutex);
                if (seq > mLastValidLedger.second)
                    mLastValidLedger = std::make_pair (hash, seq);

                if (!mStrictValCount && (mMinValidations < (valCount/2 + 1)))
                {
                    mMinValidations = (valCount/2 + 1);
                    JLOG (m_journal.info)
                        << "Raising minimum validations to " << mMinValidations;
                }
            }

            if (seq == mValidLedgerSeq)
                return;

            // Ledger could match the ledger we're already building
            if (seq == mBuildingLedgerSeq)
                return;
        }

        Ledger::pointer ledger = mLedgerHistory.getLedgerByHash (hash);

        if (!ledger)
        {
            if ((seq != 0) && (getValidLedgerIndex() == 0))
            {
                // Set peers sane early if we can
                if (valCount >= mMinValidations)
                    app_.overlay().checkSanity (seq);
            }

            // FIXME: We may not want to fetch a ledger with just one
            // trusted validation
            ledger = app_.getInboundLedgers().acquire(
                hash, 0, InboundLedger::fcGENERIC);
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
        if (standalone_)
            return 0;

        int minVal = mMinValidations;

        if (mLastValidateHash.isNonZero ())
        {
            int val = app_.getValidations ().getTrustedValidationCount (
                mLastValidateHash);
            val *= MIN_VALIDATION_RATIO;
            val /= 256;

            if (val > minVal)
                minVal = val;
        }

        return minVal;
    }

    void checkAccept (Ledger::ref ledger) override
    {
        if (ledger->info().seq <= mValidLedgerSeq)
            return;

        // Can we advance the last fully-validated ledger? If so, can we
        // publish?
        ScopedLockType ml (m_mutex);

        if (ledger->info().seq <= mValidLedgerSeq)
            return;

        int minVal = getNeededValidations();
        int tvc = app_.getValidations().getTrustedValidationCount(
            ledger->getHash());
        if (tvc < minVal) // nothing we can do
        {
            JLOG (m_journal.trace)
                << "Only " << tvc << " validations for " << ledger->getHash();
            return;
        }

        JLOG (m_journal.info)
            << "Advancing accepted ledger to " << ledger->info().seq
            << " with >= " << minVal << " validations";

        mLastValidateHash = ledger->getHash();
        mLastValidateSeq = ledger->info().seq;

        ledger->setValidated();
        ledger->setFull();
        setValidLedger(ledger);
        if (!mPubLedger)
        {
            pendSaveValidated(app_, ledger, true, true);
            setPubLedger(ledger);
            app_.getOrderBookDB().setup(ledger);
        }

        std::uint64_t const base = app_.getFeeTrack().getLoadBase();
        auto fees = app_.getValidations().fees (ledger->getHash(), base);
        {
            auto fees2 = app_.getValidations().fees (
                ledger->info(). parentHash, base);
            fees.reserve (fees.size() + fees2.size());
            std::copy (fees2.begin(), fees2.end(), std::back_inserter(fees));
        }
        std::uint64_t fee;
        if (! fees.empty())
        {
            std::sort (fees.begin(), fees.end());
            fee = fees[fees.size() / 2]; // median
        }
        else
        {
            fee = base;
        }

        app_.getFeeTrack().setRemoteFee(fee);

        tryAdvance ();
    }

    /** Report that the consensus process built a particular ledger */
    void consensusBuilt (Ledger::ref ledger) override
    {

        // Because we just built a ledger, we are no longer building one
        setBuildingLedger (0);

        // No need to process validations in standalone mode
        if (standalone_)
            return;

        if (ledger->info().seq <= mValidLedgerSeq)
        {
            JLOG (app_.journal ("LedgerConsensus").info)
               << "Consensus built old ledger: "
               << ledger->info().seq << " <= " << mValidLedgerSeq;
            return;
        }

        // See if this ledger can be the new fully-validated ledger
        checkAccept (ledger);

        if (ledger->info().seq <= mValidLedgerSeq)
        {
            JLOG (app_.journal ("LedgerConsensus").debug)
                << "Consensus ledger fully validated";
            return;
        }

        // This ledger cannot be the new fully-validated ledger, but
        // maybe we saved up validations for some other ledger that can be

        auto const val =
            app_.getValidations().getCurrentTrustedValidations();

        // Track validation counts with sequence numbers
        class valSeq
        {
            public:

            valSeq () : valCount_ (0), ledgerSeq_ (0) { ; }

            void mergeValidation (LedgerIndex seq)
            {
                valCount_++;

                // If we didn't already know the sequence, now we do
                if (ledgerSeq_ == 0)
                    ledgerSeq_ = seq;
            }

            int valCount_;
            LedgerIndex ledgerSeq_;
        };

        // Count the number of current, trusted validations
        hash_map <uint256, valSeq> count;
        for (auto const& v : val)
        {
            valSeq& vs = count[v->getLedgerHash()];
            vs.mergeValidation (v->getFieldU32 (sfLedgerSequence));
        }

        auto neededValidations = getNeededValidations ();
        auto maxSeq = mValidLedgerSeq.load();
        auto maxLedger = ledger->getHash();

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
                        v.second.ledgerSeq_ = ledger->info().seq;
                }

                if (v.second.ledgerSeq_ > maxSeq)
                {
                    maxSeq = v.second.ledgerSeq_;
                    maxLedger = v.first;
                }
            }

        if (maxSeq > mValidLedgerSeq)
        {
            JLOG (app_.journal ("LedgerConsensus").debug)
                << "Consensus triggered check of ledger";
            checkAccept (maxLedger, maxSeq);
        }
    }

    void advanceThread()
    {
        ScopedLockType sl (m_mutex);
        assert (!mValidLedger.empty () && mAdvanceThread);

        JLOG (m_journal.trace) << "advanceThread<";

        try
        {
            doAdvance();
        }
        catch (...)
        {
            JLOG (m_journal.fatal) << "doAdvance throws an exception";
        }

        mAdvanceThread = false;
        JLOG (m_journal.trace) << "advanceThread>";
    }

    // VFALCO NOTE This should return boost::optional<uint256>
    LedgerHash getLedgerHashForHistory (LedgerIndex index)
    {
        // Try to get the hash of a ledger we need to fetch for history
        boost::optional<LedgerHash> ret;

        if (mHistLedger && (mHistLedger->info().seq >= index))
        {
            ret = hashOfSeq(*mHistLedger, index, m_journal);
            if (! ret)
                ret = walkHashBySeq (index, mHistLedger);
        }

        if (! ret)
            ret = walkHashBySeq (index);

        return *ret;
    }

    bool shouldFetchPack (std::uint32_t seq) const
    {
        return (fetch_seq_ != seq);
    }

    bool shouldAcquire (
        std::uint32_t const currentLedger,
        std::uint32_t const ledgerHistory,
        std::uint32_t const ledgerHistoryIndex,
        std::uint32_t const candidateLedger) const;

    // Try to publish ledgers, acquire missing ledgers
    void doAdvance ();

    std::vector<Ledger::pointer> findNewLedgersToPublish ()
    {
        std::vector<Ledger::pointer> ret;

        JLOG (m_journal.trace) << "findNewLedgersToPublish<";
        if (mValidLedger.empty ())
        {
            // No valid ledger, nothing to do
        }
        else if (! mPubLedger)
        {
            JLOG (m_journal.info) << "First published ledger will be "
                                            << mValidLedgerSeq;
            ret.push_back (mValidLedger.get ());
        }
        else if (mValidLedgerSeq > (mPubLedgerSeq + MAX_LEDGER_GAP))
        {
            JLOG (m_journal.warning)
                << "Gap in validated ledger stream " << mPubLedgerSeq
                << " - " << mValidLedgerSeq - 1;
            Ledger::pointer valLedger = mValidLedger.get ();
            ret.push_back (valLedger);
            setPubLedger (valLedger);
            app_.getOrderBookDB().setup(valLedger);
        }
        else if (mValidLedgerSeq > mPubLedgerSeq)
        {
            int acqCount = 0;

            auto pubSeq = mPubLedgerSeq + 1; // Next sequence to publish
            Ledger::pointer valLedger = mValidLedger.get ();
            std::uint32_t valSeq = valLedger->info().seq;

            ScopedUnlockType sul(m_mutex);
            try
            {
                for (std::uint32_t seq = pubSeq; seq <= valSeq; ++seq)
                {
                    JLOG (m_journal.trace)
                        << "Trying to fetch/publish valid ledger " << seq;

                    Ledger::pointer ledger;
                    // This can throw
                    auto hash = hashOfSeq(*valLedger, seq, m_journal);
                    // VFALCO TODO Restructure this code so that zero is not
                    // used.
                    if (! hash)
                        hash = zero; // kludge
                    if (seq == valSeq)
                    {
                        // We need to publish the ledger we just fully validated
                        ledger = valLedger;
                    }
                    else if (hash->isZero())
                    {
                        JLOG (m_journal.fatal)
                            << "Ledger: " << valSeq
                            << " does not have hash for " << seq;
                        assert (false);
                    }
                    else
                    {
                        ledger = mLedgerHistory.getLedgerByHash (*hash);
                    }

                    // Can we try to acquire the ledger we need?
                    if (! ledger && (++acqCount < 4))
                        ledger = app_.getInboundLedgers ().acquire(
                            *hash, seq, InboundLedger::fcGENERIC);

                    // Did we acquire the next ledger we need to publish?
                    if (ledger && (ledger->info().seq == pubSeq))
                    {
                        ledger->setValidated();
                        ret.push_back (ledger);
                        ++pubSeq;
                    }

                }
            }
            catch (...)
            {
                JLOG (m_journal.error)
                    << "findNewLedgersToPublish catches an exception";
            }
        }

        JLOG (m_journal.trace)
            << "findNewLedgersToPublish> " << ret.size();
        return ret;
    }

    void tryAdvance() override
    {
        ScopedLockType ml (m_mutex);

        // Can't advance without at least one fully-valid ledger
        mAdvanceWork = true;
        if (!mAdvanceThread && !mValidLedger.empty ())
        {
            mAdvanceThread = true;
            app_.getJobQueue ().addJob (
                jtADVANCE, "advanceLedger",
                [this] (Job&) { advanceThread(); });
        }
    }

    // Return the hash of the valid ledger with a particular sequence, given a
    // subsequent ledger known valid.
    // VFALCO NOTE This should return boost::optional<uint256>
    uint256 getLedgerHash(std::uint32_t desiredSeq, Ledger::ref knownGoodLedger) override
    {
        assert(desiredSeq < knownGoodLedger->info().seq);

        auto hash = hashOfSeq(*knownGoodLedger, desiredSeq, m_journal);

        // Not directly in the given ledger
        if (! hash)
        {
            std::uint32_t seq = (desiredSeq + 255) % 256;
            assert(seq < desiredSeq);

            hash = hashOfSeq(*knownGoodLedger, seq, m_journal);
            if (hash)
            {
                auto l = getLedgerByHash(*hash);
                if (l)
                {
                    hash = hashOfSeq(*l, desiredSeq, m_journal);
                    assert (hash);
                }
            }
            else
            {
                assert(false);
            }
        }

        // VFALCO NOTE This shouldn't be needed, but
        //             preserves original behavior.
        return hash ? *hash : zero; // kludge
    }

    void updatePaths (Job& job)
    {
        {
            ScopedLockType ml (m_mutex);
            if (app_.getOPs().isNeedNetworkLedger())
            {
                --mPathFindThread;
                return;
            }
        }


        while (! job.shouldCancel())
        {
            std::shared_ptr<ReadView const> lastLedger;
            {
                ScopedLockType ml (m_mutex);

                if (!mValidLedger.empty() &&
                    (!mPathLedger ||
                     (mPathLedger->info().seq != mValidLedgerSeq)))
                { // We have a new valid ledger since the last full pathfinding
                    mPathLedger = mValidLedger.get ();
                    lastLedger = mPathLedger;
                }
                else if (mPathFindNewRequest)
                { // We have a new request but no new ledger
                    lastLedger = app_.openLedger().current();
                }
                else
                { // Nothing to do
                    --mPathFindThread;
                    return;
                }
            }

            if (!standalone_)
            { // don't pathfind with a ledger that's more than 60 seconds old
                auto age = app_.timeKeeper().closeTime().time_since_epoch()
                        .count();
                age -= static_cast<std::int64_t> (lastLedger->info().closeTime);
                if (age > 60)
                {
                    JLOG (m_journal.debug)
                            << "Published ledger too old for updating paths";
                    --mPathFindThread;
                    return;
                }
            }

            try
            {
                app_.getPathRequests().updateAll(
                    lastLedger, job.getCancelCallback());
            }
            catch (SHAMapMissingNode&)
            {
                JLOG (m_journal.info)
                        << "Missing node detected during pathfinding";
                if (lastLedger->info().open)
                {
                    // our parent is the problem
                    app_.getInboundLedgers().acquire(
                        lastLedger->info().parentHash, lastLedger->info().seq - 1,
                        InboundLedger::fcGENERIC);
                }
                else
                {
                    // this ledger is the problem
                    app_.getInboundLedgers().acquire(
                        lastLedger->info().hash, lastLedger->info().seq,
                        InboundLedger::fcGENERIC);
                 }
            }
        }
    }

    void newPathRequest () override
    {
        ScopedLockType ml (m_mutex);
        mPathFindNewRequest = true;

        newPFWork("pf:newRequest");
    }

    bool isNewPathRequest () override
    {
        ScopedLockType ml (m_mutex);
        if (!mPathFindNewRequest)
            return false;
        mPathFindNewRequest = false;
        return true;
    }

    // If the order book is radically updated, we need to reprocess all
    // pathfinding requests.
    void newOrderBookDB () override
    {
        ScopedLockType ml (m_mutex);
        mPathLedger.reset();

        newPFWork("pf:newOBDB");
    }

    /** A thread needs to be dispatched to handle pathfinding work of some kind.
    */
    void newPFWork (const char *name)
    {
        if (mPathFindThread < 2)
        {
            ++mPathFindThread;
            app_.getJobQueue().addJob (
                jtUPDATE_PF, name,
                [this] (Job& j) { updatePaths(j); });
        }
    }

    LockType& peekMutex () override
    {
        return m_mutex;
    }

    // The current ledger is the ledger we believe new transactions should go in
    std::shared_ptr<ReadView const> getCurrentLedger () override
    {
        return app_.openLedger().current();
    }

    // The finalized ledger is the last closed/accepted ledger
    Ledger::pointer getClosedLedger () override
    {
        return mClosedLedger.get ();
    }

    // The validated ledger is the last fully validated ledger
    Ledger::pointer getValidatedLedger () override
    {
        return mValidLedger.get ();
    }

    Rules getValidatedRules () override
    {
        // Once we have a guarantee that there's always a last validated
        // ledger then we can dispense with the if.

        // Return the Rules from the last validated ledger.
        if (auto const ledger = getValidatedLedger())
            return ledger->rules();

        return Rules();
   }

    // This is the last ledger we published to clients and can lag the validated
    // ledger.
    Ledger::ref getPublishedLedger () override
    {
        return mPubLedger;
    }

    bool isValidLedger(LedgerInfo const& info) override
    {
        if (info.validated)
            return true;

        if (info.open)
            return false;

        auto seq = info.seq;
        try
        {
            // Use the skip list in the last validated ledger to see if ledger
            // comes before the last validated ledger (and thus has been
            // validated).
            auto hash = walkHashBySeq (seq);
            if (info.hash != hash)
                return false;
        }
        catch (SHAMapMissingNode const&)
        {
            JLOG (app_.journal ("RPCHandler").warning)
                    << "Missing SHANode " << std::to_string (seq);
            return false;
        }

        // Mark ledger as validated to save time if we see it again.
        info.validated = true;
        return true;
    }

    int getMinValidations () override
    {
        return mMinValidations;
    }

    void setMinValidations (int v, bool strict) override
    {
        JLOG (m_journal.info) << "Validation quorum: " << v
            << (strict ? " strict" : "");
        mMinValidations = v;
        mStrictValCount = strict;
    }

    std::string getCompleteLedgers () override
    {
        ScopedLockType sl (mCompleteLock);
        return mCompleteLedgers.toString ();
    }

    uint256 getHashBySeq (std::uint32_t index) override
    {
        uint256 hash = mLedgerHistory.getLedgerHash (index);

        if (hash.isNonZero ())
            return hash;

        return getHashByIndex (index, app_);
    }

    // VFALCO NOTE This should return boost::optional<uint256>
    uint256 walkHashBySeq (std::uint32_t index) override
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
    // VFALCO NOTE This should return boost::optional<uint256>
    uint256 walkHashBySeq (std::uint32_t index, Ledger::ref referenceLedger) override
    {
        if (!referenceLedger || (referenceLedger->info().seq < index))
        {
            // Nothing we can do. No validated ledger.
            return zero;
        }

        // See if the hash for the ledger we need is in the reference ledger
        auto ledgerHash = hashOfSeq(*referenceLedger, index, m_journal);
        if (ledgerHash)
            return *ledgerHash;

        // The hash is not in the reference ledger. Get another ledger which can
        // be located easily and should contain the hash.
        LedgerIndex refIndex = getCandidateLedger(index);
        auto const refHash = hashOfSeq(*referenceLedger, refIndex, m_journal);
        assert(refHash);
        if (refHash)
        {
            // Try the hash and sequence of a better reference ledger just found
            auto ledger = mLedgerHistory.getLedgerByHash (*refHash);

            if (ledger)
            {
                try
                {
                    ledgerHash = hashOfSeq(*ledger, index, m_journal);
                }
                catch(SHAMapMissingNode&)
                {
                    ledger.reset();
                }
            }

            // Try to acquire the complete ledger
            if (!ledger)
            {
                auto const ledger = app_.getInboundLedgers().acquire (
                    *refHash, refIndex, InboundLedger::fcGENERIC);
                if (ledger)
                {
                    ledgerHash = hashOfSeq(*ledger, index, m_journal);
                    assert (ledgerHash);
                }
            }
        }
        return ledgerHash ? *ledgerHash : zero; // kludge
    }

    Ledger::pointer getLedgerBySeq (std::uint32_t index) override
    {
        if (index <= mValidLedgerSeq)
        {
            // Always prefer a validated ledger
            auto valid = mValidLedger.get ();
            if (valid)
            {
                if (valid->info().seq == index)
                    return valid;

                try
                {
                    auto const hash = hashOfSeq(*valid, index, m_journal);
                    if (hash)
                        return mLedgerHistory.getLedgerByHash (*hash);
                }
                catch (...)
                {
                    // Missing nodes are already handled
                }
            }
        }

        Ledger::pointer ret = mLedgerHistory.getLedgerBySeq (index);
        if (ret)
            return ret;

        ret = mClosedLedger.get ();
        if (ret && (ret->info().seq == index))
            return ret;

        clearLedger (index);
        return Ledger::pointer();
    }

    Ledger::pointer getLedgerByHash (uint256 const& hash) override
    {
        Ledger::pointer ret = mLedgerHistory.getLedgerByHash (hash);
        if (ret)
            return ret;

        ret = mClosedLedger.get ();
        if (ret && (ret->getHash () == hash))
            return ret;

        return Ledger::pointer ();
    }

    void doLedgerCleaner(Json::Value const& parameters) override
    {
        mLedgerCleaner->doClean (parameters);
    }

    void setLedgerRangePresent (std::uint32_t minV, std::uint32_t maxV) override
    {
        ScopedLockType sl (mCompleteLock);
        mCompleteLedgers.setRange (minV, maxV);
    }
    void tune (int size, int age) override
    {
        mLedgerHistory.tune (size, age);
    }

    void sweep () override
    {
        mLedgerHistory.sweep ();
        fetch_packs_.sweep ();
    }

    float getCacheHitRate () override
    {
        return mLedgerHistory.getCacheHitRate ();
    }

    beast::PropertyStream::Source& getPropertySource () override
    {
        return *mLedgerCleaner;
    }

    void clearPriorLedgers (LedgerIndex seq) override
    {
        ScopedLockType sl (mCompleteLock);
        for (LedgerIndex i = mCompleteLedgers.getFirst(); i < seq; ++i)
        {
            if (haveLedger (i))
                clearLedger (i);
        }
    }

    void clearLedgerCachePrior (LedgerIndex seq) override
    {
        mLedgerHistory.clearLedgerCachePrior (seq);
    }

    void takeReplay (std::unique_ptr<LedgerReplay> replay) override
    {
        replayData = std::move (replay);
    }

    std::unique_ptr<LedgerReplay> releaseReplay () override
    {
        return std::move (replayData);
    }

    // Fetch packs:
    void gotFetchPack (
        bool progress,
        std::uint32_t seq) override;

    void addFetchPack (
        uint256 const& hash,
        std::shared_ptr<Blob>& data) override;

    bool getFetchPack (
        uint256 const& hash,
        Blob& data) override;

    void makeFetchPack (
        std::weak_ptr<Peer> const& wPeer,
        std::shared_ptr<protocol::TMGetObjectByHash> const& request,
        uint256 haveLedgerHash,
        std::uint32_t uUptime) override;

    std::size_t getFetchPackCacheSize () const override;
};

bool LedgerMasterImp::shouldAcquire (
    std::uint32_t const currentLedger,
    std::uint32_t const ledgerHistory,
    std::uint32_t const ledgerHistoryIndex,
    std::uint32_t const candidateLedger) const
{
    bool ret (candidateLedger >= currentLedger ||
        candidateLedger > ledgerHistoryIndex ||
        (currentLedger - candidateLedger) <= ledgerHistory);

    JLOG (m_journal.trace)
        << "Missing ledger "
        << candidateLedger
        << (ret ? " should" : " should NOT")
        << " be acquired";
    return ret;
}

// Try to publish ledgers, acquire missing ledgers
void LedgerMasterImp::doAdvance ()
{
    // TODO NIKB: simplify and unindent this a bit!

    do
    {
        mAdvanceWork = false; // If there's work to do, we'll make progress
        bool progress = false;

        auto const pubLedgers = findNewLedgersToPublish ();
        if (pubLedgers.empty())
        {
            if (!standalone_ && !app_.getFeeTrack().isLoadedLocal() &&
                (app_.getJobQueue().getJobCount(jtPUBOLDLEDGER) < 10) &&
                (mValidLedgerSeq == mPubLedgerSeq) &&
                (getValidatedLedgerAge() < MAX_LEDGER_AGE_ACQUIRE))
            { // We are in sync, so can acquire
                std::uint32_t missing;
                {
                    ScopedLockType sl (mCompleteLock);
                    missing = mCompleteLedgers.prevMissing(
                        mPubLedger->info().seq);
                }
                JLOG (m_journal.trace)
                    << "tryAdvance discovered missing " << missing;
                if ((missing != RangeSet::absent) && (missing > 0) &&
                    shouldAcquire (mValidLedgerSeq, ledger_history_,
                        app_.getSHAMapStore ().getCanDelete (), missing) &&
                    ((mFillInProgress == 0) || (missing > mFillInProgress)))
                {
                    JLOG (m_journal.trace)
                        << "advanceThread should acquire";
                    {
                        ScopedUnlockType sl(m_mutex);
                        uint256 hash = getLedgerHashForHistory (missing);
                        if (hash.isNonZero())
                        {
                            Ledger::pointer ledger = getLedgerByHash (hash);
                            if (!ledger)
                            {
                                if (!app_.getInboundLedgers().isFailure (
                                        hash))
                                {
                                    ledger =
                                        app_.getInboundLedgers().acquire(
                                            hash, missing,
                                            InboundLedger::fcHISTORY);
                                    if (! ledger && (missing > 32600) &&
                                        shouldFetchPack (missing))
                                    {
                                        JLOG (m_journal.trace) <<
                                        "tryAdvance want fetch pack " <<
                                        missing;
                                        fetch_seq_ = missing;
                                        getFetchPack(hash, missing);
                                    }
                                    else
                                        JLOG (m_journal.trace) <<
                                        "tryAdvance no fetch pack for " <<
                                        missing;
                                }
                                else
                                    JLOG (m_journal.debug) <<
                                    "tryAdvance found failed acquire";
                            }
                            if (ledger)
                            {
                                auto seq = ledger->info().seq;
                                assert(seq == missing);
                                JLOG (m_journal.trace)
                                        << "tryAdvance acquired "
                                        << ledger->info().seq;
                                setFullLedger(ledger, false, false);
                                mHistLedger = ledger;
                                auto& parent = ledger->info().parentHash;
                                if (mFillInProgress == 0 &&
                                    getHashByIndex(seq - 1, app_) == parent)
                                {
                                    // Previous ledger is in DB
                                    ScopedLockType lock (m_mutex);
                                    mFillInProgress = ledger->info().seq;
                                    app_.getJobQueue().addJob(
                                        jtADVANCE, "tryFill",
                                        [this, ledger] (Job& j) {
                                            tryFill(j, ledger);
                                        });
                                }
                                progress = true;
                            }
                            else
                            {
                                try
                                {
                                    for (int i = 0; i < ledger_fetch_size_; ++i)
                                    {
                                        std::uint32_t seq = missing - i;
                                        auto hash =
                                                getLedgerHashForHistory(seq);
                                        if (hash.isNonZero())
                                            app_.getInboundLedgers().acquire
                                                    (hash, seq,
                                                     InboundLedger::fcHISTORY);
                                    }
                                }
                                catch (...)
                                {
                                    JLOG (m_journal.warning) <<
                                    "Threw while prefetching";
                                }
                            }
                        }
                        else
                        {
                            JLOG (m_journal.fatal) <<
                                "Can't find ledger following prevMissing " <<
                                missing;
                            JLOG (m_journal.fatal) << "Pub:" <<
                                mPubLedgerSeq << " Val:" << mValidLedgerSeq;
                            JLOG (m_journal.fatal) << "Ledgers: " <<
                                app_.getLedgerMaster().getCompleteLedgers();
                            clearLedger (missing + 1);
                            progress = true;
                        }
                    }
                    if (mValidLedgerSeq != mPubLedgerSeq)
                    {
                        JLOG (m_journal.debug) <<
                            "tryAdvance found last valid changed";
                        progress = true;
                    }
                }
            }
            else
            {
                mHistLedger.reset();
                JLOG (m_journal.trace) <<
                    "tryAdvance not fetching history";
            }
        }
        else
        {
            JLOG (m_journal.trace) <<
                "tryAdvance found " << pubLedgers.size() <<
                " ledgers to publish";
            for(auto ledger : pubLedgers)
            {
                {
                    ScopedUnlockType sul (m_mutex);
                    JLOG (m_journal.debug) <<
                        "tryAdvance publishing seq " << ledger->info().seq;

                    setFullLedger(ledger, true, true);
                    app_.getOPs().pubLedger(ledger);
                }

                setPubLedger(ledger);
                progress = true;
            }

            app_.getOPs().clearNeedNetworkLedger();
            newPFWork ("pf:newLedger");
        }
        if (progress)
            mAdvanceWork = true;
    } while (mAdvanceWork);
}

void LedgerMasterImp::addFetchPack (
    uint256 const& hash,
    std::shared_ptr< Blob >& data)
{
    fetch_packs_.canonicalize (hash, data);
}

bool LedgerMasterImp::getFetchPack (
    uint256 const& hash,
    Blob& data)
{
    if (!fetch_packs_.retrieve (hash, data))
        return false;

    fetch_packs_.del (hash, false);

    return hash == sha512Half(makeSlice(data));
}

void LedgerMasterImp::gotFetchPack (
    bool progress,
    std::uint32_t seq)
{
    // FIXME: Calling this function more than once will result in
    // InboundLedgers::gotFetchPack being called more than once
    // which is expensive. A flag should track whether we've already dispatched

    app_.getJobQueue().addJob (
        jtLEDGER_DATA, "gotFetchPack",
        [&] (Job&) { app_.getInboundLedgers().gotFetchPack(); });
}

void LedgerMasterImp::makeFetchPack (
    std::weak_ptr<Peer> const& wPeer,
    std::shared_ptr<protocol::TMGetObjectByHash> const& request,
    uint256 haveLedgerHash,
    std::uint32_t uUptime)
{
    if (UptimeTimer::getInstance ().getElapsedSeconds () > (uUptime + 1))
    {
        m_journal.info << "Fetch pack request got stale";
        return;
    }

    if (app_.getFeeTrack ().isLoadedLocal () ||
        (getValidatedLedgerAge() > 40))
    {
        m_journal.info << "Too busy to make fetch pack";
        return;
    }

    Peer::ptr peer = wPeer.lock ();

    if (!peer)
        return;

    auto haveLedger = getLedgerByHash (haveLedgerHash);

    if (!haveLedger)
    {
        m_journal.info
            << "Peer requests fetch pack for ledger we don't have: "
            << haveLedger;
        peer->charge (Resource::feeRequestNoReply);
        return;
    }

    if (haveLedger->info().open)
    {
        m_journal.warning
            << "Peer requests fetch pack from open ledger: "
            << haveLedger;
        peer->charge (Resource::feeInvalidRequest);
        return;
    }

    if (haveLedger->info().seq < getEarliestFetch())
    {
        m_journal.debug << "Peer requests fetch pack that is too early";
        peer->charge (Resource::feeInvalidRequest);
        return;
    }

    auto wantLedger = getLedgerByHash (haveLedger->info().parentHash);

    if (!wantLedger)
    {
        m_journal.info
            << "Peer requests fetch pack for ledger whose predecessor we "
            << "don't have: " << haveLedger;
        peer->charge (Resource::feeRequestNoReply);
        return;
    }


    auto fpAppender = [](
        protocol::TMGetObjectByHash* reply,
        std::uint32_t ledgerSeq,
        uint256 const& hash,
        const Blob& blob)
    {
        protocol::TMIndexedObject& newObj = * (reply->add_objects ());
        newObj.set_ledgerseq (ledgerSeq);
        newObj.set_hash (hash.begin (), 256 / 8);
        newObj.set_data (&blob[0], blob.size ());
    };

    try
    {
        protocol::TMGetObjectByHash reply;
        reply.set_query (false);

        if (request->has_seq ())
            reply.set_seq (request->seq ());

        reply.set_ledgerhash (request->ledgerhash ());
        reply.set_type (protocol::TMGetObjectByHash::otFETCH_PACK);

        // Building a fetch pack:
        //  1. Add the header for the requested ledger.
        //  2. Add the nodes for the AccountStateMap of that ledger.
        //  3. If there are transactions, add the nodes for the
        //     transactions of the ledger.
        //  4. If the FetchPack now contains greater than or equal to
        //     256 entries then stop.
        //  5. If not very much time has elapsed, then loop back and repeat
        //     the same process adding the previous ledger to the FetchPack.
        do
        {
            std::uint32_t lSeq = wantLedger->info().seq;

            protocol::TMIndexedObject& newObj = *reply.add_objects ();
            newObj.set_hash (wantLedger->getHash ().begin (), 256 / 8);
            Serializer s (256);
            s.add32 (HashPrefix::ledgerMaster);
            wantLedger->addRaw (s);
            newObj.set_data (s.getDataPtr (), s.getLength ());
            newObj.set_ledgerseq (lSeq);

            wantLedger->stateMap().getFetchPack
                (&haveLedger->stateMap(), true, 16384,
                    std::bind (fpAppender, &reply, lSeq, std::placeholders::_1,
                               std::placeholders::_2));

            if (wantLedger->info().txHash.isNonZero ())
                wantLedger->txMap().getFetchPack (
                    nullptr, true, 512,
                    std::bind (fpAppender, &reply, lSeq, std::placeholders::_1,
                               std::placeholders::_2));

            if (reply.objects ().size () >= 512)
                break;

            // move may save a ref/unref
            haveLedger = std::move (wantLedger);
            wantLedger = getLedgerByHash (haveLedger->info().parentHash);
        }
        while (wantLedger &&
               UptimeTimer::getInstance ().getElapsedSeconds () <= uUptime + 1);

        m_journal.info
            << "Built fetch pack with " << reply.objects ().size () << " nodes";
        auto msg = std::make_shared<Message> (reply, protocol::mtGET_OBJECTS);
        peer->send (msg);
    }
    catch (...)
    {
        m_journal.warning << "Exception building fetch pach";
    }
}

std::size_t LedgerMasterImp::getFetchPackCacheSize () const
{
    return fetch_packs_.getCacheSize ();
}

//------------------------------------------------------------------------------

LedgerMaster::LedgerMaster (Stoppable& parent)
    : Stoppable ("LedgerMaster", parent)
{
}

std::unique_ptr<LedgerMaster>
make_LedgerMaster (
    Application& app,
    Stopwatch& stopwatch,
    beast::Stoppable& parent,
    beast::insight::Collector::ptr const& collector,
    beast::Journal journal)
{
    return std::make_unique <LedgerMasterImp> (
        app, stopwatch, parent, collector, journal);
}

} // ripple
