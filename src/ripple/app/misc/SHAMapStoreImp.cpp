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
#include <ripple/app/rdb/RelationalDBInterface_global.h>
#include <ripple/app/rdb/backend/RelationalDBInterfaceSqlite.h>
#include <ripple/beast/core/CurrentThreadName.h>
#include <ripple/core/ConfigSections.h>
#include <ripple/core/Pg.h>
#include <ripple/nodestore/impl/DatabaseRotatingImp.h>

#include <ripple/nodestore/Scheduler.h>

#include <boost/algorithm/string/predicate.hpp>

namespace ripple {
void
SHAMapStoreImp::SavedStateDB::init(
    BasicConfig const& config,
    std::string const& dbName)
{
    std::lock_guard lock(mutex_);
    initStateDB(sqlDb_, config, dbName);
}

LedgerIndex
SHAMapStoreImp::SavedStateDB::getCanDelete()
{
    std::lock_guard lock(mutex_);

    return ripple::getCanDelete(sqlDb_);
}

LedgerIndex
SHAMapStoreImp::SavedStateDB::setCanDelete(LedgerIndex canDelete)
{
    std::lock_guard lock(mutex_);

    return ripple::setCanDelete(sqlDb_, canDelete);
}

SavedState
SHAMapStoreImp::SavedStateDB::getState()
{
    std::lock_guard lock(mutex_);

    return ripple::getSavedState(sqlDb_);
}

void
SHAMapStoreImp::SavedStateDB::setState(SavedState const& state)
{
    std::lock_guard lock(mutex_);
    ripple::setSavedState(sqlDb_, state);
}

void
SHAMapStoreImp::SavedStateDB::setLastRotated(LedgerIndex seq)
{
    std::lock_guard lock(mutex_);
    ripple::setLastRotated(sqlDb_, seq);
}

//------------------------------------------------------------------------------

SHAMapStoreImp::SHAMapStoreImp(
    Application& app,
    NodeStore::Scheduler& scheduler,
    beast::Journal journal)
    : app_(app)
    , scheduler_(scheduler)
    , journal_(journal)
    , working_(true)
    , canDelete_(std::numeric_limits<LedgerIndex>::max())
{
    Config& config{app.config()};

    Section& section{config.section(ConfigSection::nodeDatabase())};
    if (section.empty())
    {
        Throw<std::runtime_error>(
            "Missing [" + ConfigSection::nodeDatabase() +
            "] entry in configuration file");
    }

    // RocksDB only. Use sensible defaults if no values specified.
    if (boost::iequals(get(section, "type"), "RocksDB"))
    {
        if (!section.exists("cache_mb"))
        {
            section.set(
                "cache_mb",
                std::to_string(config.getValueFor(SizedItem::hashNodeDBCache)));
        }

        if (!section.exists("filter_bits") && (config.NODE_SIZE >= 2))
            section.set("filter_bits", "10");
    }

    get_if_exists(section, "online_delete", deleteInterval_);

    if (deleteInterval_)
    {
        if (app_.config().reporting())
        {
            Throw<std::runtime_error>(
                "Reporting does not support online_delete. Remove "
                "online_delete info from config");
        }

        // Configuration that affects the behavior of online delete
        get_if_exists(section, "delete_batch", deleteBatch_);
        std::uint32_t temp;
        if (get_if_exists(section, "back_off_milliseconds", temp) ||
            // Included for backward compaibility with an undocumented setting
            get_if_exists(section, "backOff", temp))
        {
            backOff_ = std::chrono::milliseconds{temp};
        }
        if (get_if_exists(section, "age_threshold_seconds", temp))
            ageThreshold_ = std::chrono::seconds{temp};
        if (get_if_exists(section, "recovery_wait_seconds", temp))
            recoveryWaitTime_.emplace(std::chrono::seconds{temp});

        get_if_exists(section, "advisory_delete", advisoryDelete_);

        auto const minInterval = config.standalone()
            ? minimumDeletionIntervalSA_
            : minimumDeletionInterval_;
        if (deleteInterval_ < minInterval)
        {
            Throw<std::runtime_error>(
                "online_delete must be at least " +
                std::to_string(minInterval));
        }

        if (config.LEDGER_HISTORY > deleteInterval_)
        {
            Throw<std::runtime_error>(
                "online_delete must not be less than ledger_history "
                "(currently " +
                std::to_string(config.LEDGER_HISTORY) + ")");
        }

        state_db_.init(config, dbName_);
        dbPaths();
    }
}

std::unique_ptr<NodeStore::Database>
SHAMapStoreImp::makeNodeStore(std::int32_t readThreads)
{
    auto nscfg = app_.config().section(ConfigSection::nodeDatabase());

    // Provide default values:
    if (!nscfg.exists("cache_size"))
        nscfg.set(
            "cache_size",
            std::to_string(app_.config().getValueFor(
                SizedItem::treeCacheSize, std::nullopt)));

    if (!nscfg.exists("cache_age"))
        nscfg.set(
            "cache_age",
            std::to_string(app_.config().getValueFor(
                SizedItem::treeCacheAge, std::nullopt)));

    std::unique_ptr<NodeStore::Database> db;

    if (deleteInterval_)
    {
        if (app_.config().reporting())
        {
            Throw<std::runtime_error>(
                "Reporting does not support online_delete. Remove "
                "online_delete info from config");
        }
        SavedState state = state_db_.getState();
        auto writableBackend = makeBackendRotating(state.writableDb);
        auto archiveBackend = makeBackendRotating(state.archiveDb);
        if (!state.writableDb.size())
        {
            state.writableDb = writableBackend->getName();
            state.archiveDb = archiveBackend->getName();
            state_db_.setState(state);
        }

        // Create NodeStore with two backends to allow online deletion of
        // data
        auto dbr = std::make_unique<NodeStore::DatabaseRotatingImp>(
            scheduler_,
            readThreads,
            std::move(writableBackend),
            std::move(archiveBackend),
            nscfg,
            app_.logs().journal(nodeStoreName_));
        fdRequired_ += dbr->fdRequired();
        dbRotating_ = dbr.get();
        db.reset(dynamic_cast<NodeStore::Database*>(dbr.release()));
    }
    else
    {
        db = NodeStore::Manager::instance().make_Database(
            megabytes(
                app_.config().getValueFor(SizedItem::burstSize, std::nullopt)),
            scheduler_,
            readThreads,
            nscfg,
            app_.logs().journal(nodeStoreName_));
        fdRequired_ += db->fdRequired();
    }
    return db;
}

void
SHAMapStoreImp::onLedgerClosed(std::shared_ptr<Ledger const> const& ledger)
{
    {
        std::lock_guard lock(mutex_);
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

    std::unique_lock<std::mutex> lock(mutex_);
    rendezvous_.wait(lock, [&] { return !working_; });
}

int
SHAMapStoreImp::fdRequired() const
{
    return fdRequired_;
}

bool
SHAMapStoreImp::copyNode(std::uint64_t& nodeCount, SHAMapTreeNode const& node)
{
    // Copy a single record from node to dbRotating_
    dbRotating_->fetchNodeObject(
        node.getHash().as_uint256(),
        0,
        NodeStore::FetchType::synchronous,
        true);
    if (!(++nodeCount % checkHealthInterval_))
    {
        if (health())
            return false;
    }

    return true;
}

void
SHAMapStoreImp::run()
{
    if (app_.config().reporting())
    {
        assert(false);
        Throw<std::runtime_error>(
            "Reporting does not support online_delete. Remove "
            "online_delete info from config");
    }
    beast::setCurrentThreadName("SHAMapStore");
    LedgerIndex lastRotated = state_db_.getState().lastRotated;
    netOPs_ = &app_.getOPs();
    ledgerMaster_ = &app_.getLedgerMaster();
    fullBelowCache_ = &(*app_.getNodeFamily().getFullBelowCache(0));
    treeNodeCache_ = &(*app_.getNodeFamily().getTreeNodeCache(0));

    if (advisoryDelete_)
        canDelete_ = state_db_.getCanDelete();

    while (true)
    {
        healthy_ = true;
        std::shared_ptr<Ledger const> validatedLedger;

        {
            std::unique_lock<std::mutex> lock(mutex_);
            working_ = false;
            rendezvous_.notify_all();
            if (stop_)
            {
                return;
            }
            cond_.wait(lock);
            if (newLedger_)
            {
                validatedLedger = std::move(newLedger_);
            }
            else
                continue;
        }

        LedgerIndex const validatedSeq = validatedLedger->info().seq;
        if (!lastRotated)
        {
            lastRotated = validatedSeq;
            state_db_.setLastRotated(lastRotated);
        }

        bool const readyToRotate =
            validatedSeq >= lastRotated + deleteInterval_ &&
            canDelete_ >= lastRotated - 1 && !health();

        // Make sure we don't delete ledgers currently being
        // imported into the ShardStore
        bool const waitForImport = readyToRotate && [this, lastRotated] {
            if (auto shardStore = app_.getShardStore())
            {
                if (auto sequence = shardStore->getDatabaseImportSequence())
                    return sequence <= lastRotated - 1;
            }

            return false;
        }();

        if (waitForImport)
        {
            JLOG(journal_.info())
                << "NOT rotating validatedSeq " << validatedSeq
                << " as rotation would interfere with ShardStore import";
        }

        // will delete up to (not including) lastRotated
        if (readyToRotate && !waitForImport)
        {
            JLOG(journal_.warn())
                << "rotating  validatedSeq " << validatedSeq << " lastRotated "
                << lastRotated << " deleteInterval " << deleteInterval_
                << " canDelete_ " << canDelete_ << " state "
                << app_.getOPs().strOperatingMode(false) << " age "
                << ledgerMaster_->getValidatedLedgerAge().count() << 's';

            clearPrior(lastRotated);
            switch (health())
            {
                case Health::stopping:
                    return;
                case Health::unhealthy:
                    continue;
                case Health::ok:
                default:;
            }

            JLOG(journal_.debug()) << "copying ledger " << validatedSeq;
            std::uint64_t nodeCount = 0;
            validatedLedger->stateMap().snapShot(false)->visitNodes(std::bind(
                &SHAMapStoreImp::copyNode,
                this,
                std::ref(nodeCount),
                std::placeholders::_1));
            switch (health())
            {
                case Health::stopping:
                    return;
                case Health::unhealthy:
                    continue;
                case Health::ok:
                default:;
            }
            // Only log if we completed without a "health" abort
            JLOG(journal_.debug()) << "copied ledger " << validatedSeq
                                   << " nodecount " << nodeCount;

            JLOG(journal_.debug()) << "freshening caches";
            freshenCaches();
            switch (health())
            {
                case Health::stopping:
                    return;
                case Health::unhealthy:
                    continue;
                case Health::ok:
                default:;
            }
            // Only log if we completed without a "health" abort
            JLOG(journal_.debug()) << validatedSeq << " freshened caches";

            JLOG(journal_.trace()) << "Making a new backend";
            auto newBackend = makeBackendRotating();
            JLOG(journal_.debug())
                << validatedSeq << " new backend " << newBackend->getName();

            clearCaches(validatedSeq);
            switch (health())
            {
                case Health::stopping:
                    return;
                case Health::unhealthy:
                    continue;
                case Health::ok:
                default:;
            }

            lastRotated = validatedSeq;

            dbRotating_->rotateWithLock(
                [&](std::string const& writableBackendName) {
                    SavedState savedState;
                    savedState.writableDb = newBackend->getName();
                    savedState.archiveDb = writableBackendName;
                    savedState.lastRotated = lastRotated;
                    state_db_.setState(savedState);

                    clearCaches(validatedSeq);

                    return std::move(newBackend);
                });

            JLOG(journal_.warn()) << "finished rotation " << validatedSeq;
        }
    }
}

void
SHAMapStoreImp::dbPaths()
{
    Section section{app_.config().section(ConfigSection::nodeDatabase())};
    boost::filesystem::path dbPath = get(section, "path");

    if (boost::filesystem::exists(dbPath))
    {
        if (!boost::filesystem::is_directory(dbPath))
        {
            journal_.error()
                << "node db path must be a directory. " << dbPath.string();
            Throw<std::runtime_error>("node db path must be a directory.");
        }
    }
    else
    {
        boost::filesystem::create_directories(dbPath);
    }

    SavedState state = state_db_.getState();

    {
        auto update = [&dbPath](std::string& sPath) {
            if (sPath.empty())
                return false;

            // Check if configured "path" matches stored directory path
            using namespace boost::filesystem;
            auto const stored{path(sPath)};
            if (stored.parent_path() == dbPath)
                return false;

            sPath = (dbPath / stored.filename()).string();
            return true;
        };

        if (update(state.writableDb))
        {
            update(state.archiveDb);
            state_db_.setState(state);
        }
    }

    bool writableDbExists = false;
    bool archiveDbExists = false;

    for (boost::filesystem::directory_iterator it(dbPath);
         it != boost::filesystem::directory_iterator();
         ++it)
    {
        if (!state.writableDb.compare(it->path().string()))
            writableDbExists = true;
        else if (!state.archiveDb.compare(it->path().string()))
            archiveDbExists = true;
        else if (!dbPrefix_.compare(it->path().stem().string()))
            boost::filesystem::remove_all(it->path());
    }

    if ((!writableDbExists && state.writableDb.size()) ||
        (!archiveDbExists && state.archiveDb.size()) ||
        (writableDbExists != archiveDbExists) ||
        state.writableDb.empty() != state.archiveDb.empty())
    {
        boost::filesystem::path stateDbPathName =
            app_.config().legacy("database_path");
        stateDbPathName /= dbName_;
        stateDbPathName += "*";

        journal_.error()
            << "state db error:\n"
            << "  writableDbExists " << writableDbExists << " archiveDbExists "
            << archiveDbExists << '\n'
            << "  writableDb '" << state.writableDb << "' archiveDb '"
            << state.archiveDb << "\n\n"
            << "The existing data is in a corrupted state.\n"
            << "To resume operation, remove the files matching "
            << stateDbPathName.string() << " and contents of the directory "
            << get(section, "path") << '\n'
            << "Optionally, you can move those files to another\n"
            << "location if you wish to analyze or back up the data.\n"
            << "However, there is no guarantee that the data in its\n"
            << "existing form is usable.";

        Throw<std::runtime_error>("state db error");
    }
}

std::unique_ptr<NodeStore::Backend>
SHAMapStoreImp::makeBackendRotating(std::string path)
{
    Section section{app_.config().section(ConfigSection::nodeDatabase())};
    boost::filesystem::path newPath;

    if (path.size())
    {
        newPath = path;
    }
    else
    {
        boost::filesystem::path p = get(section, "path");
        p /= dbPrefix_;
        p += ".%%%%";
        newPath = boost::filesystem::unique_path(p);
    }
    section.set("path", newPath.string());

    auto backend{NodeStore::Manager::instance().make_Backend(
        section,
        megabytes(
            app_.config().getValueFor(SizedItem::burstSize, std::nullopt)),
        scheduler_,
        app_.logs().journal(nodeStoreName_))};
    backend->open();
    return backend;
}

void
SHAMapStoreImp::clearSql(
    LedgerIndex lastRotated,
    std::string const& TableName,
    std::function<std::optional<LedgerIndex>()> const& getMinSeq,
    std::function<void(LedgerIndex)> const& deleteBeforeSeq)
{
    assert(deleteInterval_);
    LedgerIndex min = std::numeric_limits<LedgerIndex>::max();

    {
        JLOG(journal_.trace())
            << "Begin: Look up lowest value of: " << TableName;
        auto m = getMinSeq();
        JLOG(journal_.trace()) << "End: Look up lowest value of: " << TableName;
        if (!m)
            return;
        min = *m;
    }

    if (min > lastRotated || health() != Health::ok)
        return;
    if (min == lastRotated)
    {
        // Micro-optimization mainly to clarify logs
        JLOG(journal_.trace()) << "Nothing to delete from " << TableName;
        return;
    }

    JLOG(journal_.debug()) << "start deleting in: " << TableName << " from "
                           << min << " to " << lastRotated;
    while (min < lastRotated)
    {
        min = std::min(lastRotated, min + deleteBatch_);
        JLOG(journal_.trace())
            << "Begin: Delete up to " << deleteBatch_
            << " rows with LedgerSeq < " << min << " from: " << TableName;
        deleteBeforeSeq(min);
        JLOG(journal_.trace())
            << "End: Delete up to " << deleteBatch_ << " rows with LedgerSeq < "
            << min << " from: " << TableName;
        if (health())
            return;
        if (min < lastRotated)
            std::this_thread::sleep_for(backOff_);
        if (health())
            return;
    }
    JLOG(journal_.debug()) << "finished deleting from: " << TableName;
}

void
SHAMapStoreImp::clearCaches(LedgerIndex validatedSeq)
{
    ledgerMaster_->clearLedgerCachePrior(validatedSeq);
    fullBelowCache_->clear();
}

void
SHAMapStoreImp::freshenCaches()
{
    if (freshenCache(*treeNodeCache_))
        return;
    if (freshenCache(app_.getMasterTransaction().getCache()))
        return;
}

void
SHAMapStoreImp::clearPrior(LedgerIndex lastRotated)
{
    if (app_.config().reporting())
    {
        assert(false);
        Throw<std::runtime_error>(
            "Reporting does not support online_delete. Remove "
            "online_delete info from config");
    }
    // Do not allow ledgers to be acquired from the network
    // that are about to be deleted.
    minimumOnline_ = lastRotated + 1;
    JLOG(journal_.trace()) << "Begin: Clear internal ledgers up to "
                           << lastRotated;
    ledgerMaster_->clearPriorLedgers(lastRotated);
    JLOG(journal_.trace()) << "End: Clear internal ledgers up to "
                           << lastRotated;
    if (health())
        return;

    RelationalDBInterfaceSqlite* iface =
        dynamic_cast<RelationalDBInterfaceSqlite*>(
            &app_.getRelationalDBInterface());

    clearSql(
        lastRotated,
        "Ledgers",
        [&iface]() -> std::optional<LedgerIndex> {
            return iface->getMinLedgerSeq();
        },
        [&iface](LedgerIndex min) -> void {
            iface->deleteBeforeLedgerSeq(min);
        });
    if (health())
        return;

    if (!app_.config().useTxTables())
        return;

    clearSql(
        lastRotated,
        "Transactions",
        [&iface]() -> std::optional<LedgerIndex> {
            return iface->getTransactionsMinLedgerSeq();
        },
        [&iface](LedgerIndex min) -> void {
            iface->deleteTransactionsBeforeLedgerSeq(min);
        });
    if (health())
        return;

    clearSql(
        lastRotated,
        "AccountTransactions",
        [&iface]() -> std::optional<LedgerIndex> {
            return iface->getAccountTransactionsMinLedgerSeq();
        },
        [&iface](LedgerIndex min) -> void {
            iface->deleteAccountTransactionsBeforeLedgerSeq(min);
        });
    if (health())
        return;
}

SHAMapStoreImp::Health
SHAMapStoreImp::health()
{
    {
        std::lock_guard lock(mutex_);
        if (stop_)
            return Health::stopping;
    }
    if (!netOPs_)
        return Health::ok;
    assert(deleteInterval_);

    if (healthy_)
    {
        auto age = ledgerMaster_->getValidatedLedgerAge();
        OperatingMode mode = netOPs_->getOperatingMode();
        if (recoveryWaitTime_ && mode == OperatingMode::SYNCING &&
            age < ageThreshold_)
        {
            JLOG(journal_.warn())
                << "Waiting " << recoveryWaitTime_->count()
                << "s for node to get back into sync with network. state: "
                << app_.getOPs().strOperatingMode(mode, false) << ". age "
                << age.count() << 's';
            std::this_thread::sleep_for(*recoveryWaitTime_);

            age = ledgerMaster_->getValidatedLedgerAge();
            mode = netOPs_->getOperatingMode();
        }
        if (mode != OperatingMode::FULL || age > ageThreshold_)
        {
            JLOG(journal_.warn()) << "Not deleting. state: "
                                  << app_.getOPs().strOperatingMode(mode, false)
                                  << ". age " << age.count() << 's';
            healthy_ = false;
        }
    }

    if (healthy_)
        return Health::ok;
    else
        return Health::unhealthy;
}

void
SHAMapStoreImp::stop()
{
    if (thread_.joinable())
    {
        {
            std::lock_guard lock(mutex_);
            stop_ = true;
            cond_.notify_one();
        }
        thread_.join();
    }
}

std::optional<LedgerIndex>
SHAMapStoreImp::minimumOnline() const
{
    // minimumOnline_ with 0 value is equivalent to unknown/not set.
    // Don't attempt to acquire ledgers if that value is unknown.
    if (deleteInterval_ && minimumOnline_)
        return minimumOnline_.load();
    return app_.getLedgerMaster().minSqlSeq();
}

//------------------------------------------------------------------------------

std::unique_ptr<SHAMapStore>
make_SHAMapStore(
    Application& app,
    NodeStore::Scheduler& scheduler,
    beast::Journal journal)
{
    return std::make_unique<SHAMapStoreImp>(app, scheduler, journal);
}

}  // namespace ripple
