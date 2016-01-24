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

#include <ripple/app/main/Application.h>
#include <ripple/app/ledger/Ledger.h>
#include <ripple/app/ledger/LedgerCleaner.h>
#include <ripple/app/ledger/LedgerHistory.h>
#include <ripple/app/ledger/LedgerHolder.h>
#include <ripple/app/misc/CanonicalTXSet.h>
#include <ripple/basics/chrono.h>
#include <ripple/basics/RangeSet.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/protocol/RippleLedgerHash.h>
#include <ripple/protocol/STValidation.h>
#include <beast/insight/Collector.h>
#include <beast/module/core/threads/ScopedLock.h>
#include <beast/threads/Stoppable.h>
#include <beast/utility/PropertyStream.h>
#include <mutex>

#include "ripple.pb.h"

namespace ripple {

class Peer;
class Transaction;

struct LedgerReplay
{
    std::map< int, std::shared_ptr<STTx const> > txns_;
    NetClock::time_point closeTime_;
    int closeFlags_;
    Ledger::pointer prevLedger_;
};

// Tracks the current ledger and any ledgers in the process of closing
// Tracks ledger history
// Tracks held transactions

// VFALCO TODO Rename to Ledgers
//        It sounds like this holds all the ledgers...
//
class LedgerMaster
    : public beast::Stoppable
{
public:
    explicit
    LedgerMaster(Application& app, Stopwatch& stopwatch,
        Stoppable& parent,
            beast::insight::Collector::ptr const& collector,
                beast::Journal journal);

public:
    using callback = std::function <void (Ledger::ref)>;

public:
    virtual ~LedgerMaster () = default;

    LedgerIndex getCurrentLedgerIndex ();
    LedgerIndex getValidLedgerIndex ();

    bool isCompatible (Ledger::pointer,
        beast::Journal::Stream, const char* reason);

    std::recursive_mutex& peekMutex ();

    // The current ledger is the ledger we believe new transactions should go in
    std::shared_ptr<ReadView const> getCurrentLedger ();

    // The finalized ledger is the last closed/accepted ledger
    Ledger::pointer getClosedLedger ();

    // The validated ledger is the last fully validated ledger
    Ledger::pointer getValidatedLedger ();

    // The Rules are in the last fully validated ledger if there is one.
    Rules getValidatedRules();

    // This is the last ledger we published to clients and can lag the validated
    // ledger
    Ledger::pointer getPublishedLedger ();

    bool isValidLedger(LedgerInfo const&);

    std::chrono::seconds getPublishedLedgerAge ();
    std::chrono::seconds getValidatedLedgerAge ();
    bool isCaughtUp(std::string& reason);

    int getMinValidations ();

    void setMinValidations (int v, bool strict);

    std::uint32_t getEarliestFetch ();

    bool storeLedger (Ledger::pointer);
    void forceValid (Ledger::pointer);

    void setFullLedger (
        Ledger::pointer ledger, bool isSynchronous, bool isCurrent);

    void switchLCL (Ledger::pointer lastClosed);

    void failedSave(std::uint32_t seq, uint256 const& hash);

    std::string getCompleteLedgers ();

    /** Apply held transactions to the open ledger
        This is normally called as we close the ledger.
        The open ledger remains open to handle new transactions
        until a new open ledger is built.
    */
    void applyHeldTransactions ();

    /** Get all the transactions held for a particular account.
        This is normally called when a transaction for that
        account is successfully applied to the open
        ledger so those transactions can be resubmitted without
        waiting for ledger close.
    */
    std::vector<std::shared_ptr<STTx const>>
    pruneHeldTransactions(AccountID const& account,
        std::uint32_t const seq);

    /** Get a ledger's hash by sequence number using the cache
    */
    uint256 getHashBySeq (std::uint32_t index);

    /** Walk to a ledger's hash using the skip list
    */
    boost::optional<LedgerHash> walkHashBySeq (std::uint32_t index);
    /** Walk the chain of ledger hashes to determine the hash of the
        ledger with the specified index. The referenceLedger is used as
        the base of the chain and should be fully validated and must not
        precede the target index. This function may throw if nodes
        from the reference ledger or any prior ledger are not present
        in the node store.
    */
    boost::optional<LedgerHash> walkHashBySeq (
        std::uint32_t index, Ledger::ref referenceLedger);

    Ledger::pointer getLedgerBySeq (std::uint32_t index);

    Ledger::pointer getLedgerByHash (uint256 const& hash);

    void setLedgerRangePresent (
        std::uint32_t minV, std::uint32_t maxV);

    boost::optional<LedgerHash> getLedgerHash(
        std::uint32_t desiredSeq, Ledger::ref knownGoodLedger);

    boost::optional <NetClock::time_point> getCloseTimeBySeq (
        LedgerIndex ledgerIndex);

    boost::optional <NetClock::time_point> getCloseTimeByHash (
        LedgerHash const& ledgerHash);

    void addHeldTransaction (std::shared_ptr<Transaction> const& trans);
    void fixMismatch (Ledger::ref ledger);

    bool haveLedger (std::uint32_t seq);
    void clearLedger (std::uint32_t seq);
    bool getValidatedRange (
        std::uint32_t& minVal, std::uint32_t& maxVal);
    bool getFullValidatedRange (
        std::uint32_t& minVal, std::uint32_t& maxVal);

    void tune (int size, int age);
    void sweep ();
    float getCacheHitRate ();

    void checkAccept (Ledger::ref ledger);
    void checkAccept (uint256 const& hash, std::uint32_t seq);
    void consensusBuilt (Ledger::ref ledger, Json::Value consensus);

    LedgerIndex getBuildingLedger ();
    void setBuildingLedger (LedgerIndex index);

    void tryAdvance ();
    void newPathRequest ();
    bool isNewPathRequest ();
    void newOrderBookDB ();

    bool fixIndex (
        LedgerIndex ledgerIndex, LedgerHash const& ledgerHash);
    void doLedgerCleaner(Json::Value const& parameters);

    beast::PropertyStream::Source& getPropertySource ();

    void clearPriorLedgers (LedgerIndex seq);

    void clearLedgerCachePrior (LedgerIndex seq);

    // ledger replay
    void takeReplay (std::unique_ptr<LedgerReplay> replay);
    std::unique_ptr<LedgerReplay> releaseReplay ();

    // Fetch Packs
    void gotFetchPack (
        bool progress,
        std::uint32_t seq);

    void addFetchPack (
        uint256 const& hash,
        std::shared_ptr<Blob>& data);

    bool getFetchPack (
        uint256 const& hash,
        Blob& data);

    void makeFetchPack (
        std::weak_ptr<Peer> const& wPeer,
        std::shared_ptr<protocol::TMGetObjectByHash> const& request,
        uint256 haveLedgerHash,
        std::uint32_t uUptime);

    std::size_t getFetchPackCacheSize () const;

private:
    void setValidLedger(Ledger::ref l);
    void setPubLedger(Ledger::ref l);
    void tryFill(Job& job, Ledger::pointer ledger);
    void getFetchPack(LedgerHash missingHash, LedgerIndex missingIndex);
    boost::optional<LedgerHash> getLedgerHashForHistory(LedgerIndex index);
    int getNeededValidations();
    void advanceThread();
    // Try to publish ledgers, acquire missing ledgers
    void doAdvance();
    bool shouldFetchPack(std::uint32_t seq) const;
    bool shouldAcquire(
        std::uint32_t const currentLedger,
        std::uint32_t const ledgerHistory,
        std::uint32_t const ledgerHistoryIndex,
        std::uint32_t const candidateLedger) const;
    std::vector<Ledger::pointer> findNewLedgersToPublish();
    void updatePaths(Job& job);
    void newPFWork(const char *name);

private:
    using ScopedLockType = std::lock_guard <std::recursive_mutex>;
    using ScopedUnlockType = beast::GenericScopedUnlock <std::recursive_mutex>;

    Application& app_;
    beast::Journal m_journal;

    std::recursive_mutex mutable m_mutex;

    // The ledger that most recently closed.
    LedgerHolder mClosedLedger;

    // The highest-sequence ledger we have fully accepted.
    LedgerHolder mValidLedger;

    // The last ledger we have published.
    Ledger::pointer mPubLedger;

    // The last ledger we did pathfinding against.
    Ledger::pointer mPathLedger;

    // The last ledger we handled fetching history
    Ledger::pointer mHistLedger;

    // Fully validated ledger, whether or not we have the ledger resident.
    std::pair <uint256, LedgerIndex> mLastValidLedger;

    LedgerHistory mLedgerHistory;

    CanonicalTXSet mHeldTransactions;

    // A set of transactions to replay during the next close
    std::unique_ptr<LedgerReplay> replayData;

    std::recursive_mutex mCompleteLock;
    RangeSet mCompleteLedgers;

    std::unique_ptr <detail::LedgerCleaner> mLedgerCleaner;

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

};

} // ripple

#endif
