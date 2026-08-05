#include <xrpld/app/misc/SHAMapStoreImp.h>

#include <xrpld/app/ledger/TransactionMaster.h>
#include <xrpld/app/misc/SHAMapStore.h>
#include <xrpld/app/rdb/backend/SQLiteDatabase.h>
#include <xrpld/core/Config.h>

#include <xrpl/basics/ByteUtilities.h>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/core/CurrentThreadName.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/config/BasicConfig.h>
#include <xrpl/config/Constants.h>
#include <xrpl/ledger/Ledger.h>
#include <xrpl/nodestore/Backend.h>
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
#include <boost/filesystem/directory.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
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
        // Configuration that affects the behavior of online delete
        getIfExists(section, Keys::kDeleteBatch, deleteBatch_);
        getIfExists(section, Keys::kOnlineDeleteGenerations, numGenerations_);
        // A ring needs at least a writable generation plus one archive (the historical
        // two-backend behavior); fewer would drop data still referenced by the network.
        // The upper bound caps file-descriptor and directory growth.
        numGenerations_ = std::clamp(numGenerations_, kMinGenerations, kMaxGenerations);
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

        getIfExists(section, Keys::kAdvisoryDelete, advisoryDelete_);

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

        // Open the generation ring, oldest -> newest. New nodes are written to the
        // newest (writable) generation; reads probe newest -> oldest.
        std::vector<std::shared_ptr<node_store::Backend>> generations;
        if (state.generations.empty())
        {
            // First run: bootstrap a two-generation ring (a sealed archive plus an
            // empty writable), matching the historical initial two-backend state.
            auto archive = makeBackendRotating();
            auto writable = makeBackendRotating();
            state.archiveDb = archive->getName();
            state.writableDb = writable->getName();
            state.generations = {archive->getName(), writable->getName()};
            stateDb_.setState(state);
            generations.emplace_back(std::move(archive));
            generations.emplace_back(std::move(writable));
        }
        else
        {
            for (auto const& name : state.generations)
                generations.emplace_back(makeBackendRotating(name));
        }

        // Create the rotating NodeStore over the generation ring to allow online
        // deletion of data.
        auto dbr = std::make_unique<node_store::DatabaseRotatingImp>(
            scheduler_,
            readThreads,
            std::move(generations),
            nscfg,
            app_.getJournal(kNodeStoreName));
        fdRequired_ += dbr->fdRequired();
        // The ring grows to numGenerations_ (+1 transiently between advance and retire),
        // so budget descriptors for the full ring, not just the generations open at boot.
        if (auto const bootCount = dbr->generationCount();
            bootCount > 0 && numGenerations_ + 1 > bootCount)
        {
            fdRequired_ += static_cast<int>(dbr->fdRequired() / bootCount) *
                static_cast<int>(numGenerations_ + 1 - bootCount);
        }
        dbRotating_ = dbr.get();
        JLOG(journal_.warn()) << "online_delete generation ring: opened " << dbr->generationCount()
                              << " generations, budget " << numGenerations_
                              << " (retire when above budget)";
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
SHAMapStoreImp::copyNode(
    std::uint64_t& nodeCount,
    SHAMapTreeNode const& node,
    NodeObjectType rescueType)
{
    // Copy a single record from node to dbRotating_
    auto obj = dbRotating_->fetchNodeObject(
        node.getHash().asUInt256(), 0, node_store::FetchType::Synchronous, true);
    if (!obj)
    {
        XRPL_ASSERT(node.cowid() == 0, "SHAMapStoreImp::copyNode : rescued node must be clean");
        // Reachable from the walked map in memory, but present in no
        // generation: its only on-disk copy lived in a backend removed by
        // an earlier rotation, and it was never rewritten because it is clean
        // (cowid == 0, so flushDirty skips it). Persist the in-memory body
        // directly into the writable backend so it survives this rotation
        // instead of later surfacing as an unresolvable SHAMapMissingNode.
        auto const hash = node.getHash().asUInt256();
        Serializer s;
        node.serializeWithPrefix(s);
        dbRotating_->store(rescueType, std::move(s.modData()), hash, 0);
        JLOG(journal_.warn()) << "copyNode: re-stored node missing from both backends, hash="
                              << hash << " type=" << static_cast<int>(node.getType());
    }
    if ((++nodeCount % checkHealthInterval_) == 0u)
    {
        if (healthWait() == HealthResult::Stopping)
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

        bool const readyToRotate = validatedSeq >= lastRotated + deleteInterval_ &&
            canDelete_ >= lastRotated - 1 && healthWait() == HealthResult::KeepGoing;

        // will delete up to (not including) lastRotated
        if (readyToRotate)
        {
            JLOG(journal_.warn()) << "rotating  validatedSeq " << validatedSeq << " lastRotated "
                                  << lastRotated << " deleteInterval " << deleteInterval_
                                  << " canDelete_ " << canDelete_ << " state "
                                  << app_.getOPs().strOperatingMode(false) << " age "
                                  << ledgerMaster_->getValidatedLedgerAge().count() << 's';

            clearPrior(lastRotated);
            if (healthWait() == HealthResult::Stopping)
                return;

            // Persist the whole generation ring durably. archiveDb/writableDb are kept
            // in sync with the ring ends so an older two-backend build could still boot.
            auto const persistRing = [&](std::vector<std::string> const& generations) {
                SavedState savedState;
                savedState.generations = generations;
                savedState.archiveDb = generations.empty() ? std::string() : generations.front();
                savedState.writableDb = generations.empty() ? std::string() : generations.back();
                savedState.lastRotated = lastRotated;
                stateDb_.setState(savedState);
            };

            // Oldest ledger still retained after clearPrior. Its state map is the second
            // evacuation root: it anchors node versions that current state no longer
            // references but retained historical ledgers still do.
            LedgerIndex const oldestRetained = lastRotated;
            lastRotated = validatedSeq;

            // Seal the current writable generation and open a fresh empty one. This is
            // O(1): new nodes now accumulate in the new generation, and the whole live
            // set is NOT re-stored (unlike the old full-state copy).
            JLOG(journal_.trace()) << "Making a new writable generation";
            auto newBackend = makeBackendRotating();
            JLOG(journal_.debug())
                << validatedSeq << " new writable generation " << newBackend->getName();
            dbRotating_->advance(std::move(newBackend), persistRing);

            // Once the ring exceeds its generation budget, retire oldest generations:
            // evacuate only their still-live nodes into the writable backend, then drop
            // them. This is the O(churn) replacement for the old O(total state)
            // copy-on-rotate. Loop until back at budget: normally one generation, but an
            // earlier rotation interrupted between advance and retire leaves extras, and a
            // single retire per rotation would never shrink the ring back (each rotation's
            // advance adds one).
            JLOG(journal_.warn()) << "rotation " << validatedSeq << ": ring has "
                                  << dbRotating_->generationCount() << " generations, budget "
                                  << numGenerations_
                                  << (dbRotating_->generationCount() > numGenerations_
                                          ? " -> retiring oldest"
                                          : " -> below budget, no retirement");
            while (dbRotating_->generationCount() > numGenerations_)
            {
                // RAII: close the retire window on any early return / exception, so a
                // read is never copied forward from a generation we are no longer dropping.
                struct RetireGuard
                {
                    node_store::DatabaseRotating& db;
                    ~RetireGuard()
                    {
                        db.endRetire();
                    }
                };
                RetireGuard const retireGuard{*dbRotating_};
                dbRotating_->beginRetire();

                // Copy forward the retiring generation's survivors. copyNode fetches each
                // visited node; the scoped copy-forward re-stores only those served by the
                // retiring generation (the cold survivors), not the whole live set. Two
                // evacuation roots cover every version a retained ledger can reference:
                // the current state, and the oldest retained ledger's state. A version
                // referenced only by a ledger strictly between the two was created inside
                // the retention window, so it lives in the newest generations, never the
                // retiring one. Anything in the retiring generation reachable from neither
                // root is dead.
                JLOG(journal_.warn())
                    << "evacuating retiring generation for ledger " << validatedSeq;
                std::uint64_t nodeCount = 0;
                bool evacuationComplete = true;
                auto const evacuate =
                    [&](SHAMap const& map, LedgerIndex seq, NodeObjectType rescueType) {
                        // An empty map (zero root hash, e.g. the transaction tree of a
                        // no-transaction ledger) has nothing on disk to evacuate.
                        if (map.getHash().isZero())
                            return;
                        try
                        {
                            map.snapShot(false)->visitNodes(
                                [this, &nodeCount, rescueType](SHAMapTreeNode const& node) {
                                    return copyNode(nodeCount, node, rescueType);
                                });
                        }
                        catch (SHAMapMissingNode const& e)
                        {
                            // A node absent from every generation AND memory is already lost:
                            // it cannot be recovered whether or not we retire, so aborting
                            // retirement only leaks the ring forever (the observed failure
                            // mode). Log it, preserve everything reachable + cached
                            // (freshenCaches below), and still drop the oldest generation so
                            // the ring stays bounded.
                            evacuationComplete = false;
                            JLOG(journal_.warn())
                                << "evacuation incomplete for ledger " << seq << " after "
                                << nodeCount
                                << " nodes -- a node is already lost (retiring anyway to keep "
                                << "the ring bounded): " << e.what();
                        }
                    };
                evacuate(validatedLedger->stateMap(), validatedSeq, NodeObjectType::AccountNode);
                if (healthWait() == HealthResult::Stopping)
                    return;
                if (oldestRetained != validatedSeq)
                {
                    if (auto const boundary = ledgerMaster_->getLedgerBySeq(oldestRetained))
                    {
                        evacuate(boundary->stateMap(), oldestRetained, NodeObjectType::AccountNode);
                        // The boundary ledger's transaction tree was written at its own
                        // close, which lands in the retiring generation when the budget
                        // is at its minimum; it is referenced by a retained ledger, so it
                        // must survive too.
                        evacuate(
                            boundary->txMap(), oldestRetained, NodeObjectType::TransactionNode);
                    }
                    else
                    {
                        JLOG(journal_.warn())
                            << "evacuation: oldest retained ledger " << oldestRetained
                            << " unavailable; reads of retained ledgers below " << validatedSeq
                            << " may miss superseded nodes";
                    }
                }

                if (healthWait() == HealthResult::Stopping)
                    return;
                JLOG(journal_.warn()) << "evacuated ledger " << validatedSeq << " nodecount "
                                      << nodeCount << (evacuationComplete ? "" : " (INCOMPLETE)");

                // Any hot node still living only in the retiring generation is re-stored
                // into the writable backend via the same scoped copy-forward.
                JLOG(journal_.debug()) << "freshening caches";
                freshenCaches();
                if (healthWait() == HealthResult::Stopping)
                    return;

                // Invalidate FullBelow / ledger caches before the drop so nothing resolves
                // to the removed backend.
                clearCaches(validatedSeq);
                if (healthWait() == HealthResult::Stopping)
                    return;

                auto const evacuated = dbRotating_->copyForwardCount();
                dbRotating_->retireOldest(persistRing);
                clearCaches(validatedSeq);
                JLOG(journal_.warn()) << "retired oldest generation for ledger " << validatedSeq
                                      << ": evacuated " << evacuated << " live nodes, ring now "
                                      << dbRotating_->generationCount() << " generations";
            }

            JLOG(journal_.warn()) << "finished rotation " << validatedSeq;
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

    boost::filesystem::path dbPath = get(section, Keys::kPath);
    if (boost::filesystem::exists(dbPath))
    {
        if (!boost::filesystem::is_directory(dbPath))
        {
            journal_.error() << "node db path must be a directory. " << dbPath.string();
            Throw<std::runtime_error>("node db path must be a directory.");
        }
    }
    else
    {
        boost::filesystem::create_directories(dbPath);
    }

    SavedState state = stateDb_.getState();

    {
        // If the configured node_db "path" changed, rewrite every stored generation
        // name (and the legacy pair) to point at the new directory, keeping filenames.
        using namespace boost::filesystem;
        bool changed = false;
        auto relocate = [&dbPath, &changed](std::string& sPath) {
            if (sPath.empty())
                return;
            auto const stored{path(sPath)};
            if (stored.parent_path() == dbPath)
                return;
            sPath = (dbPath / stored.filename()).string();
            changed = true;
        };

        for (auto& name : state.generations)
            relocate(name);
        relocate(state.writableDb);
        relocate(state.archiveDb);
        if (changed)
            stateDb_.setState(state);
    }

    // Every generation named in the ring must exist on disk. Any other directory whose
    // stem is the backend prefix is an orphan (e.g. a generation whose directory was
    // created but never persisted into the ring before a crash) and is removed.
    std::size_t generationsFound = 0;
    std::vector<boost::filesystem::path> pathsToDelete;
    for (boost::filesystem::directory_iterator it(dbPath);
         it != boost::filesystem::directory_iterator();
         ++it)
    {
        auto const name = it->path().string();
        if (std::ranges::find(state.generations, name) != state.generations.end())
        {
            ++generationsFound;
        }
        else if (dbPrefix_ == it->path().stem().string())
        {
            pathsToDelete.push_back(it->path());
        }
    }

    if (generationsFound != state.generations.size())
    {
        boost::filesystem::path stateDbPathName = app_.config().legacy(Sections::kDatabasePath);
        stateDbPathName /= dbName_;
        stateDbPathName += "*";

        journal_.error() << "state db error:\n"
                         << "  generations expected " << state.generations.size() << " found "
                         << generationsFound << "\n\n"
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
    for (boost::filesystem::path const& p : pathsToDelete)
        boost::filesystem::remove_all(p);
}

std::unique_ptr<node_store::Backend>
SHAMapStoreImp::makeBackendRotating(std::string path)
{
    Section section{app_.config().section(Sections::kNodeDatabase)};
    boost::filesystem::path newPath;

    if (!path.empty())
    {
        newPath = path;
    }
    else
    {
        boost::filesystem::path p = get(section, Keys::kPath);
        p /= dbPrefix_;
        p += ".%%%%";
        newPath = boost::filesystem::unique_path(p);
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

    if (min > lastRotated || healthWait() == HealthResult::Stopping)
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
        min = std::min(lastRotated, min + deleteBatch_);
        JLOG(journal_.trace()) << "Begin: Delete up to " << deleteBatch_
                               << " rows with LedgerSeq < " << min << " from: " << tableName;
        deleteBeforeSeq(min);
        JLOG(journal_.trace()) << "End: Delete up to " << deleteBatch_ << " rows with LedgerSeq < "
                               << min << " from: " << tableName;
        if (healthWait() == HealthResult::Stopping)
            return;
        if (min < lastRotated)
            std::this_thread::sleep_for(backOff_);
        if (healthWait() == HealthResult::Stopping)
            return;
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
    if (freshenCache(app_.getMasterTransaction().getCache()))
        return;
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
    if (healthWait() == HealthResult::Stopping)
        return;

    auto& db = app_.getRelationalDatabase();

    clearSql(
        lastRotated,
        "Ledgers",
        [&db]() -> std::optional<LedgerIndex> { return db.getMinLedgerSeq(); },
        [&db](LedgerIndex min) -> void { db.deleteBeforeLedgerSeq(min); });
    if (healthWait() == HealthResult::Stopping)
        return;

    if (!app_.config().useTxTables())
        return;

    clearSql(
        lastRotated,
        "Transactions",
        [&db]() -> std::optional<LedgerIndex> { return db.getTransactionsMinLedgerSeq(); },
        [&db](LedgerIndex min) -> void { db.deleteTransactionsBeforeLedgerSeq(min); });
    if (healthWait() == HealthResult::Stopping)
        return;

    clearSql(
        lastRotated,
        "AccountTransactions",
        [&db]() -> std::optional<LedgerIndex> { return db.getAccountTransactionsMinLedgerSeq(); },
        [&db](LedgerIndex min) -> void { db.deleteAccountTransactionsBeforeLedgerSeq(min); });
    if (healthWait() == HealthResult::Stopping)
        return;
}

SHAMapStoreImp::HealthResult
SHAMapStoreImp::healthWait()
{
    auto age = ledgerMaster_->getValidatedLedgerAge();
    OperatingMode mode = netOPs_->getOperatingMode();
    std::unique_lock lock(mutex_);
    while (!stop_ && (mode != OperatingMode::FULL || age > ageThreshold_))
    {
        lock.unlock();
        JLOG(journal_.warn()) << "Waiting " << recoveryWaitTime_.count()
                              << "s for node to stabilize. state: "
                              << app_.getOPs().strOperatingMode(mode, false) << ". age "
                              << age.count() << 's';
        std::this_thread::sleep_for(recoveryWaitTime_);
        age = ledgerMaster_->getValidatedLedgerAge();
        mode = netOPs_->getOperatingMode();
        lock.lock();
    }

    return stop_ ? HealthResult::Stopping : HealthResult::KeepGoing;
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
