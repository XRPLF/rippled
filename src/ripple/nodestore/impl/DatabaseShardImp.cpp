//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2017 Ripple Labs Inc.

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

#include <ripple/app/ledger/InboundLedgers.h>
#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/basics/ByteUtilities.h>
#include <ripple/basics/chrono.h>
#include <ripple/basics/random.h>
#include <ripple/core/ConfigSections.h>
#include <ripple/nodestore/DummyScheduler.h>
#include <ripple/nodestore/impl/DatabaseShardImp.h>
#include <ripple/overlay/Overlay.h>
#include <ripple/overlay/predicates.h>
#include <ripple/protocol/HashPrefix.h>

#include <boost/algorithm/string/predicate.hpp>

#if BOOST_OS_LINUX
#include <sys/statvfs.h>
#endif

namespace ripple {
namespace NodeStore {

DatabaseShardImp::DatabaseShardImp(
    Application& app,
    Stoppable& parent,
    std::string const& name,
    Scheduler& scheduler,
    int readThreads,
    beast::Journal j)
    : DatabaseShard(
          name,
          parent,
          scheduler,
          readThreads,
          app.config().section(ConfigSection::shardDatabase()),
          j)
    , app_(app)
    , parent_(parent)
    , taskQueue_(std::make_unique<TaskQueue>(*this))
    , earliestShardIndex_(seqToShardIndex(earliestLedgerSeq()))
    , avgShardFileSz_(ledgersPerShard_ * kilobytes(192ull))
    , openFinalLimit_(
          app.config().getValueFor(SizedItem::openFinalLimit, boost::none))
{
}

bool
DatabaseShardImp::init()
{
    {
        std::lock_guard lock(mutex_);
        if (init_)
        {
            JLOG(j_.error()) << "already initialized";
            return false;
        }

        if (!initConfig(lock))
        {
            JLOG(j_.error()) << "invalid configuration file settings";
            return false;
        }

        try
        {
            using namespace boost::filesystem;

            // Consolidate the main storage path and all historical paths
            std::vector<path> paths{dir_};
            paths.insert(
                paths.end(), historicalPaths_.begin(), historicalPaths_.end());

            for (auto const& path : paths)
            {
                if (exists(path))
                {
                    if (!is_directory(path))
                    {
                        JLOG(j_.error()) << path << " must be a directory";
                        return false;
                    }
                }
                else if (!create_directories(path))
                {
                    JLOG(j_.error())
                        << "failed to create path: " + path.string();
                    return false;
                }
            }

            if (!app_.config().standalone() && !historicalPaths_.empty())
            {
                // Check historical paths for duplicated file systems
                if (!checkHistoricalPaths())
                    return false;
            }

            ctx_ = std::make_unique<nudb::context>();
            ctx_->start();

            // Find shards
            std::uint32_t openFinals{0};
            for (auto const& path : paths)
            {
                for (auto const& it : directory_iterator(path))
                {
                    // Ignore files
                    if (!is_directory(it))
                        continue;

                    // Ignore nonnumerical directory names
                    auto const shardDir{it.path()};
                    auto dirName{shardDir.stem().string()};
                    if (!std::all_of(
                            dirName.begin(), dirName.end(), [](auto c) {
                                return ::isdigit(static_cast<unsigned char>(c));
                            }))
                    {
                        continue;
                    }

                    // Ignore values below the earliest shard index
                    auto const shardIndex{std::stoul(dirName)};
                    if (shardIndex < earliestShardIndex())
                    {
                        JLOG(j_.debug())
                            << "shard " << shardIndex
                            << " ignored, comes before earliest shard index "
                            << earliestShardIndex();
                        continue;
                    }

                    // Check if a previous import failed
                    if (is_regular_file(shardDir / importMarker_))
                    {
                        JLOG(j_.warn())
                            << "shard " << shardIndex
                            << " previously failed import, removing";
                        remove_all(shardDir);
                        continue;
                    }

                    auto shard{std::make_shared<Shard>(
                        app_, *this, shardIndex, shardDir.parent_path(), j_)};
                    if (!shard->init(scheduler_, *ctx_))
                    {
                        // Remove corrupted or legacy shard
                        shard->removeOnDestroy();
                        JLOG(j_.warn())
                            << "shard " << shardIndex << " removed, "
                            << (shard->isLegacy() ? "legacy" : "corrupted")
                            << " shard";
                        continue;
                    }

                    switch (shard->getState())
                    {
                        case Shard::final:
                            if (++openFinals > openFinalLimit_)
                                shard->tryClose();
                            shards_.emplace(shardIndex, std::move(shard));
                            break;

                        case Shard::complete:
                            finalizeShard(
                                shards_.emplace(shardIndex, std::move(shard))
                                    .first->second,
                                true,
                                boost::none);
                            break;

                        case Shard::acquire:
                            if (acquireIndex_ != 0)
                            {
                                JLOG(j_.error())
                                    << "more than one shard being acquired";
                                return false;
                            }

                            shards_.emplace(shardIndex, std::move(shard));
                            acquireIndex_ = shardIndex;
                            break;

                        default:
                            JLOG(j_.error())
                                << "shard " << shardIndex << " invalid state";
                            return false;
                    }
                }
            }
        }
        catch (std::exception const& e)
        {
            JLOG(j_.fatal()) << "Exception caught in function " << __func__
                             << ". Error: " << e.what();
            return false;
        }

        updateStatus(lock);
        setParent(parent_);
        init_ = true;
    }

    setFileStats();
    return true;
}

boost::optional<std::uint32_t>
DatabaseShardImp::prepareLedger(std::uint32_t validLedgerSeq)
{
    boost::optional<std::uint32_t> shardIndex;

    {
        std::lock_guard lock(mutex_);
        assert(init_);

        if (acquireIndex_ != 0)
        {
            if (auto const it{shards_.find(acquireIndex_)}; it != shards_.end())
                return it->second->prepare();

            // Should never get here
            assert(false);
            return boost::none;
        }

        if (!canAdd_)
            return boost::none;

        shardIndex = findAcquireIndex(validLedgerSeq, lock);
    }

    if (!shardIndex)
    {
        JLOG(j_.debug()) << "no new shards to add";
        {
            std::lock_guard lock(mutex_);
            canAdd_ = false;
        }
        return boost::none;
    }

    auto const pathDesignation = [this, shardIndex = *shardIndex]() {
        std::lock_guard lock(mutex_);
        return prepareForNewShard(shardIndex, numHistoricalShards(lock), lock);
    }();

    if (!pathDesignation)
        return boost::none;

    auto const needsHistoricalPath =
        *pathDesignation == PathDesignation::historical;

    auto shard = [this, shardIndex, needsHistoricalPath] {
        std::lock_guard lock(mutex_);
        return std::make_unique<Shard>(
            app_,
            *this,
            *shardIndex,
            (needsHistoricalPath ? chooseHistoricalPath(lock) : ""),
            j_);
    }();

    if (!shard->init(scheduler_, *ctx_))
        return boost::none;

    auto const ledgerSeq{shard->prepare()};
    {
        std::lock_guard lock(mutex_);
        shards_.emplace(*shardIndex, std::move(shard));
        acquireIndex_ = *shardIndex;
    }
    return ledgerSeq;
}

bool
DatabaseShardImp::prepareShard(std::uint32_t shardIndex)
{
    auto fail = [j = j_, shardIndex](std::string const& msg) {
        JLOG(j.error()) << "shard " << shardIndex << " " << msg;
        return false;
    };

    if (shardIndex < earliestShardIndex())
    {
        return fail(
            "comes before earliest shard index " +
            std::to_string(earliestShardIndex()));
    }

    // If we are synced to the network, check if the shard index is
    // greater or equal to the current or validated shard index.
    auto seqCheck = [&](std::uint32_t ledgerSeq) {
        if (ledgerSeq >= earliestLedgerSeq() &&
            shardIndex >= seqToShardIndex(ledgerSeq))
        {
            return fail("invalid index");
        }
        return true;
    };
    if (!seqCheck(app_.getLedgerMaster().getValidLedgerIndex() + 1) ||
        !seqCheck(app_.getLedgerMaster().getCurrentLedgerIndex()))
    {
        return fail("invalid index");
    }

    // Any shard earlier than the two most recent shards is a historical shard
    auto const isHistoricalShard{shardIndex < shardBoundaryIndex()};

    std::lock_guard lock(mutex_);
    assert(init_);

    if (!canAdd_)
        return fail("cannot be stored at this time");

    // Check shard count and available storage space
    if (isHistoricalShard && numHistoricalShards(lock) >= maxHistoricalShards_)
        return fail("maximum number of historical shards reached");

    if (!sufficientStorage(
            1,
            isHistoricalShard ? PathDesignation::historical
                              : PathDesignation::none,
            lock))
    {
        return fail("insufficient storage space available");
    }

    if (shards_.find(shardIndex) != shards_.end())
        return fail("already stored");
    if (!preparedIndexes_.emplace(shardIndex).second)
        return fail("already queued for import");

    return true;
}

void
DatabaseShardImp::removePreShard(std::uint32_t shardIndex)
{
    std::lock_guard lock(mutex_);
    assert(init_);

    preparedIndexes_.erase(shardIndex);
}

std::string
DatabaseShardImp::getPreShards()
{
    RangeSet<std::uint32_t> rs;
    {
        std::lock_guard lock(mutex_);
        assert(init_);

        for (auto const& shardIndex : preparedIndexes_)
            rs.insert(shardIndex);
    }

    if (rs.empty())
        return {};

    return to_string(rs);
};

bool
DatabaseShardImp::importShard(
    std::uint32_t shardIndex,
    boost::filesystem::path const& srcDir)
{
    auto fail = [&](std::string const& msg,
                    std::lock_guard<std::mutex> const& lock) {
        JLOG(j_.error()) << "shard " << shardIndex << " " << msg;

        // Remove the failed import shard index so it can be retried
        preparedIndexes_.erase(shardIndex);
        return false;
    };

    using namespace boost::filesystem;
    try
    {
        if (!is_directory(srcDir) || is_empty(srcDir))
        {
            return fail(
                "invalid source directory " + srcDir.string(),
                std::lock_guard(mutex_));
        }
    }
    catch (std::exception const& e)
    {
        return fail(
            std::string(". Exception caught in function ") + __func__ +
                ". Error: " + e.what(),
            std::lock_guard(mutex_));
    }

    auto const expectedHash{app_.getLedgerMaster().walkHashBySeq(
        lastLedgerSeq(shardIndex), InboundLedger::Reason::GENERIC)};
    if (!expectedHash)
        return fail("expected hash not found", std::lock_guard(mutex_));

    path dstDir;
    {
        std::lock_guard lock(mutex_);
        if (shards_.find(shardIndex) != shards_.end())
            return fail("already exists", lock);

        // Check shard was prepared for import
        if (preparedIndexes_.find(shardIndex) == preparedIndexes_.end())
            return fail("was not prepared for import", lock);

        auto const pathDesignation{
            prepareForNewShard(shardIndex, numHistoricalShards(lock), lock)};
        if (!pathDesignation)
            return fail("failed to import", lock);

        if (*pathDesignation == PathDesignation::historical)
            dstDir = chooseHistoricalPath(lock);
        else
            dstDir = dir_;
    }
    dstDir /= std::to_string(shardIndex);

    auto renameDir = [&](path const& src, path const& dst) {
        try
        {
            rename(src, dst);
        }
        catch (std::exception const& e)
        {
            return fail(
                std::string(". Exception caught in function ") + __func__ +
                    ". Error: " + e.what(),
                std::lock_guard(mutex_));
        }
        return true;
    };

    // Rename source directory to the shard database directory
    if (!renameDir(srcDir, dstDir))
        return false;

    // Create the new shard
    auto shard{std::make_unique<Shard>(
        app_, *this, shardIndex, dstDir.parent_path(), j_)};

    if (!shard->init(scheduler_, *ctx_) || shard->getState() != Shard::complete)
    {
        shard.reset();
        renameDir(dstDir, srcDir);
        return fail("failed to import", std::lock_guard(mutex_));
    }

    auto const [it, inserted] = [&]() {
        std::lock_guard lock(mutex_);
        preparedIndexes_.erase(shardIndex);
        return shards_.emplace(shardIndex, std::move(shard));
    }();

    if (!inserted)
    {
        shard.reset();
        renameDir(dstDir, srcDir);
        return fail("failed to import", std::lock_guard(mutex_));
    }

    finalizeShard(it->second, true, expectedHash);
    return true;
}

std::shared_ptr<Ledger>
DatabaseShardImp::fetchLedger(uint256 const& hash, std::uint32_t ledgerSeq)
{
    auto const shardIndex{seqToShardIndex(ledgerSeq)};
    {
        std::shared_ptr<Shard> shard;
        {
            std::lock_guard lock(mutex_);
            assert(init_);

            auto const it{shards_.find(shardIndex)};
            if (it == shards_.end())
                return nullptr;
            shard = it->second;
        }

        // Ledger must be stored in a final or acquiring shard
        switch (shard->getState())
        {
            case Shard::final:
                break;
            case Shard::acquire:
                if (shard->containsLedger(ledgerSeq))
                    break;
                [[fallthrough]];
            default:
                return nullptr;
        }
    }

    auto const nodeObject{Database::fetchNodeObject(hash, ledgerSeq)};
    if (!nodeObject)
        return nullptr;

    auto fail = [&](std::string const& msg) -> std::shared_ptr<Ledger> {
        JLOG(j_.error()) << "shard " << shardIndex << " " << msg;
        return nullptr;
    };

    auto ledger{std::make_shared<Ledger>(
        deserializePrefixedHeader(makeSlice(nodeObject->getData())),
        app_.config(),
        *app_.getShardFamily())};

    if (ledger->info().seq != ledgerSeq)
    {
        return fail(
            "encountered invalid ledger sequence " + std::to_string(ledgerSeq));
    }
    if (ledger->info().hash != hash)
    {
        return fail(
            "encountered invalid ledger hash " + to_string(hash) +
            " on sequence " + std::to_string(ledgerSeq));
    }

    ledger->setFull();
    if (!ledger->stateMap().fetchRoot(
            SHAMapHash{ledger->info().accountHash}, nullptr))
    {
        return fail(
            "is missing root STATE node on hash " + to_string(hash) +
            " on sequence " + std::to_string(ledgerSeq));
    }

    if (ledger->info().txHash.isNonZero())
    {
        if (!ledger->txMap().fetchRoot(
                SHAMapHash{ledger->info().txHash}, nullptr))
        {
            return fail(
                "is missing root TXN node on hash " + to_string(hash) +
                " on sequence " + std::to_string(ledgerSeq));
        }
    }
    return ledger;
}

void
DatabaseShardImp::setStored(std::shared_ptr<Ledger const> const& ledger)
{
    auto const ledgerSeq{ledger->info().seq};
    if (ledger->info().hash.isZero())
    {
        JLOG(j_.error()) << "zero ledger hash for ledger sequence "
                         << ledgerSeq;
        return;
    }
    if (ledger->info().accountHash.isZero())
    {
        JLOG(j_.error()) << "zero account hash for ledger sequence "
                         << ledgerSeq;
        return;
    }
    if (ledger->stateMap().getHash().isNonZero() &&
        !ledger->stateMap().isValid())
    {
        JLOG(j_.error()) << "invalid state map for ledger sequence "
                         << ledgerSeq;
        return;
    }
    if (ledger->info().txHash.isNonZero() && !ledger->txMap().isValid())
    {
        JLOG(j_.error()) << "invalid transaction map for ledger sequence "
                         << ledgerSeq;
        return;
    }

    auto const shardIndex{seqToShardIndex(ledgerSeq)};
    std::shared_ptr<Shard> shard;
    {
        std::lock_guard lock(mutex_);
        assert(init_);

        if (shardIndex != acquireIndex_)
        {
            JLOG(j_.trace())
                << "shard " << shardIndex << " is not being acquired";
            return;
        }

        auto const it{shards_.find(shardIndex)};
        if (it == shards_.end())
        {
            JLOG(j_.error())
                << "shard " << shardIndex << " is not being acquired";
            return;
        }
        shard = it->second;
    }

    if (shard->containsLedger(ledgerSeq))
    {
        JLOG(j_.trace()) << "shard " << shardIndex << " ledger already stored";
        return;
    }

    setStoredInShard(shard, ledger);
}

std::string
DatabaseShardImp::getCompleteShards()
{
    std::lock_guard lock(mutex_);
    assert(init_);

    return status_;
}

void
DatabaseShardImp::onStop()
{
    // Stop read threads in base before data members are destroyed
    stopReadThreads();

    std::lock_guard lock(mutex_);

    // Notify shards to stop
    for (auto const& e : shards_)
        e.second->stop();
}

void
DatabaseShardImp::onChildrenStopped()
{
    std::vector<std::weak_ptr<Shard>> shards;
    {
        std::lock_guard lock(mutex_);

        shards.reserve(shards_.size());
        for (auto const& e : shards_)
            shards.push_back(e.second);
        shards_.clear();
    }

    // All shards should be expired at this point
    for (auto const& e : shards)
    {
        if (!e.expired())
        {
            std::string shardIndex;
            if (auto const shard{e.lock()}; shard)
                shardIndex = std::to_string(shard->index());

            JLOG(j_.warn()) << " shard " << shardIndex << " unexpired";
        }
    }

    if (areChildrenStopped())
        stopped();
    else
    {
        JLOG(j_.warn()) << " Children failed to stop";
    }
}

void
DatabaseShardImp::import(Database& source)
{
    {
        std::lock_guard lock(mutex_);
        assert(init_);

        // Only the application local node store can be imported
        if (&source != &app_.getNodeStore())
        {
            assert(false);
            JLOG(j_.error()) << "invalid source database";
            return;
        }

        std::uint32_t earliestIndex;
        std::uint32_t latestIndex;
        {
            auto loadLedger = [&](bool ascendSort =
                                      true) -> boost::optional<std::uint32_t> {
                std::shared_ptr<Ledger> ledger;
                std::uint32_t ledgerSeq;
                std::tie(ledger, ledgerSeq, std::ignore) = loadLedgerHelper(
                    "WHERE LedgerSeq >= " +
                        std::to_string(earliestLedgerSeq()) +
                        " order by LedgerSeq " + (ascendSort ? "asc" : "desc") +
                        " limit 1",
                    app_,
                    false);
                if (!ledger || ledgerSeq == 0)
                {
                    JLOG(j_.error()) << "no suitable ledgers were found in"
                                        " the SQLite database to import";
                    return boost::none;
                }
                return ledgerSeq;
            };

            // Find earliest ledger sequence stored
            auto ledgerSeq{loadLedger()};
            if (!ledgerSeq)
                return;
            earliestIndex = seqToShardIndex(*ledgerSeq);

            // Consider only complete shards
            if (ledgerSeq != firstLedgerSeq(earliestIndex))
                ++earliestIndex;

            // Find last ledger sequence stored
            ledgerSeq = loadLedger(false);
            if (!ledgerSeq)
                return;
            latestIndex = seqToShardIndex(*ledgerSeq);

            // Consider only complete shards
            if (ledgerSeq != lastLedgerSeq(latestIndex))
                --latestIndex;

            if (latestIndex < earliestIndex)
            {
                JLOG(j_.error()) << "no suitable ledgers were found in"
                                    " the SQLite database to import";
                return;
            }
        }

        auto numHistShards = this->numHistoricalShards(lock);

        // Import the shards
        for (std::uint32_t shardIndex = earliestIndex;
             shardIndex <= latestIndex;
             ++shardIndex)
        {
            auto const pathDesignation =
                prepareForNewShard(shardIndex, numHistShards, lock);

            if (!pathDesignation)
                break;

            auto const needsHistoricalPath =
                *pathDesignation == PathDesignation::historical;

            // Skip if being acquired
            if (shardIndex == acquireIndex_)
            {
                JLOG(j_.debug())
                    << "shard " << shardIndex << " already being acquired";
                continue;
            }

            // Skip if being imported
            if (preparedIndexes_.find(shardIndex) != preparedIndexes_.end())
            {
                JLOG(j_.debug())
                    << "shard " << shardIndex << " already being imported";
                continue;
            }

            // Skip if stored
            if (shards_.find(shardIndex) != shards_.end())
            {
                JLOG(j_.debug()) << "shard " << shardIndex << " already stored";
                continue;
            }

            // Verify SQLite ledgers are in the node store
            {
                auto const firstSeq{firstLedgerSeq(shardIndex)};
                auto const lastSeq{
                    std::max(firstSeq, lastLedgerSeq(shardIndex))};
                auto const numLedgers{
                    shardIndex == earliestShardIndex() ? lastSeq - firstSeq + 1
                                                       : ledgersPerShard_};
                auto ledgerHashes{getHashesByIndex(firstSeq, lastSeq, app_)};
                if (ledgerHashes.size() != numLedgers)
                    continue;

                bool valid{true};
                for (std::uint32_t n = firstSeq; n <= lastSeq; n += 256)
                {
                    if (!source.fetchNodeObject(ledgerHashes[n].first, n))
                    {
                        JLOG(j_.warn()) << "SQLite ledger sequence " << n
                                        << " mismatches node store";
                        valid = false;
                        break;
                    }
                }
                if (!valid)
                    continue;
            }

            auto const path =
                needsHistoricalPath ? chooseHistoricalPath(lock) : dir_;

            // Create the new shard
            auto shard{
                std::make_unique<Shard>(app_, *this, shardIndex, path, j_)};
            if (!shard->init(scheduler_, *ctx_))
                continue;

            // Create a marker file to signify an import in progress
            auto const shardDir{path / std::to_string(shardIndex)};
            auto const markerFile{shardDir / importMarker_};
            {
                std::ofstream ofs{markerFile.string()};
                if (!ofs.is_open())
                {
                    JLOG(j_.error()) << "shard " << shardIndex
                                     << " failed to create temp marker file";
                    shard->removeOnDestroy();
                    continue;
                }
                ofs.close();
            }

            // Copy the ledgers from node store
            std::shared_ptr<Ledger> recentStored;
            boost::optional<uint256> lastLedgerHash;

            while (auto const ledgerSeq = shard->prepare())
            {
                auto ledger{loadByIndex(*ledgerSeq, app_, false)};
                if (!ledger || ledger->info().seq != ledgerSeq)
                    break;

                auto const result{shard->storeLedger(ledger, recentStored)};
                storeStats(result.count, result.size);
                if (result.error)
                    break;

                if (!shard->setLedgerStored(ledger))
                    break;

                if (!lastLedgerHash && ledgerSeq == lastLedgerSeq(shardIndex))
                    lastLedgerHash = ledger->info().hash;

                recentStored = std::move(ledger);
            }

            using namespace boost::filesystem;
            bool success{false};
            if (lastLedgerHash && shard->getState() == Shard::complete)
            {
                // Store shard final key
                Serializer s;
                s.add32(Shard::version);
                s.add32(firstLedgerSeq(shardIndex));
                s.add32(lastLedgerSeq(shardIndex));
                s.addBitString(*lastLedgerHash);
                auto const nodeObject{NodeObject::createObject(
                    hotUNKNOWN, std::move(s.modData()), Shard::finalKey)};

                if (shard->storeNodeObject(nodeObject))
                {
                    try
                    {
                        // The import process is complete and the
                        // marker file is no longer required
                        remove_all(markerFile);

                        JLOG(j_.debug()) << "shard " << shardIndex
                                         << " was successfully imported";
                        finalizeShard(
                            shards_.emplace(shardIndex, std::move(shard))
                                .first->second,
                            true,
                            boost::none);
                        success = true;

                        if (shardIndex < shardBoundaryIndex())
                            ++numHistShards;
                    }
                    catch (std::exception const& e)
                    {
                        JLOG(j_.fatal()) << "shard index " << shardIndex
                                         << ". Exception caught in function "
                                         << __func__ << ". Error: " << e.what();
                    }
                }
            }

            if (!success)
            {
                JLOG(j_.error())
                    << "shard " << shardIndex << " failed to import";
                shard->removeOnDestroy();
            }
        }

        updateStatus(lock);
    }

    setFileStats();
}

std::int32_t
DatabaseShardImp::getWriteLoad() const
{
    std::shared_ptr<Shard> shard;
    {
        std::lock_guard lock(mutex_);
        assert(init_);

        auto const it{shards_.find(acquireIndex_)};
        if (it == shards_.end())
            return 0;
        shard = it->second;
    }

    return shard->getWriteLoad();
}

void
DatabaseShardImp::store(
    NodeObjectType type,
    Blob&& data,
    uint256 const& hash,
    std::uint32_t ledgerSeq)
{
    auto const shardIndex{seqToShardIndex(ledgerSeq)};
    std::shared_ptr<Shard> shard;
    {
        std::lock_guard lock(mutex_);
        if (shardIndex != acquireIndex_)
        {
            JLOG(j_.trace())
                << "shard " << shardIndex << " is not being acquired";
            return;
        }

        auto const it{shards_.find(shardIndex)};
        if (it == shards_.end())
        {
            JLOG(j_.error())
                << "shard " << shardIndex << " is not being acquired";
            return;
        }
        shard = it->second;
    }

    auto const nodeObject{
        NodeObject::createObject(type, std::move(data), hash)};
    if (shard->storeNodeObject(nodeObject))
        storeStats(1, nodeObject->getData().size());
}

bool
DatabaseShardImp::asyncFetch(
    uint256 const& hash,
    std::uint32_t ledgerSeq,
    std::shared_ptr<NodeObject>& nodeObject)
{
    std::shared_ptr<Shard> shard;
    {
        std::lock_guard lock(mutex_);
        assert(init_);

        auto const it{shards_.find(acquireIndex_)};
        if (it == shards_.end())
            return false;
        shard = it->second;
    }

    if (shard->fetchNodeObjectFromCache(hash, nodeObject))
        return true;

    // Otherwise post a read
    Database::asyncFetch(hash, ledgerSeq);
    return false;
}

bool
DatabaseShardImp::storeLedger(std::shared_ptr<Ledger const> const& srcLedger)
{
    auto const ledgerSeq{srcLedger->info().seq};
    auto const shardIndex{seqToShardIndex(ledgerSeq)};
    std::shared_ptr<Shard> shard;
    {
        std::lock_guard lock(mutex_);
        assert(init_);

        if (shardIndex != acquireIndex_)
        {
            JLOG(j_.trace())
                << "shard " << shardIndex << " is not being acquired";
            return false;
        }

        auto const it{shards_.find(shardIndex)};
        if (it == shards_.end())
        {
            JLOG(j_.error())
                << "shard " << shardIndex << " is not being acquired";
            return false;
        }
        shard = it->second;
    }

    auto const result{shard->storeLedger(srcLedger, nullptr)};
    storeStats(result.count, result.size);
    if (result.error || result.count == 0 || result.size == 0)
        return false;

    return setStoredInShard(shard, srcLedger);
}

int
DatabaseShardImp::getDesiredAsyncReadCount(std::uint32_t ledgerSeq)
{
    auto const shardIndex{seqToShardIndex(ledgerSeq)};
    std::shared_ptr<Shard> shard;
    {
        std::lock_guard lock(mutex_);
        assert(init_);

        auto const it{shards_.find(shardIndex)};
        if (it == shards_.end())
            return 0;
        shard = it->second;
    }

    return shard->getDesiredAsyncReadCount();
}

float
DatabaseShardImp::getCacheHitRate()
{
    std::shared_ptr<Shard> shard;
    {
        std::lock_guard lock(mutex_);
        assert(init_);

        auto const it{shards_.find(acquireIndex_)};
        if (it == shards_.end())
            return 0;
        shard = it->second;
    }

    return shard->getCacheHitRate();
}

void
DatabaseShardImp::sweep()
{
    std::vector<std::weak_ptr<Shard>> shards;
    {
        std::lock_guard lock(mutex_);
        assert(init_);

        shards.reserve(shards_.size());
        for (auto const& e : shards_)
            shards.push_back(e.second);
    }

    std::vector<std::shared_ptr<Shard>> openFinals;
    openFinals.reserve(openFinalLimit_);

    for (auto const& e : shards)
    {
        if (auto const shard{e.lock()}; shard && shard->isOpen())
        {
            shard->sweep();

            if (shard->getState() == Shard::final)
                openFinals.emplace_back(std::move(shard));
        }
    }

    if (openFinals.size() > openFinalLimit_)
    {
        JLOG(j_.trace()) << "Open shards exceed configured limit of "
                         << openFinalLimit_ << " by "
                         << (openFinals.size() - openFinalLimit_);

        // Try to close enough shards to be within the limit.
        // Sort ascending on last use so the oldest are removed first.
        std::sort(
            openFinals.begin(),
            openFinals.end(),
            [&](std::shared_ptr<Shard> const& lhsShard,
                std::shared_ptr<Shard> const& rhsShard) {
                return lhsShard->getLastUse() < rhsShard->getLastUse();
            });

        for (auto it{openFinals.cbegin()};
             it != openFinals.cend() && openFinals.size() > openFinalLimit_;)
        {
            if ((*it)->tryClose())
                it = openFinals.erase(it);
            else
                ++it;
        }
    }
}

bool
DatabaseShardImp::initConfig(std::lock_guard<std::mutex> const&)
{
    auto fail = [j = j_](std::string const& msg) {
        JLOG(j.error()) << "[" << ConfigSection::shardDatabase() << "] " << msg;
        return false;
    };

    Config const& config{app_.config()};
    Section const& section{config.section(ConfigSection::shardDatabase())};

    {
        // The earliest ledger sequence defaults to XRP_LEDGER_EARLIEST_SEQ.
        // A custom earliest ledger sequence can be set through the
        // configuration file using the 'earliest_seq' field under the
        // 'node_db' and 'shard_db' stanzas. If specified, this field must
        // have a value greater than zero and be equally assigned in
        // both stanzas.

        std::uint32_t shardDBEarliestSeq{0};
        get_if_exists<std::uint32_t>(
            section, "earliest_seq", shardDBEarliestSeq);

        std::uint32_t nodeDBEarliestSeq{0};
        get_if_exists<std::uint32_t>(
            config.section(ConfigSection::nodeDatabase()),
            "earliest_seq",
            nodeDBEarliestSeq);

        if (shardDBEarliestSeq != nodeDBEarliestSeq)
        {
            return fail(
                "and [" + ConfigSection::nodeDatabase() +
                "] define different 'earliest_seq' values");
        }
    }

    using namespace boost::filesystem;
    if (!get_if_exists<path>(section, "path", dir_))
        return fail("'path' missing");

    {
        get_if_exists(section, "max_historical_shards", maxHistoricalShards_);

        Section const& historicalShardPaths =
            config.section(SECTION_HISTORICAL_SHARD_PATHS);

        auto values = historicalShardPaths.values();

        std::sort(values.begin(), values.end());
        values.erase(std::unique(values.begin(), values.end()), values.end());

        for (auto const& s : values)
        {
            auto const dir = path(s);
            if (dir_ == dir)
            {
                return fail(
                    "the 'path' cannot also be in the  "
                    "'historical_shard_path' section");
            }

            historicalPaths_.push_back(s);
        }
    }

    if (section.exists("ledgers_per_shard"))
    {
        // To be set only in standalone for testing
        if (!config.standalone())
            return fail("'ledgers_per_shard' only honored in stand alone");

        ledgersPerShard_ = get<std::uint32_t>(section, "ledgers_per_shard");
        if (ledgersPerShard_ == 0 || ledgersPerShard_ % 256 != 0)
            return fail("'ledgers_per_shard' must be a multiple of 256");

        earliestShardIndex_ = seqToShardIndex(earliestLedgerSeq());
        avgShardFileSz_ = ledgersPerShard_ * kilobytes(192);
    }

    // NuDB is the default and only supported permanent storage backend
    backendName_ = get<std::string>(section, "type", "nudb");
    if (!boost::iequals(backendName_, "NuDB"))
        return fail("'type' value unsupported");

    return true;
}

std::shared_ptr<NodeObject>
DatabaseShardImp::fetchNodeObject(
    uint256 const& hash,
    std::uint32_t ledgerSeq,
    FetchReport& fetchReport)
{
    auto const shardIndex{seqToShardIndex(ledgerSeq)};
    std::shared_ptr<Shard> shard;
    {
        std::lock_guard lock(mutex_);
        auto const it{shards_.find(shardIndex)};
        if (it == shards_.end())
            return nullptr;
        shard = it->second;
    }

    return shard->fetchNodeObject(hash, fetchReport);
}

boost::optional<std::uint32_t>
DatabaseShardImp::findAcquireIndex(
    std::uint32_t validLedgerSeq,
    std::lock_guard<std::mutex> const&)
{
    if (validLedgerSeq < earliestLedgerSeq())
        return boost::none;

    auto const maxShardIndex{[this, validLedgerSeq]() {
        auto shardIndex{seqToShardIndex(validLedgerSeq)};
        if (validLedgerSeq != lastLedgerSeq(shardIndex))
            --shardIndex;
        return shardIndex;
    }()};
    auto const maxNumShards{maxShardIndex - earliestShardIndex() + 1};

    // Check if the shard store has all shards
    if (shards_.size() >= maxNumShards)
        return boost::none;

    if (maxShardIndex < 1024 ||
        static_cast<float>(shards_.size()) / maxNumShards > 0.5f)
    {
        // Small or mostly full index space to sample
        // Find the available indexes and select one at random
        std::vector<std::uint32_t> available;
        available.reserve(maxNumShards - shards_.size());

        for (auto shardIndex = earliestShardIndex();
             shardIndex <= maxShardIndex;
             ++shardIndex)
        {
            if (shards_.find(shardIndex) == shards_.end() &&
                preparedIndexes_.find(shardIndex) == preparedIndexes_.end())
            {
                available.push_back(shardIndex);
            }
        }

        if (available.empty())
            return boost::none;

        if (available.size() == 1)
            return available.front();

        return available[rand_int(
            0u, static_cast<std::uint32_t>(available.size() - 1))];
    }

    // Large, sparse index space to sample
    // Keep choosing indexes at random until an available one is found
    // chances of running more than 30 times is less than 1 in a billion
    for (int i = 0; i < 40; ++i)
    {
        auto const shardIndex{rand_int(earliestShardIndex(), maxShardIndex)};
        if (shards_.find(shardIndex) == shards_.end() &&
            preparedIndexes_.find(shardIndex) == preparedIndexes_.end())
        {
            return shardIndex;
        }
    }

    assert(false);
    return boost::none;
}

void
DatabaseShardImp::finalizeShard(
    std::shared_ptr<Shard>& shard,
    bool writeSQLite,
    boost::optional<uint256> const& expectedHash)
{
    taskQueue_->addTask([this,
                         wptr = std::weak_ptr<Shard>(shard),
                         writeSQLite,
                         expectedHash]() {
        if (isStopping())
            return;

        auto shard{wptr.lock()};
        if (!shard)
        {
            JLOG(j_.debug()) << "Shard removed before being finalized";
            return;
        }

        if (!shard->finalize(writeSQLite, expectedHash))
        {
            if (isStopping())
                return;

            // Invalid or corrupt shard, remove it
            removeFailedShard(shard);
            return;
        }

        if (isStopping())
            return;

        {
            auto const boundaryIndex{shardBoundaryIndex()};

            std::lock_guard lock(mutex_);
            updateStatus(lock);

            if (shard->index() < boundaryIndex)
            {
                // This is a historical shard
                if (!historicalPaths_.empty() &&
                    shard->getDir().parent_path() == dir_)
                {
                    // Shard wasn't placed at a separate historical path
                    JLOG(j_.warn()) << "shard " << shard->index()
                                    << " is not stored at a historical path";
                }
            }

            else
            {
                // Not a historical shard. Shift recent shards if necessary
                relocateOutdatedShards(lock);
                assert(!boundaryIndex || shard->index() - boundaryIndex <= 1);

                auto& recentShard = shard->index() == boundaryIndex
                    ? secondLatestShardIndex_
                    : latestShardIndex_;

                // Set the appropriate recent shard index
                recentShard = shard->index();

                if (shard->getDir().parent_path() != dir_)
                {
                    JLOG(j_.warn()) << "shard " << shard->index()
                                    << " is not stored at the path";
                }
            }
        }

        setFileStats();

        // Update peers with new shard index
        if (!app_.config().standalone() &&
            app_.getOPs().getOperatingMode() != OperatingMode::DISCONNECTED)
        {
            protocol::TMPeerShardInfo message;
            PublicKey const& publicKey{app_.nodeIdentity().first};
            message.set_nodepubkey(publicKey.data(), publicKey.size());
            message.set_shardindexes(std::to_string(shard->index()));
            app_.overlay().foreach(send_always(std::make_shared<Message>(
                message, protocol::mtPEER_SHARD_INFO)));
        }
    });
}

void
DatabaseShardImp::setFileStats()
{
    std::vector<std::weak_ptr<Shard>> shards;
    {
        std::lock_guard lock(mutex_);
        if (shards_.empty())
            return;

        shards.reserve(shards_.size());
        for (auto const& e : shards_)
            shards.push_back(e.second);
    }

    std::uint64_t sumSz{0};
    std::uint32_t sumFd{0};
    std::uint32_t numShards{0};
    for (auto const& e : shards)
    {
        if (auto const shard{e.lock()}; shard)
        {
            auto const [sz, fd] = shard->getFileInfo();
            sumSz += sz;
            sumFd += fd;
            ++numShards;
        }
    }

    std::lock_guard lock(mutex_);
    fileSz_ = sumSz;
    fdRequired_ = sumFd;
    avgShardFileSz_ = (numShards == 0 ? fileSz_ : fileSz_ / numShards);

    if (!canAdd_)
        return;

    if (auto const count = numHistoricalShards(lock);
        count >= maxHistoricalShards_)
    {
        if (maxHistoricalShards_)
        {
            // In order to avoid excessive output, don't produce
            // this warning if the server isn't configured to
            // store historical shards.
            JLOG(j_.warn()) << "maximum number of historical shards reached";
        }

        canAdd_ = false;
    }
    else if (!sufficientStorage(
                 maxHistoricalShards_ - count,
                 PathDesignation::historical,
                 lock))
    {
        JLOG(j_.warn())
            << "maximum shard store size exceeds available storage space";

        canAdd_ = false;
    }
}

void
DatabaseShardImp::updateStatus(std::lock_guard<std::mutex> const&)
{
    if (!shards_.empty())
    {
        RangeSet<std::uint32_t> rs;
        for (auto const& e : shards_)
            if (e.second->getState() == Shard::final)
                rs.insert(e.second->index());
        status_ = to_string(rs);
    }
    else
        status_.clear();
}

bool
DatabaseShardImp::sufficientStorage(
    std::uint32_t numShards,
    PathDesignation pathDesignation,
    std::lock_guard<std::mutex> const&) const
{
    try
    {
        std::vector<std::uint64_t> capacities;

        if (pathDesignation == PathDesignation::historical &&
            !historicalPaths_.empty())
        {
            capacities.reserve(historicalPaths_.size());

            for (auto const& path : historicalPaths_)
            {
                // Get the available storage for each historical path
                auto const availableSpace =
                    boost::filesystem::space(path).available;

                capacities.push_back(availableSpace);
            }
        }
        else
        {
            // Get the available storage for the main shard path
            capacities.push_back(boost::filesystem::space(dir_).available);
        }

        for (std::uint64_t const capacity : capacities)
        {
            // Leverage all the historical shard paths to
            // see if collectively they can fit the specified
            // number of shards. For this to work properly,
            // each historical path must correspond to a separate
            // physical device or filesystem.

            auto const shardCap = capacity / avgShardFileSz_;
            if (numShards <= shardCap)
                return true;

            numShards -= shardCap;
        }
    }
    catch (std::exception const& e)
    {
        JLOG(j_.fatal()) << "Exception caught in function " << __func__
                         << ". Error: " << e.what();
        return false;
    }

    return false;
}

bool
DatabaseShardImp::setStoredInShard(
    std::shared_ptr<Shard>& shard,
    std::shared_ptr<Ledger const> const& ledger)
{
    if (!shard->setLedgerStored(ledger))
    {
        // Invalid or corrupt shard, remove it
        removeFailedShard(shard);
        return false;
    }

    if (shard->getState() == Shard::complete)
    {
        std::lock_guard lock(mutex_);
        if (auto const it{shards_.find(shard->index())}; it != shards_.end())
        {
            if (shard->index() == acquireIndex_)
                acquireIndex_ = 0;

            finalizeShard(it->second, false, boost::none);
        }
        else
        {
            JLOG(j_.debug())
                << "shard " << shard->index() << " is no longer being acquired";
        }
    }

    setFileStats();
    return true;
}

void
DatabaseShardImp::removeFailedShard(std::shared_ptr<Shard>& shard)
{
    {
        std::lock_guard lock(mutex_);

        if (shard->index() == acquireIndex_)
            acquireIndex_ = 0;

        if (shard->index() == latestShardIndex_)
            latestShardIndex_ = boost::none;

        if (shard->index() == secondLatestShardIndex_)
            secondLatestShardIndex_ = boost::none;

        if ((shards_.erase(shard->index()) > 0) &&
            shard->getState() == Shard::final)
        {
            updateStatus(lock);
        }
    }

    shard->removeOnDestroy();

    // Reset the shared_ptr to invoke the shard's
    // destructor and remove it from the server
    shard.reset();
    setFileStats();
}

std::uint32_t
DatabaseShardImp::shardBoundaryIndex() const
{
    auto const validIndex = app_.getLedgerMaster().getValidLedgerIndex();

    if (validIndex < earliestLedgerSeq())
        return 0;

    // Shards with an index earlier than the recent shard boundary index
    // are considered historical. The three shards at or later than
    // this index consist of the two most recently validated shards
    // and the shard still in the process of being built by live
    // transactions.
    return seqToShardIndex(validIndex) - 1;
}

std::uint32_t
DatabaseShardImp::numHistoricalShards(
    std::lock_guard<std::mutex> const& lock) const
{
    auto const boundaryIndex{shardBoundaryIndex()};
    return std::count_if(
        shards_.begin(), shards_.end(), [boundaryIndex](auto const& entry) {
            return entry.first < boundaryIndex;
        });
}

void
DatabaseShardImp::relocateOutdatedShards(
    std::lock_guard<std::mutex> const& lock)
{
    if (auto& cur = latestShardIndex_, &prev = secondLatestShardIndex_;
        cur || prev)
    {
        auto const latestShardIndex =
            seqToShardIndex(app_.getLedgerMaster().getValidLedgerIndex());

        auto const separateHistoricalPath = !historicalPaths_.empty();

        auto const removeShard =
            [this](std::uint32_t const shardIndex) -> void {
            canAdd_ = false;

            if (auto it = shards_.find(shardIndex); it != shards_.end())
            {
                if (it->second)
                    removeFailedShard(it->second);
                else
                {
                    JLOG(j_.warn()) << "can't find shard to remove";
                }
            }
            else
            {
                JLOG(j_.warn()) << "can't find shard to remove";
            }
        };

        auto const keepShard =
            [this, &lock, removeShard, separateHistoricalPath](
                std::uint32_t const shardIndex) -> bool {
            if (numHistoricalShards(lock) >= maxHistoricalShards_)
            {
                JLOG(j_.error())
                    << "maximum number of historical shards reached";

                removeShard(shardIndex);
                return false;
            }
            if (separateHistoricalPath &&
                !sufficientStorage(1, PathDesignation::historical, lock))
            {
                JLOG(j_.error()) << "insufficient storage space available";

                removeShard(shardIndex);
                return false;
            }

            return true;
        };

        // Move a shard from the main shard path to a historical shard
        // path by copying the contents, and creating a new shard.
        auto const moveShard = [this,
                                &lock](std::uint32_t const shardIndex) -> void {
            auto const dst = chooseHistoricalPath(lock);

            if (auto it = shards_.find(shardIndex); it != shards_.end())
            {
                auto& shard{it->second};

                // Close any open file descriptors before moving the shard
                // directory. Don't call removeOnDestroy since that would
                // attempt to close the fds after the directory has been moved.
                if (!shard->tryClose())
                {
                    JLOG(j_.warn())
                        << "can't close shard to move to historical path";
                    return;
                }

                try
                {
                    // Move the shard directory to the new path
                    boost::filesystem::rename(
                        shard->getDir().string(),
                        dst / std::to_string(shardIndex));
                }
                catch (...)
                {
                    JLOG(j_.error()) << "shard " << shardIndex
                                     << " failed to move to historical storage";
                    return;
                }

                // Create a shard instance at the new location
                shard =
                    std::make_shared<Shard>(app_, *this, shardIndex, dst, j_);

                // Open the new shard
                if (!shard->init(scheduler_, *ctx_))
                {
                    JLOG(j_.error()) << "shard " << shardIndex
                                     << " failed to open in historical storage";
                    shard->removeOnDestroy();
                    shard.reset();
                }
            }
            else
            {
                JLOG(j_.warn())
                    << "can't find shard to move to historical path";
            }
        };

        // See if either of the recent shards needs to be updated
        bool const curNotSynched =
            latestShardIndex_ && *latestShardIndex_ != latestShardIndex;
        bool const prevNotSynched = secondLatestShardIndex_ &&
            *secondLatestShardIndex_ != latestShardIndex - 1;

        // A new shard has been published. Move outdated
        // shards to historical storage as needed
        if (curNotSynched || prevNotSynched)
        {
            if (prev)
            {
                // Move the formerly second latest shard to historical storage
                if (keepShard(*prev) && separateHistoricalPath)
                {
                    moveShard(*prev);
                }

                prev = boost::none;
            }

            if (cur)
            {
                // The formerly latest shard is now the second latest
                if (cur == latestShardIndex - 1)
                {
                    prev = cur;
                }

                // The formerly latest shard is no longer a 'recent' shard
                else
                {
                    // Move the formerly latest shard to historical storage
                    if (keepShard(*cur) && separateHistoricalPath)
                    {
                        moveShard(*cur);
                    }
                }

                cur = boost::none;
            }
        }
    }
}

auto
DatabaseShardImp::prepareForNewShard(
    std::uint32_t shardIndex,
    std::uint32_t numHistoricalShards,
    std::lock_guard<std::mutex> const& lock) -> boost::optional<PathDesignation>
{
    // Any shard earlier than the two most recent shards is a historical shard
    auto const boundaryIndex{shardBoundaryIndex()};
    auto const isHistoricalShard = shardIndex < boundaryIndex;

    auto const designation = isHistoricalShard && !historicalPaths_.empty()
        ? PathDesignation::historical
        : PathDesignation::none;

    // Check shard count and available storage space
    if (isHistoricalShard && numHistoricalShards >= maxHistoricalShards_)
    {
        JLOG(j_.error()) << "maximum number of historical shards reached";
        canAdd_ = false;
        return boost::none;
    }
    if (!sufficientStorage(1, designation, lock))
    {
        JLOG(j_.error()) << "insufficient storage space available";
        canAdd_ = false;
        return boost::none;
    }

    return designation;
}

boost::filesystem::path
DatabaseShardImp::chooseHistoricalPath(std::lock_guard<std::mutex> const&) const
{
    // If not configured with separate historical paths,
    // use the main path (dir_) by default.
    if (historicalPaths_.empty())
        return dir_;

    boost::filesystem::path historicalShardPath;
    std::vector<boost::filesystem::path> potentialPaths;

    for (boost::filesystem::path const& path : historicalPaths_)
    {
        if (boost::filesystem::space(path).available >= avgShardFileSz_)
            potentialPaths.push_back(path);
    }

    if (potentialPaths.empty())
    {
        JLOG(j_.error()) << "failed to select a historical shard path";
        return "";
    }

    std::sample(
        potentialPaths.begin(),
        potentialPaths.end(),
        &historicalShardPath,
        1,
        default_prng());

    return historicalShardPath;
}

bool
DatabaseShardImp::checkHistoricalPaths() const
{
#if BOOST_OS_LINUX
    // Each historical shard path must correspond
    // to a directory on a distinct device or file system.
    // Currently, this constraint is enforced only on Linux.
    std::unordered_map<int, std::vector<std::string>> filesystemIDs(
        historicalPaths_.size());

    for (auto const& path : historicalPaths_)
    {
        struct statvfs buffer;
        if (statvfs(path.c_str(), &buffer))
        {
            JLOG(j_.error())
                << "failed to acquire stats for 'historical_shard_path': "
                << path;
            return false;
        }

        filesystemIDs[buffer.f_fsid].push_back(path.string());
    }

    bool ret = true;
    for (auto const& entry : filesystemIDs)
    {
        // Check to see if any of the paths are stored on the same file system
        if (entry.second.size() > 1)
        {
            // Two or more historical storage paths
            // correspond to the same file system.
            JLOG(j_.error())
                << "The following paths correspond to the same filesystem: "
                << boost::algorithm::join(entry.second, ", ")
                << ". Each configured historical storage path should"
                   " be on a unique device or filesystem.";

            ret = false;
        }
    }

    return ret;

#else
    // The requirement that each historical storage path
    // corresponds to a distinct device or file system is
    // enforced only on Linux, so on other platforms
    // keep track of the available capacities for each
    // path. Issue a warning if we suspect any of the paths
    // may violate this requirement.

    // Map byte counts to each path that shares that byte count.
    std::unordered_map<std::uint64_t, std::vector<std::string>>
        uniqueCapacities(historicalPaths_.size());

    for (auto const& path : historicalPaths_)
        uniqueCapacities[boost::filesystem::space(path).available].push_back(
            path.string());

    for (auto const& entry : uniqueCapacities)
    {
        // Check to see if any paths have the same amount of available bytes.
        if (entry.second.size() > 1)
        {
            // Two or more historical storage paths may
            // correspond to the same device or file system.
            JLOG(j_.warn())
                << "Each of the following paths have " << entry.first
                << " bytes free, and may be located on the same device"
                   " or file system: "
                << boost::algorithm::join(entry.second, ", ")
                << ". Each configured historical storage path should"
                   " be on a unique device or file system.";
        }
    }
#endif

    return true;
}

//------------------------------------------------------------------------------

std::unique_ptr<DatabaseShard>
make_ShardStore(
    Application& app,
    Stoppable& parent,
    Scheduler& scheduler,
    int readThreads,
    beast::Journal j)
{
    // The shard store is optional. Future changes will require it.
    Section const& section{
        app.config().section(ConfigSection::shardDatabase())};
    if (section.empty())
        return nullptr;

    return std::make_unique<DatabaseShardImp>(
        app, parent, "ShardStore", scheduler, readThreads, j);
}

}  // namespace NodeStore
}  // namespace ripple
