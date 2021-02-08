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

#ifndef RIPPLE_APP_LEDGER_LEDGERMASTER_H_INCLUDED
#define RIPPLE_APP_LEDGER_LEDGERMASTER_H_INCLUDED

#include <ripple/app/ledger/AbstractFetchPackContainer.h>
#include <ripple/app/ledger/InboundLedgers.h>
#include <ripple/app/ledger/Ledger.h>
#include <ripple/app/ledger/LedgerHistory.h>
#include <ripple/app/ledger/LedgerHolder.h>
#include <ripple/app/ledger/LedgerReplay.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/CanonicalTXSet.h>
#include <ripple/basics/RangeSet.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/basics/UptimeClock.h>
#include <ripple/basics/chrono.h>
#include <ripple/beast/insight/Collector.h>
#include <ripple/protocol/Protocol.h>
#include <ripple/protocol/RippleLedgerHash.h>
#include <ripple/protocol/STValidation.h>
#include <ripple/protocol/messages.h>
#include <boost/optional.hpp>

#include <mutex>

namespace ripple {

class Peer;
class Transaction;

// This error is thrown when a codepath tries to access the open or closed
// ledger while the server is running in reporting mode. Any RPCs that request
// the open or closed ledger should be forwarded to a p2p node. Usually, the
// decision to forward is made based on the required condition of the handler,
// or which ledger is specified. However, there are some codepaths which are not
// covered by the aforementioned logic (though they probably should), so this
// error is thrown in case a codepath falls through the cracks.
class ReportingShouldProxy : public std::runtime_error
{
public:
    ReportingShouldProxy()
        : std::runtime_error(
              "Reporting mode has no open or closed ledger. Proxy this "
              "request")
    {
    }
};

// Tracks the current ledger and any ledgers in the process of closing
// Tracks ledger history
// Tracks held transactions
class LedgerMaster : public AbstractFetchPackContainer
{
public:
    // Age for last validated ledger if the process has yet to validate.
    static constexpr std::chrono::seconds NO_VALIDATED_LEDGER_AGE =
        std::chrono::hours{24 * 14};

    explicit LedgerMaster(
        Application& app,
        Stopwatch& stopwatch,
        beast::insight::Collector::ptr const& collector,
        beast::Journal journal);

    virtual ~LedgerMaster() = default;

    LedgerIndex
    getCurrentLedgerIndex();
    LedgerIndex
    getValidLedgerIndex();

    bool
    isCompatible(ReadView const&, beast::Journal::Stream, char const* reason);

    std::recursive_mutex&
    peekMutex();

    // The current ledger is the ledger we believe new transactions should go in
    std::shared_ptr<ReadView const>
    getCurrentLedger();

    // The finalized ledger is the last closed/accepted ledger
    std::shared_ptr<Ledger const>
    getClosedLedger()
    {
        if (app_.config().reporting())
        {
            Throw<ReportingShouldProxy>();
        }
        return mClosedLedger.get();
    }

    // The validated ledger is the last fully validated ledger.
    std::shared_ptr<Ledger const>
    getValidatedLedger();

    // The Rules are in the last fully validated ledger if there is one.
    Rules
    getValidatedRules();

    // This is the last ledger we published to clients and can lag the validated
    // ledger
    std::shared_ptr<ReadView const>
    getPublishedLedger();

    std::chrono::seconds
    getPublishedLedgerAge();
    std::chrono::seconds
    getValidatedLedgerAge();
    bool
    isCaughtUp(std::string& reason);

    std::uint32_t
    getEarliestFetch();

    bool
    storeLedger(std::shared_ptr<Ledger const> ledger);

    void
    setFullLedger(
        std::shared_ptr<Ledger const> const& ledger,
        bool isSynchronous,
        bool isCurrent);

    /** Check the sequence number and parent close time of a
        ledger against our clock and last validated ledger to
        see if it can be the network's current ledger
    */
    bool
    canBeCurrent(std::shared_ptr<Ledger const> const& ledger);

    void
    switchLCL(std::shared_ptr<Ledger const> const& lastClosed);

    void
    failedSave(std::uint32_t seq, uint256 const& hash);

    std::string
    getCompleteLedgers();

    /** Apply held transactions to the open ledger
        This is normally called as we close the ledger.
        The open ledger remains open to handle new transactions
        until a new open ledger is built.
    */
    void
    applyHeldTransactions();

    /** Get the next transaction held for a particular account if any.
        This is normally called when a transaction for that account is
        successfully applied to the open ledger so the next transaction
        can be resubmitted without waiting for ledger close.
    */
    std::shared_ptr<STTx const>
    popAcctTransaction(std::shared_ptr<STTx const> const& tx);

    /** Get a ledger's hash by sequence number using the cache
     */
    uint256
    getHashBySeq(std::uint32_t index);

    /** Walk to a ledger's hash using the skip list */
    boost::optional<LedgerHash>
    walkHashBySeq(std::uint32_t index, InboundLedger::Reason reason);

    /** Walk the chain of ledger hashes to determine the hash of the
        ledger with the specified index. The referenceLedger is used as
        the base of the chain and should be fully validated and must not
        precede the target index. This function may throw if nodes
        from the reference ledger or any prior ledger are not present
        in the node store.
    */
    boost::optional<LedgerHash>
    walkHashBySeq(
        std::uint32_t index,
        std::shared_ptr<ReadView const> const& referenceLedger,
        InboundLedger::Reason reason);

    std::shared_ptr<Ledger const>
    getLedgerBySeq(std::uint32_t index);

    std::shared_ptr<Ledger const>
    getLedgerByHash(uint256 const& hash);

    void
    setLedgerRangePresent(std::uint32_t minV, std::uint32_t maxV);

    boost::optional<NetClock::time_point>
    getCloseTimeBySeq(LedgerIndex ledgerIndex);

    boost::optional<NetClock::time_point>
    getCloseTimeByHash(LedgerHash const& ledgerHash, LedgerIndex ledgerIndex);

    void
    addHeldTransaction(std::shared_ptr<Transaction> const& trans);
    void
    fixMismatch(ReadView const& ledger);

    bool
    haveLedger(std::uint32_t seq);
    void
    clearLedger(std::uint32_t seq);
    bool
    getValidatedRange(std::uint32_t& minVal, std::uint32_t& maxVal);
    bool
    getFullValidatedRange(std::uint32_t& minVal, std::uint32_t& maxVal);

    void
    tune(int size, std::chrono::seconds age);
    void
    sweep();
    float
    getCacheHitRate();

    void
    checkAccept(std::shared_ptr<Ledger const> const& ledger);
    void
    checkAccept(uint256 const& hash, std::uint32_t seq);
    void
    consensusBuilt(
        std::shared_ptr<Ledger const> const& ledger,
        uint256 const& consensusHash,
        Json::Value consensus);

    LedgerIndex
    getBuildingLedger();
    void
    setBuildingLedger(LedgerIndex index);

    void
    tryAdvance();
    bool
    newPathRequest();  // Returns true if path request successfully placed.
    bool
    isNewPathRequest();
    bool
    newOrderBookDB();  // Returns true if able to fulfill request.

    bool
    fixIndex(LedgerIndex ledgerIndex, LedgerHash const& ledgerHash);

    void
    clearPriorLedgers(LedgerIndex seq);

    void
    clearLedgerCachePrior(LedgerIndex seq);

    // ledger replay
    void
    takeReplay(std::unique_ptr<LedgerReplay> replay);
    std::unique_ptr<LedgerReplay>
    releaseReplay();

    // Fetch Packs
    void
    gotFetchPack(bool progress, std::uint32_t seq);

    void
    addFetchPack(uint256 const& hash, std::shared_ptr<Blob> data);

    boost::optional<Blob>
    getFetchPack(uint256 const& hash) override;

    void
    makeFetchPack(
        std::weak_ptr<Peer> const& wPeer,
        std::shared_ptr<protocol::TMGetObjectByHash> const& request,
        uint256 haveLedgerHash,
        UptimeClock::time_point uptime);

    std::size_t
    getFetchPackCacheSize() const;

    //! Whether we have ever fully validated a ledger.
    bool
    haveValidated()
    {
        return !mValidLedger.empty();
    }

    // Returns the minimum ledger sequence in SQL database, if any.
    boost::optional<LedgerIndex>
    minSqlSeq();

private:
    void
    setValidLedger(std::shared_ptr<Ledger const> const& l);
    void
    setPubLedger(std::shared_ptr<Ledger const> const& l);

    void
    tryFill(Job& job, std::shared_ptr<Ledger const> ledger);

    void
    getFetchPack(LedgerIndex missing, InboundLedger::Reason reason);

    boost::optional<LedgerHash>
    getLedgerHashForHistory(LedgerIndex index, InboundLedger::Reason reason);

    std::size_t
    getNeededValidations();
    void
    advanceThread();
    void
    fetchForHistory(
        std::uint32_t missing,
        bool& progress,
        InboundLedger::Reason reason,
        std::unique_lock<std::recursive_mutex>&);
    // Try to publish ledgers, acquire missing ledgers.  Always called with
    // m_mutex locked.  The passed lock is a reminder to callers.
    void
    doAdvance(std::unique_lock<std::recursive_mutex>&);

    std::vector<std::shared_ptr<Ledger const>>
    findNewLedgersToPublish(std::unique_lock<std::recursive_mutex>&);

    void
    updatePaths(Job& job);

    // Returns true if work started.  Always called with m_mutex locked.
    // The passed lock is a reminder to callers.
    bool
    newPFWork(const char* name, std::unique_lock<std::recursive_mutex>&);

    Application& app_;
    beast::Journal m_journal;

    std::recursive_mutex mutable m_mutex;

    // The ledger that most recently closed.
    LedgerHolder mClosedLedger;

    // The highest-sequence ledger we have fully accepted.
    LedgerHolder mValidLedger;

    // The last ledger we have published.
    std::shared_ptr<Ledger const> mPubLedger;

    // The last ledger we did pathfinding against.
    std::shared_ptr<Ledger const> mPathLedger;

    // The last ledger we handled fetching history
    std::shared_ptr<Ledger const> mHistLedger;

    // The last ledger we handled fetching for a shard
    std::shared_ptr<Ledger const> mShardLedger;

    // Fully validated ledger, whether or not we have the ledger resident.
    std::pair<uint256, LedgerIndex> mLastValidLedger{uint256(), 0};

    LedgerHistory mLedgerHistory;

    CanonicalTXSet mHeldTransactions{uint256()};

    // A set of transactions to replay during the next close
    std::unique_ptr<LedgerReplay> replayData;

    std::recursive_mutex mCompleteLock;
    RangeSet<std::uint32_t> mCompleteLedgers;

    // Publish thread is running.
    bool mAdvanceThread{false};

    // Publish thread has work to do.
    bool mAdvanceWork{false};
    int mFillInProgress{0};

    int mPathFindThread{0};  // Pathfinder jobs dispatched
    bool mPathFindNewRequest{false};

    std::atomic_flag mGotFetchPackThread =
        ATOMIC_FLAG_INIT;  // GotFetchPack jobs dispatched

    std::atomic<std::uint32_t> mPubLedgerClose{0};
    std::atomic<LedgerIndex> mPubLedgerSeq{0};
    std::atomic<std::uint32_t> mValidLedgerSign{0};
    std::atomic<LedgerIndex> mValidLedgerSeq{0};
    std::atomic<LedgerIndex> mBuildingLedgerSeq{0};

    // The server is in standalone mode
    bool const standalone_;

    // How many ledgers before the current ledger do we allow peers to request?
    std::uint32_t const fetch_depth_;

    // How much history do we want to keep
    std::uint32_t const ledger_history_;

    std::uint32_t const ledger_fetch_size_;

    TaggedCache<uint256, Blob> fetch_packs_;

    std::uint32_t fetch_seq_{0};

    // Try to keep a validator from switching from test to live network
    // without first wiping the database.
    LedgerIndex const max_ledger_difference_{1000000};

    // Time that the previous upgrade warning was issued.
    TimeKeeper::time_point upgradeWarningPrevTime_{};

private:
    struct Stats
    {
        template <class Handler>
        Stats(
            Handler const& handler,
            beast::insight::Collector::ptr const& collector)
            : hook(collector->make_hook(handler))
            , validatedLedgerAge(
                  collector->make_gauge("LedgerMaster", "Validated_Ledger_Age"))
            , publishedLedgerAge(
                  collector->make_gauge("LedgerMaster", "Published_Ledger_Age"))
        {
        }

        beast::insight::Hook hook;
        beast::insight::Gauge validatedLedgerAge;
        beast::insight::Gauge publishedLedgerAge;
    };

    Stats m_stats;

private:
    void
    collect_metrics()
    {
        std::lock_guard lock(m_mutex);
        m_stats.validatedLedgerAge.set(getValidatedLedgerAge().count());
        m_stats.publishedLedgerAge.set(getPublishedLedgerAge().count());
    }
};

}  // namespace ripple

#endif
