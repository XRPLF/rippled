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

#ifndef RIPPLE_APP_MISC_SHAMAPSTOREIMP_H_INCLUDED
#define RIPPLE_APP_MISC_SHAMAPSTOREIMP_H_INCLUDED

#include <ripple/app/misc/SHAMapStore.h>
#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/core/DatabaseCon.h>
#include <ripple/nodestore/DatabaseRotating.h>
#include <condition_variable>
#include <thread>

namespace ripple {

class NetworkOPs;

class SHAMapStoreImp : public SHAMapStore
{
private:
    struct SavedState
    {
        std::string writableDb;
        std::string archiveDb;
        LedgerIndex lastRotated;
    };

    enum Health : std::uint8_t
    {
        ok = 0,
        stopping,
        unhealthy
    };

    class SavedStateDB
    {
    public:
        soci::session session_;
        std::mutex mutex_;
        beast::Journal journal_;

        // Just instantiate without any logic in case online delete is not
        // configured
        explicit SavedStateDB()
        : journal_ {beast::Journal::getNullSink()}
        { }

        // opens database and, if necessary, creates & initializes its tables.
        void init (BasicConfig const& config, std::string const& dbName);
        // get/set the ledger index that we can delete up to and including
        LedgerIndex getCanDelete();
        LedgerIndex setCanDelete (LedgerIndex canDelete);
        SavedState getState();
        void setState (SavedState const& state);
        void setLastRotated (LedgerIndex seq);
    };

    Application& app_;

    // name of state database
    std::string const dbName_ = "state";
    // prefix of on-disk nodestore backend instances
    std::string const dbPrefix_ = "rippledb";
    // check health/stop status as records are copied
    std::uint64_t const checkHealthInterval_ = 1000;
    // minimum # of ledgers to maintain for health of network
    static std::uint32_t const minimumDeletionInterval_ = 256;
    // minimum # of ledgers required for standalone mode.
    static std::uint32_t const minimumDeletionIntervalSA_ = 8;

    Setup setup_;
    NodeStore::Scheduler& scheduler_;
    beast::Journal journal_;
    beast::Journal nodeStoreJournal_;
    NodeStore::DatabaseRotating* dbRotating_ = nullptr;
    SavedStateDB state_db_;
    std::thread thread_;
    bool stop_ = false;
    bool healthy_ = true;
    mutable std::condition_variable cond_;
    mutable std::condition_variable rendezvous_;
    mutable std::mutex mutex_;
    std::shared_ptr<Ledger const> newLedger_;
    std::atomic<bool> working_;
    TransactionMaster& transactionMaster_;
    std::atomic <LedgerIndex> canDelete_;
    // these do not exist upon SHAMapStore creation, but do exist
    // as of onPrepare() or before
    NetworkOPs* netOPs_ = nullptr;
    LedgerMaster* ledgerMaster_ = nullptr;
    FullBelowCache* fullBelowCache_ = nullptr;
    TreeNodeCache* treeNodeCache_ = nullptr;
    DatabaseCon* transactionDb_ = nullptr;
    DatabaseCon* ledgerDb_ = nullptr;
    int fdlimit_ = 0;

public:
    SHAMapStoreImp (Application& app,
            Setup const& setup,
            Stoppable& parent,
            NodeStore::Scheduler& scheduler,
            beast::Journal journal,
            beast::Journal nodeStoreJournal,
            TransactionMaster& transactionMaster,
            BasicConfig const& config);

    ~SHAMapStoreImp()
    {
        if (thread_.joinable())
            thread_.join();
    }

    std::uint32_t
    clampFetchDepth (std::uint32_t fetch_depth) const override
    {
        return setup_.deleteInterval ? std::min (fetch_depth,
                setup_.deleteInterval) : fetch_depth;
    }

    std::unique_ptr <NodeStore::Database> makeDatabase (
            std::string const&name,
            std::int32_t readThreads, Stoppable& parent) override;

    std::unique_ptr <NodeStore::DatabaseShard>
    makeDatabaseShard(std::string const& name,
        std::int32_t readThreads, Stoppable& parent) override;

    LedgerIndex
    setCanDelete (LedgerIndex seq) override
    {
        if (setup_.advisoryDelete)
            canDelete_ = seq;
        return state_db_.setCanDelete (seq);
    }

    bool
    advisoryDelete() const override
    {
        return setup_.advisoryDelete;
    }

    // All ledgers prior to this one are eligible
    // for deletion in the next rotation
    LedgerIndex
    getLastRotated() override
    {
        return state_db_.getState().lastRotated;
    }

    // All ledgers before and including this are unprotected
    // and online delete may delete them if appropriate
    LedgerIndex
    getCanDelete() override
    {
        return canDelete_;
    }

    void onLedgerClosed (std::shared_ptr<Ledger const> const& ledger) override;

    void rendezvous() const override;
    int fdlimit() const override;

private:
    // callback for visitNodes
    bool copyNode (std::uint64_t& nodeCount, SHAMapAbstractNode const &node);
    void run();
    void dbPaths();

    std::unique_ptr<NodeStore::Backend>
    makeBackendRotating (std::string path = std::string());

    template <class CacheInstance>
    bool
    freshenCache (CacheInstance& cache)
    {
        std::uint64_t check = 0;

        for (auto const& key: cache.getKeys())
        {
            dbRotating_->fetch(key, 0);
            if (! (++check % checkHealthInterval_) && health())
                return true;
        }

        return false;
    }

    /** delete from sqlite table in batches to not lock the db excessively
     *  pause briefly to extend access time to other users
     *  call with mutex object unlocked
     *  @return true if any deletable rows were found (though not
     *      necessarily deleted.
     */
    bool clearSql (DatabaseCon& database, LedgerIndex lastRotated,
                   std::string const& minQuery, std::string const& deleteQuery);
    void clearCaches (LedgerIndex validatedSeq);
    void freshenCaches();
    void clearPrior (LedgerIndex lastRotated);

    // If rippled is not healthy, defer rotate-delete.
    // If already unhealthy, do not change state on further check.
    // Assume that, once unhealthy, a necessary step has been
    // aborted, so the online-delete process needs to restart
    // at next ledger.
    Health health();
    //
    // Stoppable
    //
    void
    onPrepare() override
    {
    }

    void
    onStart() override
    {
        if (setup_.deleteInterval)
            thread_ = std::thread (&SHAMapStoreImp::run, this);
    }

    // Called when the application begins shutdown
    void onStop() override;
    // Called when all child Stoppable objects have stoped
    void onChildrenStopped() override;

};

}

#endif
