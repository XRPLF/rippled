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


#include <ripple/app/ledger/TransactionMaster.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/app/misc/SHAMapStoreImp.h>
#include <ripple/beast/core/CurrentThreadName.h>
#include <ripple/core/ConfigSections.h>
#include <ripple/nodestore/impl/DatabaseRotatingImp.h>
#include <ripple/nodestore/impl/DatabaseShardImp.h>

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
    , working_(true)
    , transactionMaster_ (transactionMaster)
    , canDelete_ (std::numeric_limits <LedgerIndex>::max())
{
    if (setup_.deleteInterval)
    {
        auto const minInterval = setup.standalone ?
            minimumDeletionIntervalSA_ : minimumDeletionInterval_;
        if (setup_.deleteInterval < minInterval)
        {
            Throw<std::runtime_error> ("online_delete must be at least " +
                std::to_string (minInterval));
        }

        if (setup_.ledgerHistory > setup_.deleteInterval)
        {
            Throw<std::runtime_error> (
                "online_delete must not be less than ledger_history (currently " +
                std::to_string (setup_.ledgerHistory) + ")");
        }

        state_db_.init (config, dbName_);

        dbPaths();
    }
    if (! setup_.shardDatabase.empty())
    {
        // The node and shard stores must use
        // the same earliest ledger sequence
        std::array<std::uint32_t, 2> seq;
        if (get_if_exists<std::uint32_t>(
            setup_.nodeDatabase, "earliest_seq", seq[0]))
        {
            if (get_if_exists<std::uint32_t>(
                setup_.shardDatabase, "earliest_seq", seq[1]) &&
                seq[0] != seq[1])
            {
                Throw<std::runtime_error>("earliest_seq set more than once");
            }
        }

        boost::filesystem::path dbPath =
            get<std::string>(setup_.shardDatabase, "path");
        if (dbPath.empty())
            Throw<std::runtime_error>("shard path missing");
        if (boost::filesystem::exists(dbPath))
        {
            if (! boost::filesystem::is_directory(dbPath))
                Throw<std::runtime_error>("shard db path must be a directory.");
        }
        else
            boost::filesystem::create_directories(dbPath);

        auto const maxDiskSpace = get<std::uint64_t>(
            setup_.shardDatabase, "max_size_gb", 0);
        // Must be large enough for one shard
        if (maxDiskSpace < 3)
            Throw<std::runtime_error>("max_size_gb too small");
        if ((maxDiskSpace << 30) < maxDiskSpace)
            Throw<std::runtime_error>("overflow max_size_gb");

        std::uint32_t lps;
        if (get_if_exists<std::uint32_t>(
                setup_.shardDatabase, "ledgers_per_shard", lps))
        {
            // ledgers_per_shard to be set only in standalone for testing
            if (! setup_.standalone)
                Throw<std::runtime_error>(
                    "ledgers_per_shard only honored in stand alone");
        }
    }
}

std::unique_ptr <NodeStore::Database>
SHAMapStoreImp::makeDatabase (std::string const& name,
        std::int32_t readThreads, Stoppable& parent)
{
    std::unique_ptr <NodeStore::Database> db;
    if (setup_.deleteInterval)
    {
        SavedState state = state_db_.getState();
        auto writableBackend = makeBackendRotating(state.writableDb);
        auto archiveBackend = makeBackendRotating(state.archiveDb);
        if (! state.writableDb.size())
        {
            state.writableDb = writableBackend->getName();
            state.archiveDb = archiveBackend->getName();
            state_db_.setState (state);
        }

        // Create NodeStore with two backends to allow online deletion of data
        auto dbr = std::make_unique<NodeStore::DatabaseRotatingImp>(
            "NodeStore.main", scheduler_, readThreads, parent,
                std::move(writableBackend), std::move(archiveBackend),
                    setup_.nodeDatabase, nodeStoreJournal_);
        fdlimit_ += dbr->fdlimit();
        dbRotating_ = dbr.get();
        db.reset(dynamic_cast<NodeStore::Database*>(dbr.release()));
    }
    else
    {
        db = NodeStore::Manager::instance().make_Database (name, scheduler_,
            readThreads, parent, setup_.nodeDatabase, nodeStoreJournal_);
        fdlimit_ += db->fdlimit();
    }
    return db;
}

std::unique_ptr<NodeStore::DatabaseShard>
SHAMapStoreImp::makeDatabaseShard(std::string const& name,
    std::int32_t readThreads, Stoppable& parent)
{
    std::unique_ptr<NodeStore::DatabaseShard> db;
    if(! setup_.shardDatabase.empty())
    {
        db = std::make_unique<NodeStore::DatabaseShardImp>(
            app_, name, parent, scheduler_, readThreads,
                setup_.shardDatabase, app_.journal("ShardStore"));
        if (db->init())
            fdlimit_ += db->fdlimit();
        else
            db.reset();
    }
    return db;
}

void
SHAMapStoreImp::onLedgerClosed(
    std::shared_ptr<Ledger const> const& ledger)
{
    {
        std::lock_guard <std::mutex> lock (mutex_);
        newLedger_ = ledger;
        working_ = true;
    }
    cond_.notify_one();
}

void
SHAMapStoreImp::rendezvous() const
{
    if (!working_)
        return;

    std::unique_lock <std::mutex> lock(mutex_);
    rendezvous_.wait(lock, [&]
    {
        return !working_;
    });
}

int
SHAMapStoreImp::fdlimit () const
{
    return fdlimit_;
}

bool
SHAMapStoreImp::copyNode (std::uint64_t& nodeCount,
        SHAMapAbstractNode const& node)
{
    // Copy a single record from node to dbRotating_
    dbRotating_->fetch(node.getNodeHash().as_uint256(), node.getSeq());
    if (! (++nodeCount % checkHealthInterval_))
    {
        if (health())
            return false;
    }

    return true;
}

void
SHAMapStoreImp::run()
{
    beast::setCurrentThreadName ("SHAMapStore");
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
        std::shared_ptr<Ledger const> validatedLedger;

        {
            std::unique_lock <std::mutex> lock (mutex_);
            working_ = false;
            rendezvous_.notify_all();
            if (stop_)
            {
                stopped();
                return;
            }
            cond_.wait (lock);
            if (newLedger_)
            {
                validatedLedger = std::move(newLedger_);
            }
            else
                continue;
        }

        LedgerIndex validatedSeq = validatedLedger->info().seq;
        if (!lastRotated)
        {
            lastRotated = validatedSeq;
            state_db_.setLastRotated (lastRotated);
        }

        // will delete up to (not including) lastRotated)
        if (validatedSeq >= lastRotated + setup_.deleteInterval
                && canDelete_ >= lastRotated - 1)
        {
            JLOG(journal_.debug()) << "rotating  validatedSeq " << validatedSeq
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
            validatedLedger->stateMap().snapShot (
                    false)->visitNodes (
                    std::bind (&SHAMapStoreImp::copyNode, this,
                    std::ref(nodeCount), std::placeholders::_1));
            JLOG(journal_.debug()) << "copied ledger " << validatedSeq
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
            JLOG(journal_.debug()) << validatedSeq << " freshened caches";
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

            auto newBackend = makeBackendRotating();
            JLOG(journal_.debug()) << validatedSeq << " new backend "
                    << newBackend->getName();

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
                dbRotating_->getWritableBackend()->getName();
            lastRotated = validatedSeq;
            std::unique_ptr<NodeStore::Backend> oldBackend;
            {
                std::lock_guard <std::mutex> lock (dbRotating_->peekMutex());

                state_db_.setState (SavedState {newBackend->getName(),
                        nextArchiveDir, lastRotated});
                clearCaches (validatedSeq);
                oldBackend = dbRotating_->rotateBackends(
                    std::move(newBackend));
            }
            JLOG(journal_.debug()) << "finished rotation " << validatedSeq;

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
            journal_.error() << "node db path must be a directory. "
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

        journal_.error() << "state db error: " << std::endl
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

std::unique_ptr <NodeStore::Backend>
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

    auto backend {NodeStore::Manager::instance().make_Backend(
        parameters, scheduler_, nodeStoreJournal_)};
    backend->open();
    return backend;
}

bool
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
            return false;
        min = *m;
    }

    if(min > lastRotated || health() != Health::ok)
        return false;

    boost::format formattedDeleteQuery (deleteQuery);

    JLOG(journal_.debug()) <<
        "start: " << deleteQuery << " from " << min << " to " << lastRotated;
    while (min < lastRotated)
    {
        min = std::min(lastRotated, min + setup_.deleteBatch);
        {
            auto db =  database.checkoutDb ();
            *db << boost::str (formattedDeleteQuery % min);
        }
        if (health())
            return true;
        if (min < lastRotated)
            std::this_thread::sleep_for (
                    std::chrono::milliseconds (setup_.backOff));
    }
    JLOG(journal_.debug()) << "finished: " << deleteQuery;
    return true;
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
    if (freshenCache (dbRotating_->getPositiveCache()))
        return;
    if (freshenCache (*treeNodeCache_))
        return;
    if (freshenCache (transactionMaster_.getCache()))
        return;
}

void
SHAMapStoreImp::clearPrior (LedgerIndex lastRotated)
{
    if (health())
        return;

    ledgerMaster_->clearPriorLedgers (lastRotated);
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

    auto age = ledgerMaster_->getValidatedLedgerAge();
    if (mode != NetworkOPs::omFULL || age.count() >= setup_.ageThreshold)
    {
        JLOG(journal_.warn()) << "Not deleting. state: " << mode
                               << " age " << age.count()
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

    setup.standalone = c.standalone();

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

    setup.shardDatabase = c.section(ConfigSection::shardDatabase());
    return setup;
}

std::unique_ptr<SHAMapStore>
make_SHAMapStore (Application& app,
        SHAMapStore::Setup const& setup,
        Stoppable& parent,
        NodeStore::Scheduler& scheduler,
        beast::Journal journal,
        beast::Journal nodeStoreJournal,
        TransactionMaster& transactionMaster,
        BasicConfig const& config)
{
    return std::make_unique<SHAMapStoreImp>(app, setup, parent, scheduler,
        journal, nodeStoreJournal, transactionMaster, config);
}

}
