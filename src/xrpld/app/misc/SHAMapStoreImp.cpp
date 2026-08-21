#include <xrpld/app/misc/SHAMapStoreImp.h>

#include <xrpld/app/ledger/TransactionMaster.h>
#include <xrpld/app/misc/SHAMapStore.h>
#include <xrpld/app/rdb/backend/SQLiteDatabase.h>
#include <xrpld/core/Config.h>

#include <xrpl/basics/ByteUtilities.h>
#include <xrpl/basics/FileUtilities.h>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/contract.h>
#include <xrpl/basics/scope.h>
#include <xrpl/beast/core/CurrentThreadName.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/config/BasicConfig.h>
#include <xrpl/config/Constants.h>
#include <xrpl/ledger/Ledger.h>
#include <xrpl/nodestore/Database.h>
#include <xrpl/nodestore/Manager.h>
#include <xrpl/nodestore/NodeObject.h>
#include <xrpl/nodestore/Scheduler.h>
#include <xrpl/nodestore/detail/DatabaseRotatingImp.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/server/NetworkOPs.h>
#include <xrpl/server/State.h>
#include <xrpl/shamap/SHAMapMissingNode.h>
#include <xrpl/shamap/SHAMapTreeNode.h>

#include <boost/algorithm/string/predicate.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace xrpl {
void
SHAMapStoreImp::SavedStateDB::init(BasicConfig const& config, std::string const& dbName)
{
    std::scoped_lock const lock(mutex);
    initStateDB(sqlDb, config, dbName);
}

LedgerIndex
SHAMapStoreImp::SavedStateDB::getCanDelete()
{
    std::scoped_lock const lock(mutex);

    return xrpl::getCanDelete(sqlDb);
}

LedgerIndex
SHAMapStoreImp::SavedStateDB::setCanDelete(LedgerIndex canDelete)
{
    std::scoped_lock const lock(mutex);

    return xrpl::setCanDelete(sqlDb, canDelete);
}

SavedState
SHAMapStoreImp::SavedStateDB::getState()
{
    std::scoped_lock const lock(mutex);

    return xrpl::getSavedState(sqlDb);
}

void
SHAMapStoreImp::SavedStateDB::setState(SavedState const& state)
{
    std::scoped_lock const lock(mutex);
    xrpl::setSavedState(sqlDb, state);
}

void
SHAMapStoreImp::SavedStateDB::setLastRotated(LedgerIndex seq)
{
    std::scoped_lock const lock(mutex);
    xrpl::setLastRotated(sqlDb, seq);
}

//------------------------------------------------------------------------------

SHAMapStoreImp::SHAMapStoreImp(
    Application& app,
    node_store::Scheduler& scheduler,
    beast::Journal journal)
    : app_(app)
    , scheduler_(scheduler)
    , journal_(journal)
    , working_(true)
    , canDelete_(std::numeric_limits<LedgerIndex>::max())
{
    Config& config{app.config()};

    Section& section{config.section(Sections::kNodeDatabase)};
    if (section.empty())
    {
        Throw<std::runtime_error>(
            std::string("Missing [") + Sections::kNodeDatabase + "] entry in configuration file");
    }

    // RocksDB only. Use sensible defaults if no values specified.
    if (boost::iequals(get(section, Keys::kType), "RocksDB"))
    {
        if (!section.exists(Keys::kCacheMb))
        {
            section.set(
                Keys::kCacheMb, std::to_string(config.getValueFor(SizedItem::HashNodeDbCache)));
        }

        if (!section.exists(Keys::kFilterBits) && (config.nodeSize >= 2))
            section.set(Keys::kFilterBits, "10");
    }

    getIfExists(section, Keys::kOnlineDelete, deleteInterval_);

    if (deleteInterval_ != 0u)
    {
        auto const minInterval =
            config.standalone() ? kMinimumDeletionIntervalSa : kMinimumDeletionInterval;
        if (deleteInterval_ < minInterval)
        {
            Throw<std::runtime_error>(
                "online_delete must be at least " + std::to_string(minInterval));
        }

        if (config.ledgerHistory > deleteInterval_)
        {
            Throw<std::runtime_error>(
                "online_delete must not be less than ledger_history "
                "(currently " +
                std::to_string(config.ledgerHistory) + ")");
        }

        // Configuration that affects the behavior of online delete
        getIfExists(section, Keys::kDeleteBatch, deleteBatch_);
        std::uint32_t temp = 0;
        if (getIfExists(section, Keys::kBackOffMilliseconds, temp) ||
            // Included for backward compatibility with an undocumented setting
            getIfExists(section, Keys::kBackOff, temp))
        {
            backOff_ = std::chrono::milliseconds{temp};
        }
        if (getIfExists(section, Keys::kAgeThresholdSeconds, temp))
            ageThreshold_ = std::chrono::seconds{temp};
        if (getIfExists(section, Keys::kRecoveryWaitSeconds, temp))
            recoveryWaitTime_ = std::chrono::seconds{temp};
        if (recoveryWaitTime_ < std::chrono::seconds{1})
            Throw<std::runtime_error>("recovery_wait_seconds must be at least 1 second");

        getIfExists(section, Keys::kAdvisoryDelete, advisoryDelete_);

        if (getIfExists(section, Keys::kMaxWaitingLedgers, temp))
        {
            maxWaitingLedgers_ = temp;
        }
        else
        {
            maxWaitingLedgers_ = deleteInterval_;
        }

        auto const minWaiting = minInterval / 4;
        if (maxWaitingLedgers_ < minWaiting)
        {
            Throw<std::runtime_error>(
                "max_waiting_ledgers must be at least " + std::to_string(minWaiting));
        }

        stateDb_.init(config, dbName_);
        dbPaths();
    }
}

std::unique_ptr<node_store::Database>
SHAMapStoreImp::makeNodeStore(int readThreads)
{
    auto nscfg = app_.config().section(Sections::kNodeDatabase);

    // Provide default values.
    if (!nscfg.exists(Keys::kCacheSize))
    {
        nscfg.set(
            Keys::kCacheSize,
            std::to_string(app_.config().getValueFor(SizedItem::TreeCacheSize, std::nullopt)));
    }

    if (!nscfg.exists(Keys::kCacheAge))
    {
        nscfg.set(
            Keys::kCacheAge,
            std::to_string(app_.config().getValueFor(SizedItem::TreeCacheAge, std::nullopt)));
    }

    std::unique_ptr<node_store::Database> db;

    if (deleteInterval_ != 0u)
    {
        SavedState state = stateDb_.getState();
        auto writableBackend = makeBackendRotating(state.writableDb);
        auto archiveBackend = makeBackendRotating(state.archiveDb);
        if (state.writableDb.empty())
        {
            state.writableDb = writableBackend->getName();
            state.archiveDb = archiveBackend->getName();
            stateDb_.setState(state);
        }

        // Create NodeStore with two backends to allow online deletion of
        // data
        auto dbr = std::make_unique<node_store::DatabaseRotatingImp>(
            scheduler_,
            readThreads,
            std::move(writableBackend),
            std::move(archiveBackend),
            nscfg,
            app_.getJournal(kNodeStoreName));
        fdRequired_ += dbr->fdRequired();
        dbRotating_ = dbr.get();
        db.reset(dynamic_cast<node_store::Database*>(dbr.release()));
    }
    else
    {
        db = node_store::Manager::instance().makeDatabase(
            megabytes(app_.config().getValueFor(SizedItem::BurstSize, std::nullopt)),
            scheduler_,
            readThreads,
            nscfg,
            app_.getJournal(kNodeStoreName));
        fdRequired_ += db->fdRequired();
    }
    return db;
}

void
SHAMapStoreImp::onLedgerClosed(std::shared_ptr<Ledger const> const& ledger)
{
    {
        std::scoped_lock const lock(mutex_);
        newLedger_ = ledger;
        working_ = true;
    }
    cond_.notify_one();
}

[[nodiscard]]
bool
SHAMapStoreImp::rendezvous(std::optional<std::chrono::milliseconds> const& timeout) const
{
    if (!working_)
        return true;

    auto notWorking = [&] { return !working_; };

    std::unique_lock<std::mutex> lock(mutex_);
    if (timeout)
    {
        return rendezvous_.wait_for(lock, *timeout, notWorking);
    }
    rendezvous_.wait(lock, notWorking);
    return true;
}

int
SHAMapStoreImp::fdRequired() const
{
    return fdRequired_;
}

void
SHAMapStoreImp::rescueNode(SHAMapTreeNode const& node, std::optional<NodeObjectType> expectedType)
{
    XRPL_ASSERT(node.cowid() == 0, "SHAMapStoreImp::rescueNode : rescued node must be clean");
    // Reachable from the validated state map in memory, but present in
    // neither backend: its only on-disk copy lived in a backend removed by
    // an earlier rotation, and it was never rewritten because it is clean
    // (cowid == 0, so flushDirty skips it). Persist the in-memory body
    // directly into the writable backend so it survives this rotation
    // instead of later surfacing as an unresolvable SHAMapMissingNode.

    auto const nodeType = node.getType();
    auto const objectType = std::invoke([nodeType, expectedType] {
        switch (nodeType)
        {
            case SHAMapNodeType::TnAccountState:
                return NodeObjectType::AccountNode;
            // We don't expect to see transaction nodes. The check below will prevent writing them.
            case SHAMapNodeType::TnTransactionNm:
            case SHAMapNodeType::TnTransactionMd:
                return NodeObjectType::TransactionNode;
            case SHAMapNodeType::TnInner:
                return expectedType.value_or(NodeObjectType::Unknown);
            default:
                return NodeObjectType::Unknown;
        }
    });

    auto const hash = node.getHash().asUInt256();
    XRPL_ASSERT_IF(
        expectedType,
        *expectedType == objectType,
        "SHAMapStoreImp::rescueNode : expected node type");

    if (objectType != NodeObjectType::AccountNode || (expectedType && *expectedType != objectType))
    {
        // LCOV_EXCL_START
        JLOG(journal_.warn())
            << "rescueNode: unable to re-store node with unsupported/unknown type, hash=" << hash
            << " type=" << static_cast<int>(nodeType);
        // We do not expect to see Inner nodes rescued without an expected type. Analysis and
        // experimentation so far indicate that it just doesn't happen, specifically in
        // freshenCaches. This UNREACHABLE is as much a developer alert as it is a safety check. If
        // it does happen, we want to know about it. It won't affect production deployments.
        UNREACHABLE("SHAMapStoreImp::rescueNode : unsupported node type");
        return;
        // LCOV_EXCL_STOP
    }

    Serializer s;
    node.serializeWithPrefix(s);
    dbRotating_->store(objectType, std::move(s.modData()), hash, 0);

    JLOG(journal_.info()) << "rescueNode: re-stored node missing from both backends, hash=" << hash
                          << " type=" << static_cast<int>(nodeType);
}

bool
SHAMapStoreImp::copyNode(std::uint64_t& nodeCount, SHAMapTreeNode const& node)
{
    // Copy a single record from node to dbRotating_
    auto obj = dbRotating_->fetchNodeObject(
        node.getHash().asUInt256(), 0, node_store::FetchType::Synchronous, true);
    if (!obj)
    {
        rescueNode(node, NodeObjectType::AccountNode);
    }
    if ((++nodeCount % checkHealthInterval_) == 0u)
    {
        if (healthWait() != HealthResult::KeepGoing)
            return false;
    }

    return true;
}

void
SHAMapStoreImp::run()
{
    beast::setCurrentThreadName("SHAMapStore");
    LedgerIndex lastRotated = stateDb_.getState().lastRotated;
    netOPs_ = &app_.getOPs();
    ledgerMaster_ = &app_.getLedgerMaster();
    fullBelowCache_ = &(*app_.getNodeFamily().getFullBelowCache());
    treeNodeCache_ = &(*app_.getNodeFamily().getTreeNodeCache());

    if (advisoryDelete_)
        canDelete_ = stateDb_.getCanDelete();

    while (true)
    {
        XRPL_ASSERT(
            !dbRotating_->isRotationInFlight(),
            "SHAMapStoreImp::run : rotationInFlight_ must be false "
            "outside rotation window");

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
            {
                continue;
            }
        }

        LedgerIndex const validatedSeq = validatedLedger->header().seq;
        if (lastRotated == 0u)
        {
            lastRotated = validatedSeq;
            stateDb_.setLastRotated(lastRotated);
        }

        // We're starting a new cycle, so reset back to the default.
        lastSuccessfulHealthCheck_ = 0;

        bool const readyToRotate = validatedSeq >= lastRotated + deleteInterval_ &&
            canDelete_ >= lastRotated - 1 && healthWait() == HealthResult::KeepGoing;

        {
            // Note that this is set after the healthWait() check, so that we
            // don't start the rotation until the validated ledger is fully
            // processed. It is not guaranteed to be done at this point. It also
            // allows the testLedgerGaps unit test to work.
            std::unique_lock<std::mutex> lock(mutex_);
            if (newLedger_)
            {
                // It is possible, though very unlikely outside of tests which manipulate internals,
                // that healthWait() took so long that the validated ledger (newLedger_) has moved
                // on from where we started. If that's the case, update lastGoodValidatedLedger_
                // to that ledger's sequence number.
                lastGoodValidatedLedger_ = newLedger_->header().seq;
            }
            else
            {
                lastGoodValidatedLedger_ = validatedSeq;
            }
            auto const l = lastGoodValidatedLedger_;
            lock.unlock();
            JLOG(journal_.trace()) << "run: Set lastGoodValidatedLedger_ to " << l;
        }

        // will delete up to (not including) lastRotated
        if (readyToRotate)
        {
            auto const diff = validatedSeq - lastRotated;
            JLOG(journal_.warn()) << "ROTATING: validatedSeq " << validatedSeq << " lastRotated "
                                  << lastRotated << " diff " << diff << " deleteInterval "
                                  << deleteInterval_ << " canDelete_ " << canDelete_ << " state "
                                  << app_.getOPs().strOperatingMode(false) << " age "
                                  << ledgerMaster_->getValidatedLedgerAge().count()
                                  << "s. Complete ledgers: " << ledgerMaster_->getCompleteLedgers();

            // Close the getKeys()->swap exposure window: from here until
            // rotate() completes, an ordinary read for new ledgers served by the archive is
            // copied forward into the writable backend, so a node fetched
            // from the doomed archive cannot be left RAM-only when the
            // archive is deleted. Use ScopeExit so the early returns and continues below (and any
            // exceptions) also clear the flag.
            ScopeExit const clearRotationInFlight{
                [this] { dbRotating_->setRotationInFlight(false); }};
            dbRotating_->setRotationInFlight(true);

            clearPrior(lastRotated);
            switch (healthWait())
            {
                case HealthResult::Stopping:
                    return;
                case HealthResult::Expired:
                    continue;
                case HealthResult::KeepGoing:
                    break;
            }

            JLOG(journal_.debug()) << "copying ledger " << validatedSeq;
            std::uint64_t nodeCount = 0;

            try
            {
                validatedLedger->stateMap().snapShot(false)->visitNodes(
                    [this, &nodeCount](SHAMapTreeNode const& node) {
                        return copyNode(nodeCount, node);
                    });
            }
            catch (SHAMapMissingNode const& e)
            {
                JLOG(journal_.error())
                    << "Missing node while copying ledger before rotate: " << e.what();
                continue;
            }

            switch (healthWait())
            {
                case HealthResult::Stopping:
                    return;
                case HealthResult::Expired:
                    continue;
                case HealthResult::KeepGoing:
                    break;
            }
            {
                // Only log if we completed without a "health" abort
                auto const copyDuplications = dbRotating_->getAndResetDuplicationCount();
                JLOG(journal_.debug()) << "copied ledger " << validatedSeq << " duplicated "
                                       << copyDuplications << " / " << nodeCount << " nodes";
            }

            JLOG(journal_.debug()) << "freshening caches";
            freshenCaches();
            switch (healthWait())
            {
                case HealthResult::Stopping:
                    return;
                case HealthResult::Expired:
                    continue;
                case HealthResult::KeepGoing:
                    break;
            }
            // Only log if we completed without a "health" abort
            JLOG(journal_.debug()) << validatedSeq << " freshened caches";

            JLOG(journal_.trace()) << "Making a new backend";
            auto newBackend = makeBackendRotating();
            JLOG(journal_.debug()) << validatedSeq << " new backend " << newBackend->getName();

            clearCaches(validatedSeq);
            switch (healthWait())
            {
                case HealthResult::Stopping:
                    return;
                case HealthResult::Expired:
                    continue;
                case HealthResult::KeepGoing:
                    break;
            }

            lastRotated = validatedSeq;

            dbRotating_->rotate(
                std::move(newBackend),
                [&](std::string const& writableName, std::string const& archiveName) {
                    SavedState savedState;
                    savedState.writableDb = writableName;
                    savedState.archiveDb = archiveName;
                    savedState.lastRotated = lastRotated;
                    stateDb_.setState(savedState);

                    clearCaches(validatedSeq);
                });

            auto const currentValidatedSeq = ledgerMaster_->getValidLedgerIndex();
            auto const processingDiff = currentValidatedSeq - validatedSeq;
            JLOG(journal_.warn())
                << "FINISHED ROTATION: validatedSeq: " << validatedSeq
                << ", lastRotated: " << lastRotated << " diff " << diff
                << ". Updated validated seq is " << currentValidatedSeq << ", " << processingDiff
                << " ledgers were validated during the rotation processs. Complete ledgers: "
                << ledgerMaster_->getCompleteLedgers();
        }
    }
}

void
SHAMapStoreImp::dbPaths()
{
    Section const section{app_.config().section(Sections::kNodeDatabase)};

    // Skip creating the directory when an in-memory database is used.
    if (boost::iequals(get(section, Keys::kType), "memory"))
        return;

    std::filesystem::path dbPath = get(section, Keys::kPath);
    if (std::filesystem::exists(dbPath))
    {
        if (!std::filesystem::is_directory(dbPath))
        {
            journal_.error() << "node db path must be a directory. " << dbPath.string();
            Throw<std::runtime_error>("node db path must be a directory.");
        }
    }
    else
    {
        std::filesystem::create_directories(dbPath);
    }

    SavedState state = stateDb_.getState();

    {
        auto update = [&dbPath](std::string& sPath) {
            if (sPath.empty())
                return false;

            // Check if configured "path" matches stored directory path
            using namespace std::filesystem;
            auto const stored{std::filesystem::path(sPath)};
            if (stored.parent_path() == dbPath)
                return false;

            sPath = (dbPath / stored.filename()).string();
            return true;
        };

        if (update(state.writableDb))
        {
            update(state.archiveDb);
            stateDb_.setState(state);
        }
    }

    bool writableDbExists = false;
    bool archiveDbExists = false;

    std::vector<std::filesystem::path> pathsToDelete;
    for (std::filesystem::directory_iterator it(dbPath);
         it != std::filesystem::directory_iterator();
         ++it)
    {
        if (state.writableDb == it->path().string())
        {
            writableDbExists = true;
        }
        else if (state.archiveDb == it->path().string())
        {
            archiveDbExists = true;
        }
        else if (dbPrefix_ == it->path().stem().string())
        {
            pathsToDelete.push_back(it->path());
        }
    }

    if ((!writableDbExists && !state.writableDb.empty()) ||
        (!archiveDbExists && !state.archiveDb.empty()) || (writableDbExists != archiveDbExists) ||
        state.writableDb.empty() != state.archiveDb.empty())
    {
        std::filesystem::path stateDbPathName = app_.config().legacy(Sections::kDatabasePath);
        stateDbPathName /= dbName_;
        stateDbPathName += "*";

        journal_.error() << "state db error:\n"
                         << "  writableDbExists " << writableDbExists << " archiveDbExists "
                         << archiveDbExists << '\n'
                         << "  writableDb '" << state.writableDb << "' archiveDb '"
                         << state.archiveDb << "\n\n"
                         << "The existing data is in a corrupted state.\n"
                         << "To resume operation, remove the files matching "
                         << stateDbPathName.string() << " and contents of the directory "
                         << get(section, Keys::kPath) << '\n'
                         << "Optionally, you can move those files to another\n"
                         << "location if you wish to analyze or back up the data.\n"
                         << "However, there is no guarantee that the data in its\n"
                         << "existing form is usable.";

        Throw<std::runtime_error>("state db error");
    }

    // The necessary directories exist. Now, remove any others.
    for (std::filesystem::path const& p : pathsToDelete)
        std::filesystem::remove_all(p);
}

std::unique_ptr<node_store::Backend>
SHAMapStoreImp::makeBackendRotating(std::string path)
{
    Section section{app_.config().section(Sections::kNodeDatabase)};
    std::filesystem::path newPath;

    if (!path.empty())
    {
        newPath = path;
    }
    else
    {
        newPath = uniqueRandomPath(get(section, Keys::kPath), dbPrefix_ + ".");
    }
    section.set(Keys::kPath, newPath.string());

    auto backend{node_store::Manager::instance().makeBackend(
        section,
        megabytes(app_.config().getValueFor(SizedItem::BurstSize, std::nullopt)),
        scheduler_,
        app_.getJournal(kNodeStoreName))};
    backend->open();
    return backend;
}

void
SHAMapStoreImp::clearSql(
    LedgerIndex lastRotated,
    std::string const& tableName,
    std::function<std::optional<LedgerIndex>()> const& getMinSeq,
    std::function<void(LedgerIndex)> const& deleteBeforeSeq)
{
    XRPL_ASSERT(deleteInterval_, "xrpl::SHAMapStoreImp::clearSql : nonzero delete interval");
    LedgerIndex min = std::numeric_limits<LedgerIndex>::max();

    {
        JLOG(journal_.trace()) << "Begin: Look up lowest value of: " << tableName;
        auto m = getMinSeq();
        JLOG(journal_.trace()) << "End: Look up lowest value of: " << tableName;
        if (!m)
            return;
        min = *m;
    }

    if (min > lastRotated || healthWait() != HealthResult::KeepGoing)
        return;
    if (min == lastRotated)
    {
        // Micro-optimization mainly to clarify logs
        JLOG(journal_.trace()) << "Nothing to delete from " << tableName;
        return;
    }

    JLOG(journal_.debug()) << "start deleting in: " << tableName << " from " << min << " to "
                           << lastRotated;
    while (min < lastRotated)
    {
        // The very first sleep is, arguably wasted, but clearSql is called multiple times for
        // different tables, so the time is amortized among all the operations. This results in
        // a backoff in between each set of tables, too.
        std::this_thread::sleep_for(backOff_);
        if (healthWait() != HealthResult::KeepGoing)
            return;

        min = std::min(lastRotated, min + deleteBatch_);
        JLOG(journal_.trace()) << "Begin: Delete up to " << deleteBatch_
                               << " rows with LedgerSeq < " << min << " from: " << tableName;
        deleteBeforeSeq(min);
        JLOG(journal_.trace()) << "End: Delete up to " << deleteBatch_ << " rows with LedgerSeq < "
                               << min << " from: " << tableName;
    }
    JLOG(journal_.debug()) << "finished deleting from: " << tableName;
}

void
SHAMapStoreImp::clearCaches(LedgerIndex validatedSeq)
{
    ledgerMaster_->clearLedgerCachePrior(validatedSeq);
    // Also clear the FullBelowCache so its generation counter is bumped.
    // This prevents stale "full below" markers from persisting across
    // backend rotation/online deletion and interfering with SHAMap sync.
    fullBelowCache_->clear();
}

void
SHAMapStoreImp::freshenCaches()
{
    if (freshenCache(*treeNodeCache_))
        return;
    freshenCache(app_.getMasterTransaction().getCache());
}

void
SHAMapStoreImp::clearPrior(LedgerIndex lastRotated)
{
    // Do not allow ledgers to be acquired from the network
    // that are about to be deleted.
    minimumOnline_ = lastRotated + 1;
    JLOG(journal_.trace()) << "Begin: Clear internal ledgers up to " << lastRotated;
    ledgerMaster_->clearPriorLedgers(lastRotated);
    JLOG(journal_.trace()) << "End: Clear internal ledgers up to " << lastRotated;
    if (healthWait() != HealthResult::KeepGoing)
        return;

    auto& db = app_.getRelationalDatabase();

    clearSql(
        lastRotated,
        "Ledgers",
        [&db]() -> std::optional<LedgerIndex> { return db.getMinLedgerSeq(); },
        [&db](LedgerIndex min) -> void { db.deleteBeforeLedgerSeq(min); });
    if (healthWait() != HealthResult::KeepGoing)
        return;

    if (!app_.config().useTxTables())
        return;

    clearSql(
        lastRotated,
        "Transactions",
        [&db]() -> std::optional<LedgerIndex> { return db.getTransactionsMinLedgerSeq(); },
        [&db](LedgerIndex min) -> void { db.deleteTransactionsBeforeLedgerSeq(min); });
    if (healthWait() != HealthResult::KeepGoing)
        return;

    clearSql(
        lastRotated,
        "AccountTransactions",
        [&db]() -> std::optional<LedgerIndex> { return db.getAccountTransactionsMinLedgerSeq(); },
        [&db](LedgerIndex min) -> void { db.deleteAccountTransactionsBeforeLedgerSeq(min); });
    if (healthWait() != HealthResult::KeepGoing)
        return;
}

SHAMapStoreImp::HealthResult
SHAMapStoreImp::healthWait()
{
    // Gets the current status of the server from ledgerMaster_ and netOPs_. Must be called
    // while mutex_ is unlocked to avoid unlikely, but possible, deadlock with ledgerMaster_'s
    // completeLock_.
    // Releasing the lock may mean that status will be slightly out of date when the lock is
    // reacquired, but it's close enough. In a normal rotation, healthWait() is called frequently,
    // so a false positive will be detected on the next call, and a false negative will be detected
    // in the next loop iteration. Database rotation is important, but not timely, so an extra
    // delay is fine.
    auto readServerStatus = [this](
                                LedgerIndex& index,
                                bool& buildingIndex,
                                std::chrono::seconds& age,
                                OperatingMode& mode,
                                std::size_t& numMissing,
                                LedgerIndex const lowerBound,
                                ScopeUnlock<decltype(mutex_)> const&) {
        index = ledgerMaster_->getValidLedgerIndex();
        bool const haveIndex = ledgerMaster_->haveLedger(index);
        age = ledgerMaster_->getValidatedLedgerAge();
        mode = netOPs_->getOperatingMode();

        numMissing =
            lowerBound == 0 ? 0 : ledgerMaster_->missingFromCompleteLedgerRange(lowerBound, index);

        buildingIndex = (numMissing == 1 && !haveIndex);
    };
    // Tracked server status properties
    LedgerIndex index = 0;
    bool buildingIndex = false;
    std::chrono::seconds age;
    OperatingMode mode = OperatingMode::DISCONNECTED;
    std::size_t numMissing = 0;

    std::unique_lock lock(mutex_);

    auto const waitTime = recoveryWaitTime_;
    auto const ageThreshold = ageThreshold_;
    {
        auto const lowerBound = lastGoodValidatedLedger_;

        ScopeUnlock const unlock(lock);

        readServerStatus(index, buildingIndex, age, mode, numMissing, lowerBound, unlock);
    }
    // If index gets past this point without the health check succeeding, return
    // HealthWait::Expired. This depends on index being initialized, so it must be after
    // readServerStatus().
    auto const lastSuccess = lastSuccessfulHealthCheck_ == 0 ? index : lastSuccessfulHealthCheck_;
    auto const circuitBreaker = lastSuccess + maxWaitingLedgers_;

    auto healthy = [&] {
        // Special case: If the server is disconnected, it's not doing any ledger I/O, because
        // it's focused on trying to get peers. A disconnected state is should never be caused by
        // the activity of the server. It's usually limited to hardware or connectivity issues. Take
        // advantage of that to run as much rotation I/O as possible before it comes back online.
        if (mode == OperatingMode::DISCONNECTED)
            return true;
        if (age > ageThreshold)
            return false;
        if (numMissing > 0)
            return false;
        if (mode != OperatingMode::FULL)
            return false;
        return true;
    };

    while (!stop_ && !healthy() && index < circuitBreaker)
    {
        // Future-proofing: this value shouldn't change while we are sleeping, but grab it while we
        // have the lock in case it does.
        auto const lowerBound = lastGoodValidatedLedger_;

        ScopeUnlock const unlock(lock);

        auto const [stream, waitMs] = std::invoke(
            [mode, age, ageThreshold, buildingIndex, waitTime, index, lastSuccess, this]
            -> std::pair<beast::Journal::Stream, std::chrono::milliseconds> {
                if (mode != OperatingMode::FULL || age > ageThreshold ||
                    (index - lastSuccess > maxWaitingLedgers_ / 4))
                    return {journal_.warn(), waitTime};
                if (buildingIndex)
                {
                    // We expect this ledger to be built soon, so log at a lower level, and don't
                    // wait as long.
                    return {
                        journal_.trace(),
                        std::chrono::duration_cast<std::chrono::milliseconds>(waitTime) / 10};
                }
                return {journal_.info(), waitTime};
            });
        JLOG(stream) << "Waiting " << waitMs.count() << "ms for node to stabilize. state: "
                     << app_.getOPs().strOperatingMode(mode, false) << ". age " << age.count()
                     << "s. Missing ledgers: " << numMissing << ". Expect: " << lowerBound << "-"
                     << index << ". Complete ledgers: " << ledgerMaster_->getCompleteLedgers();
        std::this_thread::sleep_for(waitMs);

        [[maybe_unused]]
        LedgerIndex const lastLedger = index;
        readServerStatus(index, buildingIndex, age, mode, numMissing, lowerBound, unlock);
        SOMETIMES(
            index > lastLedger, "SHAMapStoreImp::healthWait : validated ledger index changed");
    }

    auto const result = std::invoke([index, circuitBreaker, this]() -> HealthResult {
        if (stop_)
            return HealthResult::Stopping;
        if (index < circuitBreaker)
            return HealthResult::KeepGoing;
        JLOG(journal_.error()) << "online_delete rotation has been unable to make progress for "
                               << maxWaitingLedgers_ << " ledgers. "
                               << "validated ledger index: " << index
                               << ", last successful health check index: "
                               << lastSuccessfulHealthCheck_
                               << ", circuit breaker index: " << circuitBreaker;
        return HealthResult::Expired;
    });

    XRPL_ASSERT(lock.owns_lock(), "SHAMapStoreImp::healthWait : lock held");
    if (result == HealthResult::KeepGoing)
        lastSuccessfulHealthCheck_ = index;

    return result;
}

void
SHAMapStoreImp::stop()
{
    if (thread_.joinable())
    {
        {
            std::scoped_lock const lock(mutex_);
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
    if ((deleteInterval_ != 0u) && (minimumOnline_ != 0u))
        return minimumOnline_.load();
    return app_.getLedgerMaster().minSqlSeq();
}

//------------------------------------------------------------------------------

std::unique_ptr<SHAMapStore>
makeSHAMapStore(Application& app, node_store::Scheduler& scheduler, beast::Journal journal)
{
    return std::make_unique<SHAMapStoreImp>(app, scheduler, journal);
}

}  // namespace xrpl
