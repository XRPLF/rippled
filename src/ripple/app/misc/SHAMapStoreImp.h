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

#ifndef RIPPLE_APP_SHAMAPSTOREIMP_H_INCLUDED
#define RIPPLE_APP_SHAMAPSTOREIMP_H_INCLUDED

#include <ripple/app/misc/SHAMapStore.h>
#include <ripple/nodestore/impl/Tuning.h>
#include <ripple/nodestore/DatabaseRotating.h>
#include <beast/module/sqdb/sqdb.h>

#include <iostream>
#include <condition_variable>

#include "NetworkOPs.h"

namespace ripple {

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
        beast::sqdb::session session_;
        std::mutex mutex_;
        beast::Journal journal_;

        // Just instantiate without any logic in case online delete is not
        // configured
        SavedStateDB() = default;

        // opens SQLite database and, if necessary, creates & initializes its tables.
        void init (std::string const& databasePath, std::string const& dbName);
        // get/set the ledger index that we can delete up to and including
        LedgerIndex getCanDelete();
        LedgerIndex setCanDelete (LedgerIndex canDelete);
        SavedState getState();
        void setState (SavedState const& state);
        void setLastRotated (LedgerIndex seq);
        void checkError (beast::Error const& error);
    };

    // name of sqlite state database
    std::string const dbName_ = "state.db";
    // prefix of on-disk nodestore backend instances
    std::string const dbPrefix_ = "rippledb";
    // check health/stop status as records are copied
    std::uint64_t const checkHealthInterval_ = 1000;
    // microseconds to back off between sqlite deletion batches
    std::uint32_t pause_ = 1000;
    // seconds to compare against ledger age
    std::uint16_t ageTooHigh_ = 60;

    Setup setup_;
    NodeStore::Manager& manager_;
    NodeStore::Scheduler& scheduler_;
    beast::Journal journal_;
    beast::Journal nodeStoreJournal_;
    NodeStore::DatabaseRotating* database_;
    SavedStateDB state_db_;
    std::thread thread_;
    bool stop_ = false;
    bool healthy_ = true;
    mutable std::condition_variable cond_;
    mutable std::mutex mutex_;
    Ledger::pointer newLedger_;
    Ledger::pointer validatedLedger_;
    TransactionMaster& transactionMaster_;
    // these do not exist upon SHAMapStore creation, but do exist
    // as of onPrepare() or before
    NetworkOPs* netOPs_ = nullptr;
    LedgerMaster* ledgerMaster_ = nullptr;
    FullBelowCache* fullBelowCache_ = nullptr;
    TreeNodeCache* treeNodeCache_ = nullptr;
    DatabaseCon* transactionDb_ = nullptr;
    DatabaseCon* ledgerDb_ = nullptr;

public:
    SHAMapStoreImp (Setup const& setup,
            Stoppable& parent,
            NodeStore::Manager& manager,
            NodeStore::Scheduler& scheduler,
            beast::Journal journal,
            beast::Journal nodeStoreJournal,
            TransactionMaster& transactionMaster);

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
            std::string const&name, std::int32_t readThreads) override;

    LedgerIndex
    setCanDelete (LedgerIndex seq) override
    {
        return state_db_.setCanDelete (seq);
    }

    bool
    advisoryDelete() const override
    {
        return setup_.advisoryDelete;
    }

    LedgerIndex
    getLastRotated() override
    {
        return state_db_.getState().lastRotated;
    }

    LedgerIndex
    getCanDelete() override
    {
        return state_db_.getCanDelete();
    }

    void onLedgerClosed (Ledger::pointer validatedLedger) override;

private:
    // callback for visitNodes
    bool copyNode (std::uint64_t& nodeCount, SHAMapTreeNode const &node);
    void run();
    void dbPaths();
    std::shared_ptr <NodeStore::Backend> makeBackendRotating (
            std::string path = std::string());
    /**
     * Creates a NodeStore with two
     * backends to allow online deletion of data.
     *
     * @param name A diagnostic label for the database.
     * @param readThreads The number of async read threads to create
     * @param writableBackend backend for writing
     * @param archiveBackend backend for archiving
     *
     * @return The opened database.
     */
    std::unique_ptr <NodeStore::DatabaseRotating>
    makeDatabaseRotating (std::string const&name,
            std::int32_t readThreads,
            std::shared_ptr <NodeStore::Backend> writableBackend,
            std::shared_ptr <NodeStore::Backend> archiveBackend) const;

    template <class CacheInstance>
    bool
    freshenCache (CacheInstance& cache)
    {
        std::uint64_t check = 0;

        for (uint256 it: cache.getKeys())
        {
            database_->fetchNode (it);
            if (! (++check % checkHealthInterval_) && health())
                return true;
        }

        return false;
    }

    /** delete from sqlite table in batches to not lock the db excessively
     *  pause briefly to extend access time to other users
     *  call with mutex object unlocked
     */
    void clearSql (DatabaseCon& database, LedgerIndex lastRotated,
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
