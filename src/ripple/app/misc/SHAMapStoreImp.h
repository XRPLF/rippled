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

    // keep this all within SHAMapStore, with forwarding functions for
    // RPC advisory_delete & OnlineDelete, with public interface.
    // There *should* be no reason to expose this class outside of SHAMapStore
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
        void
        init (std::string const& databasePath,
                std::string const& dbName)
        {
            boost::filesystem::path pathName = databasePath;
            pathName /= dbName;

            std::lock_guard <std::mutex> l (mutex_);

            beast::Error error (session_.open (pathName.native()));
            checkError (error);

            session_.once (error) << "PRAGMA synchronous=FULL;";
            checkError (error);

            session_.once (error) <<
                    "CREATE TABLE IF NOT EXISTS DbState ("
                    "  Key                    INTEGER PRIMARY KEY,"
                    "  WritableDb             TEXT,"
                    "  ArchiveDb              TEXT,"
                    "  LastDeletedLedger      INTEGER"
                    ");"
                    ;
            checkError (error);

            session_.once (error) <<
                    "CREATE TABLE IF NOT EXISTS CanDelete ("
                    "  Key                    INTEGER PRIMARY KEY,"
                    "  CanDeleteSeq           INTEGER"
                    ");"
                    ;

            std::int64_t count = 0;
            beast::sqdb::statement st = (session_.prepare <<
                    "SELECT COUNT(Key) FROM DbState WHERE Key = 1;"
                    , beast::sqdb::into (count)
                    );
            st.execute_and_fetch (error);
            checkError (error);

            if (!count)
            {
                session_.once (error) <<
                        "INSERT INTO DbState VALUES (1, '', '', 0);";
                checkError (error);
            }

            st = (session_.prepare <<
                    "SELECT COUNT(Key) FROM CanDelete WHERE Key = 1;"
                    , beast::sqdb::into (count)
                    );
            st.execute_and_fetch (error);
            checkError (error);

            if (!count)
            {
                session_.once (error) <<
                        "INSERT INTO CanDelete VALUES (1, 0);";
                checkError (error);
            }
        }

        // get/set the ledger index that we can delete up to and including
        LedgerIndex
        getCanDelete()
        {
            beast::Error error;
            LedgerIndex seq;

            {
                std::lock_guard <std::mutex> l (mutex_);

                session_.once (error) <<
                        "SELECT CanDeleteSeq FROM CanDelete WHERE Key = 1;"
                        , beast::sqdb::into (seq);
                ;
            }
            checkError (error);

            return seq;
        }

        void
        setCanDelete (LedgerIndex canDelete)
        {
            beast::Error error;
            {
                std::lock_guard <std::mutex> l (mutex_);

                session_.once (error) <<
                        "UPDATE CanDelete SET CanDeleteSeq = ? WHERE Key = 1;"
                        , beast::sqdb::use (canDelete)
                        ;
            }
            checkError (error);
        }

        SavedState
        getState()
        {
            beast::Error error;
            SavedState state;

            {
                std::lock_guard <std::mutex> l (mutex_);

                session_.once (error) <<
                        "SELECT WritableDb, ArchiveDb, LastDeletedLedger"
                        " FROM DbState WHERE Key = 1;"
                        , beast::sqdb::into (state.writableDb)
                        , beast::sqdb::into (state.archiveDb)
                        , beast::sqdb::into (state.lastRotated)
                        ;
            }
            checkError (error);

            return state;
        }

        void
        setState (SavedState const& state)
        {
            beast::Error error;

            {
                std::lock_guard <std::mutex> l (mutex_);
                session_.once (error) <<
                        "UPDATE DbState"
                        " SET WritableDb = ?,"
                        " ArchiveDb = ?,"
                        " LastDeletedLedger = ?"
                        " WHERE Key = 1;"
                        , beast::sqdb::use (state.writableDb)
                        , beast::sqdb::use (state.archiveDb)
                        , beast::sqdb::use (state.lastRotated)
                        ;
            }
            checkError (error);
        }

        void
        setLastRotated (LedgerIndex seq)
        {
            beast::Error error;
            {
                std::lock_guard <std::mutex> l (mutex_);
                session_.once (error) <<
                        "UPDATE DbState SET LastDeletedLedger = ?"
                        " WHERE Key = 1;"
                        , beast::sqdb::use (seq)
                        ;
            }
            checkError (error);
        }

        void
        setBackends (std::string const& writableBackend,
                std::string const& archiveBackend)
        {
            beast::Error error;
            {
                std::lock_guard <std::mutex> l (mutex_);
                session_.once (error) <<
                        "UPDATE DbState SET WritableDb = ?, ArchiveDb = ?"
                        " WHERE Key=1;"
                        , beast::sqdb::use (writableBackend)
                        , beast::sqdb::use (archiveBackend)
                        ;
            }
            checkError (error);
        }

        void
        checkError (beast::Error const& error)
        {
            if (error)
            {
                journal_.fatal << "state database error: " << error.code()
                        << ": " << error.getReasonText();
                throw std::runtime_error ("State database error.");
            }
        }
    };

    Setup setup_;
    NodeStore::Manager& manager_;
    NodeStore::Scheduler& scheduler_;
    beast::Journal journal_;
    beast::Journal nodeStoreJournal_;
    NodeStore::DatabaseRotating* database_;
    SavedStateDB state_db_;
    std::thread thread_;
    bool stop_;
    mutable std::condition_variable cond_;
    mutable std::mutex mutex_;
    std::string const dbName_ = "state.db";
    std::string const dbPrefix_ = "rippledb";
    Ledger::pointer validatedLedger_;
    std::uint64_t const checkStopInterval_ = 1000;

public:
    SHAMapStoreImp (Setup const& setup,
            Stoppable& parent,
            NodeStore::Manager& manager,
            NodeStore::Scheduler& scheduler,
            beast::Journal journal,
            beast::Journal nodeStoreJournal)
        : SHAMapStore (parent)
        , setup_ (setup)
        , manager_ (manager)
        , scheduler_ (scheduler)
        , journal_ (journal)
        , nodeStoreJournal_ (nodeStoreJournal)
        , database_ (nullptr)
        , stop_ (false)
    {
        if (setup_.deleteInterval)
        {
            if (setup_.ledgerHistory > setup_.deleteInterval)
                throw std::runtime_error (
                        "LEDGER_HISTORY exceeds online_delete.");

            state_db_.init (setup_.databasePath, dbName_);

            dbPaths();
        }
    }

    ~SHAMapStoreImp()
    {
        if (thread_.joinable())
            thread_.join();
    }

    std::uint32_t
    clampFetchDepth (std::uint32_t fetch_depth) const override
    {
        if (setup_.deleteInterval && fetch_depth > setup_.deleteInterval)
            fetch_depth = setup_.deleteInterval;

        return fetch_depth;
    }

    std::unique_ptr <NodeStore::Database>
    makeDatabase (std::string const& name,
            std::int32_t readThreads) override
    {
        std::unique_ptr <NodeStore::Database> db;

        if (setup_.deleteInterval)
        {
            SavedState state = state_db_.getState();

            std::shared_ptr <NodeStore::Backend> writableBackend (
                    makeBackendRotating (state.writableDb));
            std::shared_ptr <NodeStore::Backend> archiveBackend (
                    makeBackendRotating (state.archiveDb));
            std::unique_ptr <NodeStore::DatabaseRotating> dbr =
                    makeDatabaseRotating (name, readThreads, writableBackend,
                    archiveBackend);

            if (!state.writableDb.size())
            {
                state.writableDb = writableBackend->getName();
                state.archiveDb = archiveBackend->getName();
                state_db_.setState (state);
            }

            database_ = dbr.get();
            db.reset (dynamic_cast <NodeStore::Database*>(dbr.release()));
        }
        else
        {
            db = manager_.make_Database (name, scheduler_, nodeStoreJournal_,
                    readThreads, setup_.nodeDatabase,
                    setup_.ephemeralNodeDatabase);
        }

        return db;
    }

    LedgerIndex
    setCanDelete (LedgerIndex seq) override
    {
        state_db_.setCanDelete (seq);

        return seq;
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

    void
    onLedgerClosed (Ledger::pointer validatedLedger) override
    {
        {
            std::lock_guard <std::mutex> l (mutex_);
            validatedLedger_ = validatedLedger;
        }
        cond_.notify_one();
    }

private:
    // callback for visitNodes
    bool
    copyNode (std::uint64_t& nodeCount,
            SHAMapTreeNode const& node) const
    {
        bool ret = false;
        // Copy a single record from node to database_
        database_->fetchNode (node.getNodeHash());
        if (! (++nodeCount % checkStopInterval_))
        {
            std::lock_guard <std::mutex> l (mutex_);
            if (stop_)
            {
                ret = true;
            }
        }

        return ret;
    }

    void
    run()
    {
        LedgerIndex lastRotated = state_db_.getState().lastRotated;

        while (1)
        {
            Ledger::pointer validatedLedger;

            std::unique_lock <std::mutex> l (mutex_);
            if (stop_)
            {
                stopped();
                return;
            }
            cond_.wait (l);
            if (validatedLedger_.get())
                validatedLedger = validatedLedger_;
            else
                continue;
            validatedLedger_.reset();
            l.unlock();

            LedgerIndex validatedSeq = validatedLedger->getLedgerSeq();
            if (!lastRotated)
            {
                lastRotated = validatedSeq;
                state_db_.setLastRotated (lastRotated);
            }
            LedgerIndex canDelete =
                    std::numeric_limits <LedgerIndex>::max();
            if (setup_.advisoryDelete)
                canDelete = state_db_.getCanDelete();

            if (validatedSeq >= lastRotated + setup_.deleteInterval
                    && canDelete >= lastRotated-1)
            {
                journal_.debug << "rotating  validatedSeq " << validatedSeq
                        << " lastRotated " << lastRotated << " deleteInterval "
                        << setup_.deleteInterval << " canDelete " << canDelete;

                // will delete up to (not including) lastRotated)
                clearPrior (lastRotated);
                if (checkStop())
                    return;

                lastRotated = validatedSeq;
                state_db_.setLastRotated (lastRotated);

                std::uint64_t nodeCount = 0;
                validatedLedger->peekAccountStateMap()->snapShot (
                        false)->visitNodes (
                        std::bind (&SHAMapStoreImp::copyNode, this,
                        std::ref(nodeCount), std::placeholders::_1));
                journal_.debug << "copied ledger " << validatedSeq
                        << " nodecount " << nodeCount;
                if (checkStop())
                    return;

                freshenCaches();
                journal_.debug << validatedSeq << " freshened caches";
                if (checkStop())
                    return;

                std::shared_ptr <NodeStore::Backend> newBackend =
                        makeBackendRotating();
                journal_.debug << validatedSeq << " new backend "
                        << newBackend->getName();
                std::shared_ptr <NodeStore::Backend> oldBackend;

                clearCaches (validatedSeq);
                if (checkStop())
                    return;

                {
                    std::unique_lock <std::mutex> l =
                            database_->getRotateLock();

                    clearCaches (validatedSeq);

                    oldBackend = database_->rotateBackends (newBackend);

                    state_db_.setBackends (
                            database_->getWritableBackend (true)->getName(),
                            database_->getArchiveBackend (true)->getName());
                }
                journal_.debug << "finished rotation " << validatedSeq;
                if (checkStop())
                    return;

                // it's either this or go into each backend, set a flag
                // then modify destructor
                // it's OK to stop before deleting the old backend path because
                // it will get deleted on restart
                boost::filesystem::path oldBackendPath = oldBackend->getName();
                while (oldBackend.use_count() > 1)
                {
                    journal_.debug << validatedSeq
                            << "while oldBackend.use_count "
                            << oldBackend.use_count();
                    if (checkStop())
                        return;
                    std::this_thread::sleep_for (std::chrono::seconds (1));
                }
                oldBackend.reset();
                boost::filesystem::remove_all (oldBackendPath);

                journal_.debug << validatedSeq << " backends: "
                        << database_->getWritableBackend()->getName() << ","
                        << database_->getArchiveBackend()->getName() << ";"
                        << " statedb: " << state_db_.getState().writableDb
                        << "," << state_db_.getState().archiveDb;

            journal_.debug << "end rotating " << validatedSeq;
            }
        }
    }

    void
    dbPaths()
    {
        boost::filesystem::path dbPath =
                setup_.nodeDatabase["path"].toStdString();

        if (boost::filesystem::exists (dbPath))
        {
            if (! boost::filesystem::is_directory (dbPath))
            {
                std::cerr << "node db path must be a directory. "
                        << dbPath.native();
                throw std::runtime_error (
                        "node db path must be a directory.");
            }
        }
        else
        {
            boost::filesystem::create_directories (dbPath);
        }

        SavedState state = state_db_.getState();
        bool writableDbExists = false;
        bool archiveDbExists = false;

        for (boost::filesystem::directory_iterator it (dbPath);
                it != boost::filesystem::directory_iterator(); ++it)
        {
            if (! state.writableDb.compare (it->path().native()))
            {
                writableDbExists = true;
            }
            else if (! state.archiveDb.compare (it->path().native()))
            {
                archiveDbExists = true;
            }
            else if (! dbPrefix_.compare (it->path().stem().native()))
            {
                boost::filesystem::remove_all (it->path());
            }
        }

        if ((!writableDbExists && state.writableDb.size()) ||
                (!archiveDbExists && state.archiveDb.size()) ||
                (writableDbExists != archiveDbExists) ||
                state.writableDb.empty() != state.archiveDb.empty())
        {
            boost::filesystem::path stateDbPathName = setup_.databasePath;
            stateDbPathName /= dbName_;
            stateDbPathName += "*";

            std::cerr << "state db error: " << std::endl
                    << "  writableDbExists " << writableDbExists
                    << " archiveDbExists " << archiveDbExists << std::endl
                    << "  writableDb '" << state.writableDb
                    << "' archiveDb '" << state.archiveDb << "'"
                    << std::endl << std::endl
                    << "To resume operation, make backups of and "
                    << "remove the files matching "
                    << stateDbPathName.native()
                    << " and contents of the directory "
                    << setup_.nodeDatabase["path"].toStdString()
                    << std::endl;

            throw std::runtime_error ("state db error");
        }
    }

    std::shared_ptr <NodeStore::Backend>
    makeBackendRotating (std::string path = std::string())
    {
        boost::filesystem::path newPath;
        NodeStore::Parameters parameters = setup_.nodeDatabase;

        if (path.size())
        {
            newPath = path;
        }
        else
        {
            boost::filesystem::path p = parameters["path"].toStdString();
            p /= dbPrefix_;
            p += ".%%%%";
            newPath = boost::filesystem::unique_path (p);
        }
        parameters.set("path", newPath.native());

        return manager_.make_Backend (parameters, scheduler_,
                nodeStoreJournal_);
    }

    /**
     * Creates a NodeStore with two
     * backends to allow online deletion of data.
     *
     * @param name A diagnostic label for the database.
     * @param scheduler The scheduler to use for performing asynchronous tasks
     * @param journal The logging object
     * @param readThreads The number of async read threads to create
     * @param writableBackend backend for writing
     * @param archiveBackend backend for archiving
     * @param fastBackendParameters [optional] The parameter string for the ephemeral backend.
     *
     * @return The opened database.
     */
    std::unique_ptr <NodeStore::DatabaseRotating>
    makeDatabaseRotating (std::string const& name,
            std::int32_t readThreads,
            std::shared_ptr <NodeStore::Backend> writableBackend,
            std::shared_ptr <NodeStore::Backend> archiveBackend) const
    {
        std::unique_ptr <NodeStore::Backend> fastBackend (
            (setup_.ephemeralNodeDatabase.size() > 0)
                ? manager_.make_Backend (setup_.ephemeralNodeDatabase,
                scheduler_, journal_) : nullptr);

        return manager_.make_DatabaseRotating ("NodeStore.main", scheduler_, 4,
                writableBackend, archiveBackend, std::move (fastBackend),
                nodeStoreJournal_);
    }

    bool
    checkStop()
    {
        bool r = false;

        std::lock_guard <std::mutex> l (mutex_);
        if (stop_)
        {
            stopped();
            r = true;
        }

        return r;
    }

    template <class CacheInstance>
    bool
    freshenCache (CacheInstance& cache)
    {
        std::uint64_t check = 0;

        for (uint256 it: cache.getKeys())
        {
            database_->fetchNode (it);
            if ((! ++check % checkStopInterval_) && stop_)
                return true;
        }

        return false;
    }

    /** delete from sqlite table in batches to not lock the db excessively
     *  pause briefly to extend access time to other users
     *  call with mutex object unlocked
     */
    void
    clearSql (Database* db,
            std::unique_lock <std::recursive_mutex> lock,
            LedgerIndex lastRotated,
            std::string minQuery,
            std::string deleteQuery)
    {
        LedgerIndex min = std::numeric_limits <LedgerIndex>::max();
        lock.lock();
        if (!db->executeSQL (minQuery) || !db->startIterRows())
            return;
        min = db->getBigInt (0);
        db->endIterRows ();
        lock.unlock();

        std::uint32_t const batchSize = 5000;
        std::uint32_t pause = 1000;
        boost::format formattedDeleteQuery (deleteQuery);

        journal_.debug << "start: " << deleteQuery << " from "
                << min << " to " << lastRotated;
        while (min < lastRotated)
        {
            min = (min + batchSize >= lastRotated) ? lastRotated :
                min + batchSize;
            lock.lock();
            db->executeSQL (boost::str (formattedDeleteQuery % min));
            lock.unlock();
            if (stop_)
                return;
            std::this_thread::sleep_for (std::chrono::microseconds (pause));
        }
        journal_.debug << "finished: " << deleteQuery;
    }

    /** execute SQLite incremental vacuum */
    void
    incrementalVacuum (Database* db,
            std::unique_lock <std::recursive_mutex> lock,
            std::string const& dbName)
    {
        journal_.debug << "start incremental_vacuum " << dbName;
        db->executeSQL ("PRAGMA incremental_vacuum;");
        journal_.debug << "finished incremental vacuum" << dbName;
    }

    void
    clearCaches (LedgerIndex validatedSeq)
    {
        getApp().getLedgerMaster().clearLedgerCachePrior (validatedSeq);
        getApp().getFullBelowCache().clear();
    }

    void
    freshenCaches()
    {
        if (freshenCache (database_->getPositiveCache()))
            return;
        if (freshenCache (getApp().getTreeNodeCache()))
            return;
        if (freshenCache (getApp().getTempNodeCache()))
            return;
        if (freshenCache (getApp().getSLECache()))
            return;
        if (freshenCache (getApp().getMasterTransaction().getCache()))
            return;
    }

    void
    clearPrior (LedgerIndex lastRotated)
    {
        getApp().getLedgerMaster().clearPriorLedgers (lastRotated);
        if (stop_)
            return;

        clearSql (getApp().getLedgerDB().getDB(),
            getApp().getLedgerDB().lock (true), lastRotated,
            "SELECT MIN(LedgerSeq) FROM Ledgers;",
            "DELETE FROM Validations WHERE Ledgers.LedgerSeq < %u"
            " AND Validations.LedgerHash = Ledgers.LedgerHash;");
        if (stop_)
            return;

        clearSql (getApp().getLedgerDB().getDB(),
            getApp().getLedgerDB().lock (true), lastRotated,
            "SELECT MIN(LedgerSeq) FROM Ledgers;",
            "DELETE FROM Ledgers WHERE LedgerSeq < %u;");
        if (stop_)
            return;

        incrementalVacuum (getApp().getLedgerDB().getDB(),
            getApp().getLedgerDB().lock(), "ledgerdb");
        if (stop_)
            return;

        clearSql (getApp().getTxnDB().getDB(),
            getApp().getTxnDB().lock (true), lastRotated,
            "SELECT MIN(LedgerSeq) FROM Transactions;",
            "DELETE FROM Transactions WHERE LedgerSeq < %u;");
        if (stop_)
            return;

        clearSql (getApp().getTxnDB().getDB(),
            getApp().getTxnDB().lock (true), lastRotated,
            "SELECT MIN(LedgerSeq) FROM AccountTransactions;",
            "DELETE FROM AccountTransactions WHERE LedgerSeq < %u;");
        if (stop_)
            return;

        incrementalVacuum (getApp().getTxnDB().getDB(),
            getApp().getTxnDB().lock(), "transactiondb");
    }

    //
    // Stoppable
    //
    void
    onPrepare() override
    {
    }

    // Called when all stoppables are known to exist
    void
    onStart() override
    {
        if (setup_.deleteInterval)
            thread_ = std::thread (&SHAMapStoreImp::run, this);
    }

    // Called when the application begins shutdown
    void
    onStop() override
    {
        if (setup_.deleteInterval)
        {
            {
                std::lock_guard <std::mutex> l (mutex_);
                stop_ = true;
            }
            cond_.notify_one();
        }
        else
        {
            stopped();
        }
    }

    // Called when all child Stoppable objects have stoped
    void
    onChildrenStopped() override
    {
        if (setup_.deleteInterval)
        {
            {
                std::lock_guard <std::mutex> l (mutex_);
                stop_ = true;
            }
            cond_.notify_one();
        }
        else
        {
            stopped();
        }
    }

};

}

#endif
