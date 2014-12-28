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

#include <ripple/app/misc/SHAMapStoreImp.h>
#include <beast/cxx14/memory.h> // <memory>

namespace ripple {

void
SHAMapStoreImp::SavedStateDB::init (std::string const& databasePath,
        std::string const& dbName)
{
    boost::filesystem::path pathName = databasePath;
    pathName /= dbName;

    std::lock_guard <std::mutex> lock (mutex_);

    auto error = session_.open (pathName.string());
    checkError (error);

    session_.once (error) << "PRAGMA synchronous=FULL;";
    checkError (error);

    session_.once (error) <<
            "CREATE TABLE IF NOT EXISTS DbState ("
            "  Key                    INTEGER PRIMARY KEY,"
            "  WritableDb             TEXT,"
            "  ArchiveDb              TEXT,"
            "  LastRotatedLedger      INTEGER"
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

LedgerIndex
SHAMapStoreImp::SavedStateDB::getCanDelete()
{
    beast::Error error;
    LedgerIndex seq;

    {
        std::lock_guard <std::mutex> lock (mutex_);

        session_.once (error) <<
                "SELECT CanDeleteSeq FROM CanDelete WHERE Key = 1;"
                , beast::sqdb::into (seq);
        ;
    }
    checkError (error);

    return seq;
}

LedgerIndex
SHAMapStoreImp::SavedStateDB::setCanDelete (LedgerIndex canDelete)
{
    beast::Error error;
    {
        std::lock_guard <std::mutex> lock (mutex_);

        session_.once (error) <<
                "UPDATE CanDelete SET CanDeleteSeq = ? WHERE Key = 1;"
                , beast::sqdb::use (canDelete)
                ;
    }
    checkError (error);

    return canDelete;
}

SHAMapStoreImp::SavedState
SHAMapStoreImp::SavedStateDB::getState()
{
    beast::Error error;
    SavedState state;

    {
        std::lock_guard <std::mutex> lock (mutex_);

        session_.once (error) <<
                "SELECT WritableDb, ArchiveDb, LastRotatedLedger"
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
SHAMapStoreImp::SavedStateDB::setState (SavedState const& state)
{
    beast::Error error;

    {
        std::lock_guard <std::mutex> lock (mutex_);
        session_.once (error) <<
                "UPDATE DbState"
                " SET WritableDb = ?,"
                " ArchiveDb = ?,"
                " LastRotatedLedger = ?"
                " WHERE Key = 1;"
                , beast::sqdb::use (state.writableDb)
                , beast::sqdb::use (state.archiveDb)
                , beast::sqdb::use (state.lastRotated)
                ;
    }
    checkError (error);
}

void
SHAMapStoreImp::SavedStateDB::setLastRotated (LedgerIndex seq)
{
    beast::Error error;
    {
        std::lock_guard <std::mutex> lock (mutex_);
        session_.once (error) <<
                "UPDATE DbState SET LastRotatedLedger = ?"
                " WHERE Key = 1;"
                , beast::sqdb::use (seq)
                ;
    }
    checkError (error);
}

void
SHAMapStoreImp::SavedStateDB::checkError (beast::Error const& error)
{
    if (error)
    {
        journal_.fatal << "state database error: " << error.code()
                << ": " << error.getReasonText();
        throw std::runtime_error ("State database error.");
    }
}

SHAMapStoreImp::SHAMapStoreImp (Setup const& setup,
        Stoppable& parent,
        NodeStore::Scheduler& scheduler,
        beast::Journal journal,
        beast::Journal nodeStoreJournal,
        TransactionMaster& transactionMaster)
    : SHAMapStore (parent)
    , setup_ (setup)
    , scheduler_ (scheduler)
    , journal_ (journal)
    , nodeStoreJournal_ (nodeStoreJournal)
    , database_ (nullptr)
    , transactionMaster_ (transactionMaster)
{
    if (setup_.deleteInterval)
    {
        if (setup_.ledgerHistory > setup_.deleteInterval ||
                setup_.ledgerHistory < minimumDeletionInterval_)
        {
            std::stringstream es;
            es << "online_delete (" << setup_.deleteInterval
                    << ") must be at least " << minimumDeletionInterval_
                    << " and cannot be less than LEDGER_HISTORY ("
                    << setup_.ledgerHistory << ")";
            throw std::runtime_error (es.str());
        }

        state_db_.init (setup_.databasePath, dbName_);

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
                readThreads, setup_.nodeDatabase,
                setup_.ephemeralNodeDatabase);
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
        SHAMapTreeNode const& node)
{
    // Copy a single record from node to database_
    database_->fetchNode (node.getNodeHash());
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
    netOPs_ = &getApp().getOPs();
    ledgerMaster_ = &getApp().getLedgerMaster();
    fullBelowCache_ = &getApp().getFullBelowCache();
    treeNodeCache_ = &getApp().getTreeNodeCache();
    transactionDb_ = &getApp().getTxnDB();
    ledgerDb_ = &getApp().getLedgerDB();

    while (1)
    {
        healthy_ = true;
        validatedLedger_.reset();

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
        lock.unlock();

        LedgerIndex validatedSeq = validatedLedger_->getLedgerSeq();
        if (!lastRotated)
        {
            lastRotated = validatedSeq;
            state_db_.setLastRotated (lastRotated);
        }
        LedgerIndex canDelete = std::numeric_limits <LedgerIndex>::max();
        if (setup_.advisoryDelete)
            canDelete = state_db_.getCanDelete();

        // will delete up to (not including) lastRotated)
        if (validatedSeq >= lastRotated + setup_.deleteInterval
                && canDelete >= lastRotated - 1)
        {
            journal_.debug << "rotating  validatedSeq " << validatedSeq
                    << " lastRotated " << lastRotated << " deleteInterval "
                    << setup_.deleteInterval << " canDelete " << canDelete;

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
            validatedLedger_->peekAccountStateMap()->snapShot (
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
            setup_.nodeDatabase["path"].toStdString();

    if (boost::filesystem::exists (dbPath))
    {
        if (! boost::filesystem::is_directory (dbPath))
        {
            std::cerr << "node db path must be a directory. "
                    << dbPath.string();
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
                << setup_.nodeDatabase["path"].toStdString()
                << std::endl;

        throw std::runtime_error ("state db error");
    }
}

std::shared_ptr <NodeStore::Backend>
SHAMapStoreImp::makeBackendRotating (std::string path)
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
    std::unique_ptr <NodeStore::Backend> fastBackend (
        (setup_.ephemeralNodeDatabase.size() > 0)
            ? NodeStore::Manager::instance().make_Backend (setup_.ephemeralNodeDatabase,
            scheduler_, journal_) : nullptr);

    return NodeStore::Manager::instance().make_DatabaseRotating ("NodeStore.main", scheduler_,
            readThreads, writableBackend, archiveBackend,
            std::move (fastBackend), nodeStoreJournal_);
}

void
SHAMapStoreImp::clearSql (DatabaseCon& database,
        LedgerIndex lastRotated,
        std::string const& minQuery,
        std::string const& deleteQuery)
{
    LedgerIndex min = std::numeric_limits <LedgerIndex>::max();
    Database* db = database.getDB();

    std::unique_lock <std::recursive_mutex> lock (database.peekMutex());
    if (!db->executeSQL (minQuery) || !db->startIterRows())
        return;
    min = db->getBigInt (0);
    db->endIterRows ();
    lock.unlock();
    if (health() != Health::ok)
        return;

    boost::format formattedDeleteQuery (deleteQuery);

    journal_.debug << "start: " << deleteQuery << " from "
            << min << " to " << lastRotated;
    while (min < lastRotated)
    {
        min = (min + setup_.deleteBatch >= lastRotated) ? lastRotated :
            min + setup_.deleteBatch;
        lock.lock();
        db->executeSQL (boost::str (formattedDeleteQuery % min));
        lock.unlock();
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
    // the validations table
    clearSql (*ledgerDb_, lastRotated,
        "SELECT MIN(LedgerSeq) FROM Ledgers;",
        "DELETE FROM Validations WHERE Ledgers.LedgerSeq < %u"
        " AND Validations.LedgerHash = Ledgers.LedgerHash;");
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

    if (c.nodeDatabase["online_delete"].isNotEmpty())
        setup.deleteInterval = c.nodeDatabase["online_delete"].getIntValue();
    if (c.nodeDatabase["advisory_delete"].isNotEmpty() && setup.deleteInterval)
        setup.advisoryDelete = c.nodeDatabase["advisory_delete"].getIntValue();
    setup.ledgerHistory = c.LEDGER_HISTORY;
    setup.nodeDatabase = c.nodeDatabase;
    setup.ephemeralNodeDatabase = c.ephemeralNodeDatabase;
    setup.databasePath = c.DATABASE_PATH;
    if (c.nodeDatabase["delete_batch"].isNotEmpty())
        setup.deleteBatch = c.nodeDatabase["delete_batch"].getIntValue();
    if (c.nodeDatabase["backOff"].isNotEmpty())
        setup.backOff = c.nodeDatabase["backOff"].getIntValue();
    if (c.nodeDatabase["age_threshold"].isNotEmpty())
        setup.ageThreshold = c.nodeDatabase["age_threshold"].getIntValue();

    return setup;
}

std::unique_ptr<SHAMapStore>
make_SHAMapStore (SHAMapStore::Setup const& s,
        beast::Stoppable& parent,
        NodeStore::Scheduler& scheduler,
        beast::Journal journal,
        beast::Journal nodeStoreJournal,
        TransactionMaster& transactionMaster)
{
    return std::make_unique<SHAMapStoreImp> (s, parent, scheduler,
            journal, nodeStoreJournal, transactionMaster);
}

}
