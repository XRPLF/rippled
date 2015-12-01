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

#include <ripple/app/misc/SHAMapStoreImp.h>
#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/ledger/TransactionMaster.h>
#include <ripple/app/main/Application.h>
#include <ripple/basics/contract.h>
#include <ripple/core/ConfigSections.h>
#include <boost/format.hpp>
#include <boost/format.hpp>
#include <boost/optional.hpp>
#include <memory>

namespace ripple {
void SHAMapStoreImp::SavedStateDB::init (BasicConfig const& config,
                                         std::string const& dbName)
{
    std::lock_guard<std::mutex> lock (mutex_);

    open(session_, config, dbName);

    session_ << "PRAGMA synchronous=FULL;";

    session_ <<
        "CREATE TABLE IF NOT EXISTS DbState ("
        "  Key                    INTEGER PRIMARY KEY,"
        "  WritableDb             TEXT,"
        "  ArchiveDb              TEXT,"
        "  LastRotatedLedger      INTEGER"
        ");"
        ;

    session_ <<
        "CREATE TABLE IF NOT EXISTS CanDelete ("
        "  Key                    INTEGER PRIMARY KEY,"
        "  CanDeleteSeq           INTEGER"
        ");"
        ;

    std::int64_t count = 0;
    {
        boost::optional<std::int64_t> countO;
        session_ <<
                "SELECT COUNT(Key) FROM DbState WHERE Key = 1;"
                , soci::into (countO);
        if (!countO)
            Throw<std::runtime_error> ("Failed to fetch Key Count from DbState.");
        count = *countO;
    }

    if (!count)
    {
        session_ <<
                "INSERT INTO DbState VALUES (1, '', '', 0);";
    }


    {
        boost::optional<std::int64_t> countO;
        session_ <<
                "SELECT COUNT(Key) FROM CanDelete WHERE Key = 1;"
                , soci::into (countO);
        if (!countO)
            Throw<std::runtime_error> ("Failed to fetch Key Count from CanDelete.");
        count = *countO;
    }

    if (!count)
    {
        session_ <<
                "INSERT INTO CanDelete VALUES (1, 0);";
    }
}

LedgerIndex
SHAMapStoreImp::SavedStateDB::getCanDelete()
{
    LedgerIndex seq;
    std::lock_guard<std::mutex> lock (mutex_);

    session_ <<
            "SELECT CanDeleteSeq FROM CanDelete WHERE Key = 1;"
            , soci::into (seq);
    ;

    return seq;
}

LedgerIndex
SHAMapStoreImp::SavedStateDB::setCanDelete (LedgerIndex canDelete)
{
    std::lock_guard<std::mutex> lock (mutex_);

    session_ <<
            "UPDATE CanDelete SET CanDeleteSeq = :canDelete WHERE Key = 1;"
            , soci::use (canDelete)
            ;

    return canDelete;
}

SHAMapStoreImp::SavedState
SHAMapStoreImp::SavedStateDB::getState()
{
    SavedState state;

    std::lock_guard<std::mutex> lock (mutex_);

    session_ <<
            "SELECT WritableDb, ArchiveDb, LastRotatedLedger"
            " FROM DbState WHERE Key = 1;"
            , soci::into (state.writableDb), soci::into (state.archiveDb)
            , soci::into (state.lastRotated)
            ;

    return state;
}

void
SHAMapStoreImp::SavedStateDB::setState (SavedState const& state)
{
    std::lock_guard<std::mutex> lock (mutex_);
    session_ <<
            "UPDATE DbState"
            " SET WritableDb = :writableDb,"
            " ArchiveDb = :archiveDb,"
            " LastRotatedLedger = :lastRotated"
            " WHERE Key = 1;"
            , soci::use (state.writableDb)
            , soci::use (state.archiveDb)
            , soci::use (state.lastRotated)
            ;
}

void
SHAMapStoreImp::SavedStateDB::setLastRotated (LedgerIndex seq)
{
    std::lock_guard<std::mutex> lock (mutex_);
    session_ <<
            "UPDATE DbState SET LastRotatedLedger = :seq"
            " WHERE Key = 1;"
            , soci::use (seq)
            ;
}

//------------------------------------------------------------------------------

SHAMapStoreImp::SHAMapStoreImp (
        Application& app,
        Setup const& setup,
        Stoppable& parent,
        NodeStore::Scheduler& scheduler,
        beast::Journal journal,
        beast::Journal nodeStoreJournal,
        TransactionMaster& transactionMaster,
        BasicConfig const& config)
    : SHAMapStore (parent)
    , app_ (app)
    , setup_ (setup)
    , scheduler_ (scheduler)
    , journal_ (journal)
    , nodeStoreJournal_ (nodeStoreJournal)
    , transactionMaster_ (transactionMaster)
    , canDelete_ (std::numeric_limits <LedgerIndex>::max())
{
    if (setup_.deleteInterval)
    {
        if (setup_.deleteInterval < minimumDeletionInterval_)
        {
            Throw<std::runtime_error> ("online_delete must be at least " +
                std::to_string (minimumDeletionInterval_));
        }

        if (setup_.ledgerHistory > setup_.deleteInterval)
        {
            Throw<std::runtime_error> (
                "online_delete must be less than ledger_history (currently " +
                std::to_string (setup_.ledgerHistory) + ")");
        }

        state_db_.init (config, dbName_);

        dbPaths();
    }
}

std::unique_ptr <NodeStore::Database>
SHAMapStoreImp::makeDatabase (std::string const& name,
        std::int32_t readThreads)
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
        db = NodeStore::Manager::instance().make_Database (name, scheduler_, nodeStoreJournal_,
                readThreads, setup_.nodeDatabase);
    }

    return db;
}

void
SHAMapStoreImp::onLedgerClosed (Ledger::pointer validatedLedger)
{
    {
        std::lock_guard <std::mutex> lock (mutex_);
        newLedger_ = validatedLedger;
    }
    cond_.notify_one();
}

bool
SHAMapStoreImp::copyNode (std::uint64_t& nodeCount,
        SHAMapAbstractNode const& node)
{
    // Copy a single record from node to database_
    database_->fetchNode (node.getNodeHash().as_uint256());
    if (! (++nodeCount % checkHealthInterval_))
    {
        if (health())
            return true;
    }

    return false;
}

void
SHAMapStoreImp::run()
{
    LedgerIndex lastRotated = state_db_.getState().lastRotated;
    netOPs_ = &app_.getOPs();
    ledgerMaster_ = &app_.getLedgerMaster();
    fullBelowCache_ = &app_.family().fullbelow();
    treeNodeCache_ = &app_.family().treecache();
    transactionDb_ = &app_.getTxnDB();
    ledgerDb_ = &app_.getLedgerDB();

    if (setup_.advisoryDelete)
        canDelete_ = state_db_.getCanDelete ();

    while (1)
    {
        healthy_ = true;
        validatedLedger_.reset();

        {
            std::unique_lock <std::mutex> lock (mutex_);
            if (stop_)
            {
                stopped();
                return;
            }
            cond_.wait (lock);
            if (newLedger_)
                validatedLedger_ = std::move (newLedger_);
            else
                continue;
        }

        LedgerIndex validatedSeq = validatedLedger_->info().seq;
        if (!lastRotated)
        {
            lastRotated = validatedSeq;
            state_db_.setLastRotated (lastRotated);
        }

        // will delete up to (not including) lastRotated)
        if (validatedSeq >= lastRotated + setup_.deleteInterval
                && canDelete_ >= lastRotated - 1)
        {
            journal_.debug << "rotating  validatedSeq " << validatedSeq
                    << " lastRotated " << lastRotated << " deleteInterval "
                    << setup_.deleteInterval << " canDelete_ " << canDelete_;

            switch (health())
            {
                case Health::stopping:
                    stopped();
                    return;
                case Health::unhealthy:
                    continue;
                case Health::ok:
                default:
                    ;
            }

            clearPrior (lastRotated);
            switch (health())
            {
                case Health::stopping:
                    stopped();
                    return;
                case Health::unhealthy:
                    continue;
                case Health::ok:
                default:
                    ;
            }

            std::uint64_t nodeCount = 0;
            validatedLedger_->stateMap().snapShot (
                    false)->visitNodes (
                    std::bind (&SHAMapStoreImp::copyNode, this,
                    std::ref(nodeCount), std::placeholders::_1));
            journal_.debug << "copied ledger " << validatedSeq
                    << " nodecount " << nodeCount;
            switch (health())
            {
                case Health::stopping:
                    stopped();
                    return;
                case Health::unhealthy:
                    continue;
                case Health::ok:
                default:
                    ;
            }

            freshenCaches();
            journal_.debug << validatedSeq << " freshened caches";
            switch (health())
            {
                case Health::stopping:
                    stopped();
                    return;
                case Health::unhealthy:
                    continue;
                case Health::ok:
                default:
                    ;
            }

            std::shared_ptr <NodeStore::Backend> newBackend =
                    makeBackendRotating();
            journal_.debug << validatedSeq << " new backend "
                    << newBackend->getName();
            std::shared_ptr <NodeStore::Backend> oldBackend;

            clearCaches (validatedSeq);
            switch (health())
            {
                case Health::stopping:
                    stopped();
                    return;
                case Health::unhealthy:
                    continue;
                case Health::ok:
                default:
                    ;
            }

            std::string nextArchiveDir =
                    database_->getWritableBackend()->getName();
            lastRotated = validatedSeq;
            {
                std::lock_guard <std::mutex> lock (database_->peekMutex());

                state_db_.setState (SavedState {newBackend->getName(),
                        nextArchiveDir, lastRotated});
                clearCaches (validatedSeq);
                oldBackend = database_->rotateBackends (newBackend);
            }
            journal_.debug << "finished rotation " << validatedSeq;

            oldBackend->setDeletePath();
        }
    }
}

void
SHAMapStoreImp::dbPaths()
{
    boost::filesystem::path dbPath =
            get<std::string>(setup_.nodeDatabase, "path");

    if (boost::filesystem::exists (dbPath))
    {
        if (! boost::filesystem::is_directory (dbPath))
        {
            std::cerr << "node db path must be a directory. "
                    << dbPath.string();
            Throw<std::runtime_error> (
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
        if (! state.writableDb.compare (it->path().string()))
            writableDbExists = true;
        else if (! state.archiveDb.compare (it->path().string()))
            archiveDbExists = true;
        else if (! dbPrefix_.compare (it->path().stem().string()))
            boost::filesystem::remove_all (it->path());
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
                << stateDbPathName.string()
                << " and contents of the directory "
                << get<std::string>(setup_.nodeDatabase, "path")
                << std::endl;

        Throw<std::runtime_error> ("state db error");
    }
}

std::shared_ptr <NodeStore::Backend>
SHAMapStoreImp::makeBackendRotating (std::string path)
{
    boost::filesystem::path newPath;
    Section parameters = setup_.nodeDatabase;

    if (path.size())
    {
        newPath = path;
    }
    else
    {
        boost::filesystem::path p = get<std::string>(parameters, "path");
        p /= dbPrefix_;
        p += ".%%%%";
        newPath = boost::filesystem::unique_path (p);
    }
    parameters.set("path", newPath.string());

    return NodeStore::Manager::instance().make_Backend (parameters, scheduler_,
            nodeStoreJournal_);
}

std::unique_ptr <NodeStore::DatabaseRotating>
SHAMapStoreImp::makeDatabaseRotating (std::string const& name,
        std::int32_t readThreads,
        std::shared_ptr <NodeStore::Backend> writableBackend,
        std::shared_ptr <NodeStore::Backend> archiveBackend) const
{
    return NodeStore::Manager::instance().make_DatabaseRotating ("NodeStore.main", scheduler_,
            readThreads, writableBackend, archiveBackend, nodeStoreJournal_);
}

void
SHAMapStoreImp::clearSql (DatabaseCon& database,
        LedgerIndex lastRotated,
        std::string const& minQuery,
        std::string const& deleteQuery)
{
    LedgerIndex min = std::numeric_limits <LedgerIndex>::max();

    {
        auto db = database.checkoutDb ();
        boost::optional<std::uint64_t> m;
        *db << minQuery, soci::into(m);
        if (!m)
            return;
        min = *m;
    }

    if (health() != Health::ok)
        return;

    boost::format formattedDeleteQuery (deleteQuery);

    if (journal_.debug) journal_.debug <<
        "start: " << deleteQuery << " from " << min << " to " << lastRotated;
    while (min < lastRotated)
    {
        min = (min + setup_.deleteBatch >= lastRotated) ? lastRotated :
            min + setup_.deleteBatch;
        {
            auto db =  database.checkoutDb ();
            *db << boost::str (formattedDeleteQuery % min);
        }
        if (health())
            return;
        if (min < lastRotated)
            std::this_thread::sleep_for (
                    std::chrono::milliseconds (setup_.backOff));
    }
    journal_.debug << "finished: " << deleteQuery;
}

void
SHAMapStoreImp::clearCaches (LedgerIndex validatedSeq)
{
    ledgerMaster_->clearLedgerCachePrior (validatedSeq);
    fullBelowCache_->clear();
}

void
SHAMapStoreImp::freshenCaches()
{
    if (freshenCache (database_->getPositiveCache()))
        return;
    if (freshenCache (*treeNodeCache_))
        return;
    if (freshenCache (transactionMaster_.getCache()))
        return;
}

void
SHAMapStoreImp::clearPrior (LedgerIndex lastRotated)
{
    ledgerMaster_->clearPriorLedgers (lastRotated);
    if (health())
        return;

    // TODO This won't remove validations for ledgers that do not get
    // validated. That will likely require inserting LedgerSeq into
    // the validations table.
    //
    // This query has poor performance with large data sets.
    // The schema needs to be redesigned to avoid the JOIN, or an
    // RDBMS that supports concurrency should be used.
    /*
    clearSql (*ledgerDb_, lastRotated,
        "SELECT MIN(LedgerSeq) FROM Ledgers;",
        "DELETE FROM Validations WHERE LedgerHash IN "
        "(SELECT Ledgers.LedgerHash FROM Validations JOIN Ledgers ON "
        "Validations.LedgerHash=Ledgers.LedgerHash WHERE Ledgers.LedgerSeq < %u);");
     */

    if (health())
        return;

    clearSql (*ledgerDb_, lastRotated,
        "SELECT MIN(LedgerSeq) FROM Ledgers;",
        "DELETE FROM Ledgers WHERE LedgerSeq < %u;");
    if (health())
        return;

    clearSql (*transactionDb_, lastRotated,
        "SELECT MIN(LedgerSeq) FROM Transactions;",
        "DELETE FROM Transactions WHERE LedgerSeq < %u;");
    if (health())
        return;

    clearSql (*transactionDb_, lastRotated,
        "SELECT MIN(LedgerSeq) FROM AccountTransactions;",
        "DELETE FROM AccountTransactions WHERE LedgerSeq < %u;");
    if (health())
        return;
}

SHAMapStoreImp::Health
SHAMapStoreImp::health()
{
    {
        std::lock_guard<std::mutex> lock (mutex_);
        if (stop_)
            return Health::stopping;
    }
    if (!netOPs_)
        return Health::ok;

    NetworkOPs::OperatingMode mode = netOPs_->getOperatingMode();

    std::int32_t age = ledgerMaster_->getValidatedLedgerAge();
    if (mode != NetworkOPs::omFULL || age >= setup_.ageThreshold)
    {
        journal_.warning << "Not deleting. state: " << mode << " age " << age
                << " age threshold " << setup_.ageThreshold;
        healthy_ = false;
    }

    if (healthy_)
        return Health::ok;
    else
        return Health::unhealthy;
}

void
SHAMapStoreImp::onStop()
{
    if (setup_.deleteInterval)
    {
        {
            std::lock_guard <std::mutex> lock (mutex_);
            stop_ = true;
        }
        cond_.notify_one();
    }
    else
    {
        stopped();
    }
}

void
SHAMapStoreImp::onChildrenStopped()
{
    if (setup_.deleteInterval)
    {
        {
            std::lock_guard <std::mutex> lock (mutex_);
            stop_ = true;
        }
        cond_.notify_one();
    }
    else
    {
        stopped();
    }
}

//------------------------------------------------------------------------------
SHAMapStore::Setup
setup_SHAMapStore (Config const& c)
{
    SHAMapStore::Setup setup;

    // Get existing settings and add some default values if not specified:
    setup.nodeDatabase = c.section (ConfigSection::nodeDatabase ());

    // These two parameters apply only to RocksDB. We want to give them sensible
    // defaults if no values are specified.
    if (!setup.nodeDatabase.exists ("cache_mb"))
        setup.nodeDatabase.set ("cache_mb", std::to_string (c.getSize (siHashNodeDBCache)));

    if (!setup.nodeDatabase.exists ("filter_bits") && (c.NODE_SIZE >= 2))
        setup.nodeDatabase.set ("filter_bits", "10");

    get_if_exists (setup.nodeDatabase, "online_delete", setup.deleteInterval);

    if (setup.deleteInterval)
        get_if_exists (setup.nodeDatabase, "advisory_delete", setup.advisoryDelete);

    setup.ledgerHistory = c.LEDGER_HISTORY;
    setup.databasePath = c.legacy("database_path");

    get_if_exists (setup.nodeDatabase, "delete_batch", setup.deleteBatch);
    get_if_exists (setup.nodeDatabase, "backOff", setup.backOff);
    get_if_exists (setup.nodeDatabase, "age_threshold", setup.ageThreshold);

    return setup;
}

std::unique_ptr<SHAMapStore>
make_SHAMapStore (Application& app,
        SHAMapStore::Setup const& s,
        beast::Stoppable& parent,
        NodeStore::Scheduler& scheduler,
        beast::Journal journal,
        beast::Journal nodeStoreJournal,
        TransactionMaster& transactionMaster,
        BasicConfig const& config)
{
    return std::make_unique<SHAMapStoreImp>(app, s, parent, scheduler,
            journal, nodeStoreJournal, transactionMaster,
            config);
}

}
