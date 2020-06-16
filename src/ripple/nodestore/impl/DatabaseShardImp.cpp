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
    , avgShardFileSz_(ledgersPerShard_ * kilobytes(192))
{
}

DatabaseShardImp::~DatabaseShardImp()
{
    onStop();
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

            // Consolidate the main storage path and all
            // historical paths
            std::vector<path> paths{dir_};
            paths.insert(
                paths.end(), historicalPaths_.begin(), historicalPaths_.end());

            for (auto const& path : paths)
            {
                try
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
                catch (...)
                {
                    JLOG(j_.error())
                        << "failed to create path: " + path.string();
                    return false;
                }
            }

            if (!app_.config().standalone() && !historicalPaths_.empty())
            {
                // Check historical paths for duplicated
                // filesystems
                if (!checkHistoricalPaths())
                    return false;
            }

            ctx_ = std::make_unique<nudb::context>();
            ctx_->start();

            // Find shards
            for (auto const& path : paths)
            {
                for (auto const& d : directory_iterator(path))
                {
                    if (!is_directory(d))
                        continue;

                    auto const shardDir = d.path();

                    // Check shard directory name is numeric
                    auto dirName = shardDir.stem().string();
                    if (!std::all_of(
                            dirName.begin(), dirName.end(), [](auto c) {
                                return ::isdigit(static_cast<unsigned char>(c));
                            }))
                    {
                        continue;
                    }

                    auto const shardIndex{std::stoul(dirName)};
                    if (shardIndex < earliestShardIndex())
                    {
                        JLOG(j_.error())
                            << "shard " << shardIndex
                            << " comes before earliest shard index "
                            << earliestShardIndex();
                        return false;
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

                    auto shard{std::make_unique<Shard>(
                        app_, *this, shardIndex, shardDir.parent_path(), j_)};
                    if (!shard->open(scheduler_, *ctx_))
                    {
                        // Remove corrupted or legacy shard
                        shard->removeOnDestroy();
                        JLOG(j_.warn())
                            << "shard " << shardIndex << " removed, "
                            << (shard->isLegacy() ? "legacy" : "corrupted")
                            << " shard";
                        continue;
                    }

                    if (shard->isFinal())
                    {
                        shards_.emplace(
                            shardIndex,
                            ShardInfo(
                                std::move(shard), ShardInfo::State::final));
                    }
                    else if (shard->isBackendComplete())
                    {
                        auto const result{shards_.emplace(
                            shardIndex,
                            ShardInfo(
                                std::move(shard), ShardInfo::State::none))};
                        finalizeShard(
                            result.first->second, true, lock, boost::none);
                    }
                    else
                    {
                        if (acquireIndex_ != 0)
                        {
                            JLOG(j_.error())
                                << "more than one shard being acquired";
                            return false;
                        }

                        shards_.emplace(
                            shardIndex,
                            ShardInfo(
                                std::move(shard), ShardInfo::State::acquire));
                        acquireIndex_ = shardIndex;
                    }
                }
            }
        }
        catch (std::exception const& e)
        {
            JLOG(j_.error())
                << "exception " << e.what() << " in function " << __func__;
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
            if (auto it{shards_.find(acquireIndex_)}; it != shards_.end())
                return it->second.shard->prepare();
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
        return false;

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

    if (!shard->open(scheduler_, *ctx_))
        return boost::none;

    auto const seq{shard->prepare()};
    {
        std::lock_guard lock(mutex_);
        shards_.emplace(
            *shardIndex,
            ShardInfo(std::move(shard), ShardInfo::State::acquire));
        acquireIndex_ = *shardIndex;
    }
    return seq;
}

bool
DatabaseShardImp::prepareShard(std::uint32_t shardIndex)
{
    auto fail = [j = j_, shardIndex](std::string const& msg) {
        JLOG(j.error()) << "shard " << shardIndex << " " << msg;
        return false;
    };
    std::lock_guard lock(mutex_);
    assert(init_);

    if (!canAdd_)
        return fail("cannot be stored at this time");

    if (shardIndex < earliestShardIndex())
    {
        return fail(
            "comes before earliest shard index " +
            std::to_string(earliestShardIndex()));
    }

    // If we are synced to the network, check if the shard index
    // is greater or equal to the current shard.
    auto seqCheck = [&](std::uint32_t seq) {
        // seq will be greater than zero if valid
        if (seq >= earliestLedgerSeq() && shardIndex >= seqToShardIndex(seq))
            return fail("has an invalid index");
        return true;
    };
    if (!seqCheck(app_.getLedgerMaster().getValidLedgerIndex() + 1) ||
        !seqCheck(app_.getLedgerMaster().getCurrentLedgerIndex()))
    {
        return false;
    }

    if (shards_.find(shardIndex) != shards_.end())
    {
        JLOG(j_.debug()) << "shard " << shardIndex
                         << " is already stored or queued for import";
        return false;
    }

    // Any shard earlier than the two most recent shards
    // is a historical shard
    bool const isHistoricalShard = shardIndex < shardBoundaryIndex(lock);
    auto const numHistShards = numHistoricalShards(lock);

    // Check shard count and available storage space
    if (isHistoricalShard && numHistShards >= maxHistoricalShards_)
        return fail("maximum number of historical shards reached");
    if (!sufficientStorage(
            1,
            isHistoricalShard ? PathDesignation::historical
                              : PathDesignation::none,
            lock))
        return fail("insufficient storage space available");

    shards_.emplace(shardIndex, ShardInfo(nullptr, ShardInfo::State::import));
    return true;
}

void
DatabaseShardImp::removePreShard(std::uint32_t shardIndex)
{
    std::lock_guard lock(mutex_);
    assert(init_);

    if (auto const it{shards_.find(shardIndex)};
        it != shards_.end() && it->second.state == ShardInfo::State::import)
    {
        shards_.erase(it);
    }
}

std::string
DatabaseShardImp::getPreShards()
{
    RangeSet<std::uint32_t> rs;
    {
        std::lock_guard lock(mutex_);
        assert(init_);

        for (auto const& e : shards_)
            if (e.second.state == ShardInfo::State::import)
                rs.insert(e.first);
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
    using namespace boost::filesystem;
    try
    {
        if (!is_directory(srcDir) || is_empty(srcDir))
        {
            JLOG(j_.error()) << "invalid source directory " << srcDir.string();
            return false;
        }
    }
    catch (std::exception const& e)
    {
        JLOG(j_.error()) << "exception " << e.what() << " in function "
                         << __func__;
        return false;
    }

    auto expectedHash = app_.getLedgerMaster().walkHashBySeq(
        lastLedgerSeq(shardIndex), InboundLedger::Reason::GENERIC);

    if (!expectedHash)
    {
        JLOG(j_.error()) << "shard " << shardIndex
                         << " expected hash not found";
        return false;
    }

    auto renameDir = [&](path const& src, path const& dst) {
        try
        {
            rename(src, dst);
        }
        catch (std::exception const& e)
        {
            JLOG(j_.error())
                << "exception " << e.what() << " in function " << __func__;
            return false;
        }
        return true;
    };

    path dstDir;
    {
        std::lock_guard lock(mutex_);
        assert(init_);

        // Check shard is prepared
        if (auto const it{shards_.find(shardIndex)}; it == shards_.end() ||
            it->second.shard || it->second.state != ShardInfo::State::import)
        {
            JLOG(j_.error()) << "shard " << shardIndex << " failed to import";
            return false;
        }

        auto const pathDesignation =
            prepareForNewShard(shardIndex, numHistoricalShards(lock), lock);

        if (!pathDesignation)
        {
            JLOG(j_.error()) << "shard " << shardIndex << " failed to import";
            return false;
        }

        auto const needsHistoricalPath =
            *pathDesignation == PathDesignation::historical;
        dstDir = needsHistoricalPath ? chooseHistoricalPath(lock) : dir_;
    }

    dstDir /= std::to_string(shardIndex);

    // Rename source directory to the shard database directory
    if (!renameDir(srcDir, dstDir))
        return false;

    // Create the new shard
    auto shard{std::make_unique<Shard>(
        app_, *this, shardIndex, dstDir.parent_path(), j_)};

    if (!shard->open(scheduler_, *ctx_) || !shard->isBackendComplete())
    {
        JLOG(j_.error()) << "shard " << shardIndex << " failed to import";
        shard.reset();
        renameDir(dstDir, srcDir);
        return false;
    }

    {
        std::lock_guard lock(mutex_);
        auto const it{shards_.find(shardIndex)};
        if (it == shards_.end() || it->second.shard ||
            it->second.state != ShardInfo::State::import)
        {
            JLOG(j_.error()) << "shard " << shardIndex << " failed to import";
            shard.reset();
            renameDir(dstDir, srcDir);
            return false;
        }

        it->second.shard = std::move(shard);
        finalizeShard(it->second, true, lock, expectedHash);
    }

    return true;
}

std::shared_ptr<Ledger>
DatabaseShardImp::fetchLedger(uint256 const& hash, std::uint32_t seq)
{
    auto const shardIndex{seqToShardIndex(seq)};
    {
        std::shared_ptr<Shard> shard;
        ShardInfo::State state;
        {
            std::lock_guard lock(mutex_);
            assert(init_);

            if (auto const it{shards_.find(shardIndex)}; it != shards_.end())
            {
                shard = it->second.shard;
                state = it->second.state;
            }
            else
                return {};
        }

        // Check if the ledger is stored in a final shard
        // or in the shard being acquired
        switch (state)
        {
            case ShardInfo::State::final:
                break;
            case ShardInfo::State::acquire:
                if (shard->containsLedger(seq))
                    break;
                [[fallthrough]];
            default:
                return {};
        }
    }

    auto nObj{fetch(hash, seq)};
    if (!nObj)
        return {};

    auto fail = [this, seq](std::string const& msg) -> std::shared_ptr<Ledger> {
        JLOG(j_.error()) << "shard " << seqToShardIndex(seq) << " " << msg;
        return {};
    };

    auto ledger{std::make_shared<Ledger>(
        deserializePrefixedHeader(makeSlice(nObj->getData())),
        app_.config(),
        *app_.getShardFamily())};

    if (ledger->info().seq != seq)
    {
        return fail(
            "encountered invalid ledger sequence " + std::to_string(seq));
    }
    if (ledger->info().hash != hash)
    {
        return fail(
            "encountered invalid ledger hash " + to_string(hash) +
            " on sequence " + std::to_string(seq));
    }

    ledger->setFull();
    if (!ledger->stateMap().fetchRoot(
            SHAMapHash{ledger->info().accountHash}, nullptr))
    {
        return fail(
            "is missing root STATE node on hash " + to_string(hash) +
            " on sequence " + std::to_string(seq));
    }

    if (ledger->info().txHash.isNonZero())
    {
        if (!ledger->txMap().fetchRoot(
                SHAMapHash{ledger->info().txHash}, nullptr))
        {
            return fail(
                "is missing root TXN node on hash " + to_string(hash) +
                " on sequence " + std::to_string(seq));
        }
    }
    return ledger;
}

void
DatabaseShardImp::setStored(std::shared_ptr<Ledger const> const& ledger)
{
    if (ledger->info().hash.isZero())
    {
        JLOG(j_.error()) << "zero ledger hash for ledger sequence "
                         << ledger->info().seq;
        return;
    }
    if (ledger->info().accountHash.isZero())
    {
        JLOG(j_.error()) << "zero account hash for ledger sequence "
                         << ledger->info().seq;
        return;
    }
    if (ledger->stateMap().getHash().isNonZero() &&
        !ledger->stateMap().isValid())
    {
        JLOG(j_.error()) << "invalid state map for ledger sequence "
                         << ledger->info().seq;
        return;
    }
    if (ledger->info().txHash.isNonZero() && !ledger->txMap().isValid())
    {
        JLOG(j_.error()) << "invalid transaction map for ledger sequence "
                         << ledger->info().seq;
        return;
    }

    auto const shardIndex{seqToShardIndex(ledger->info().seq)};
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

        if (auto const it{shards_.find(shardIndex)}; it != shards_.end())
            shard = it->second.shard;
        else
        {
            JLOG(j_.error())
                << "shard " << shardIndex << " is not being acquired";
            return;
        }
    }

    storeLedgerInShard(shard, ledger);
}

std::string
DatabaseShardImp::getCompleteShards()
{
    std::lock_guard lock(mutex_);
    assert(init_);

    return status_;
}

void
DatabaseShardImp::validate()
{
    std::vector<std::weak_ptr<Shard>> shards;
    {
        std::lock_guard lock(mutex_);
        assert(init_);

        // Only shards with a state of final should be validated
        for (auto& e : shards_)
            if (e.second.state == ShardInfo::State::final)
                shards.push_back(e.second.shard);

        if (shards.empty())
            return;

        JLOG(j_.debug()) << "Validating shards " << status_;
    }

    for (auto const& e : shards)
    {
        if (auto shard{e.lock()}; shard)
            shard->finalize(true, boost::none);
    }

    app_.getShardFamily()->reset();
}

void
DatabaseShardImp::onStop()
{
    // Stop read threads in base before data members are destroyed
    stopThreads();

    std::lock_guard lock(mutex_);
    if (shards_.empty())
        return;

    // Notify and destroy shards
    for (auto& e : shards_)
    {
        if (e.second.shard)
            e.second.shard->stop();
    }
    shards_.clear();
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
                std::uint32_t seq;
                std::tie(ledger, seq, std::ignore) = loadLedgerHelper(
                    "WHERE LedgerSeq >= " +
                        std::to_string(earliestLedgerSeq()) +
                        " order by LedgerSeq " + (ascendSort ? "asc" : "desc") +
                        " limit 1",
                    app_,
                    false);
                if (!ledger || seq == 0)
                {
                    JLOG(j_.error()) << "no suitable ledgers were found in"
                                        " the SQLite database to import";
                    return boost::none;
                }
                return seq;
            };

            // Find earliest ledger sequence stored
            auto seq{loadLedger()};
            if (!seq)
                return;
            earliestIndex = seqToShardIndex(*seq);

            // Consider only complete shards
            if (seq != firstLedgerSeq(earliestIndex))
                ++earliestIndex;

            // Find last ledger sequence stored
            seq = loadLedger(false);
            if (!seq)
                return;
            latestIndex = seqToShardIndex(*seq);

            // Consider only complete shards
            if (seq != lastLedgerSeq(latestIndex))
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

            // Skip if already stored
            if (shardIndex == acquireIndex_ ||
                shards_.find(shardIndex) != shards_.end())
            {
                JLOG(j_.debug()) << "shard " << shardIndex << " already exists";
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
                    if (!source.fetch(ledgerHashes[n].first, n))
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
            auto shard =
                std::make_unique<Shard>(app_, *this, shardIndex, path, j_);

            if (!shard->open(scheduler_, *ctx_))
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

            while (auto seq = shard->prepare())
            {
                auto ledger{loadByIndex(*seq, app_, false)};
                if (!ledger || ledger->info().seq != seq)
                    break;

                if (!Database::storeLedger(
                        *ledger,
                        shard->getBackend(),
                        nullptr,
                        nullptr,
                        recentStored))
                {
                    break;
                }

                if (!shard->store(ledger))
                    break;

                if (!lastLedgerHash && seq == lastLedgerSeq(shardIndex))
                    lastLedgerHash = ledger->info().hash;

                recentStored = ledger;
            }

            using namespace boost::filesystem;
            if (lastLedgerHash && shard->isBackendComplete())
            {
                // Store shard final key
                Serializer s;
                s.add32(Shard::version);
                s.add32(firstLedgerSeq(shardIndex));
                s.add32(lastLedgerSeq(shardIndex));
                s.addBitString(*lastLedgerHash);
                auto nObj{NodeObject::createObject(
                    hotUNKNOWN, std::move(s.modData()), Shard::finalKey)};

                try
                {
                    shard->getBackend()->store(nObj);

                    // The import process is complete and the
                    // marker file is no longer required
                    remove_all(markerFile);

                    JLOG(j_.debug()) << "shard " << shardIndex
                                     << " was successfully imported";

                    auto const result{shards_.emplace(
                        shardIndex,
                        ShardInfo(std::move(shard), ShardInfo::State::none))};
                    finalizeShard(
                        result.first->second, true, lock, boost::none);

                    if (shardIndex < shardBoundaryIndex(lock))
                        ++numHistShards;
                }
                catch (std::exception const& e)
                {
                    JLOG(j_.error()) << "exception " << e.what()
                                     << " in function " << __func__;
                    shard->removeOnDestroy();
                }
            }
            else
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

        if (auto const it{shards_.find(acquireIndex_)}; it != shards_.end())
            shard = it->second.shard;
        else
            return 0;
    }

    return shard->getBackend()->getWriteLoad();
}

void
DatabaseShardImp::store(
    NodeObjectType type,
    Blob&& data,
    uint256 const& hash,
    std::uint32_t seq)
{
    auto const shardIndex{seqToShardIndex(seq)};
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

        if (auto const it{shards_.find(shardIndex)}; it != shards_.end())
            shard = it->second.shard;
        else
        {
            JLOG(j_.error())
                << "shard " << shardIndex << " is not being acquired";
            return;
        }
    }

    auto [backend, pCache, nCache] = shard->getBackendAll();
    auto nObj{NodeObject::createObject(type, std::move(data), hash)};

    pCache->canonicalize_replace_cache(hash, nObj);
    backend->store(nObj);
    nCache->erase(hash);

    storeStats(nObj->getData().size());
}

std::shared_ptr<NodeObject>
DatabaseShardImp::fetch(uint256 const& hash, std::uint32_t seq)
{
    auto cache{getCache(seq)};
    if (cache.first)
        return doFetch(hash, seq, *cache.first, *cache.second, false);
    return {};
}

bool
DatabaseShardImp::asyncFetch(
    uint256 const& hash,
    std::uint32_t seq,
    std::shared_ptr<NodeObject>& object)
{
    auto cache{getCache(seq)};
    if (cache.first)
    {
        // See if the object is in cache
        object = cache.first->fetch(hash);
        if (object || cache.second->touch_if_exists(hash))
            return true;
        // Otherwise post a read
        Database::asyncFetch(hash, seq, cache.first, cache.second);
    }
    return false;
}

bool
DatabaseShardImp::storeLedger(std::shared_ptr<Ledger const> const& srcLedger)
{
    auto const seq{srcLedger->info().seq};
    auto const shardIndex{seqToShardIndex(seq)};
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

        if (auto const it{shards_.find(shardIndex)}; it != shards_.end())
            shard = it->second.shard;
        else
        {
            JLOG(j_.error())
                << "shard " << shardIndex << " is not being acquired";
            return false;
        }
    }

    if (shard->containsLedger(seq))
    {
        JLOG(j_.trace()) << "shard " << shardIndex << " ledger already stored";
        return false;
    }

    {
        auto [backend, pCache, nCache] = shard->getBackendAll();
        if (!Database::storeLedger(
                *srcLedger, backend, pCache, nCache, nullptr))
        {
            return false;
        }
    }

    return storeLedgerInShard(shard, srcLedger);
}

int
DatabaseShardImp::getDesiredAsyncReadCount(std::uint32_t seq)
{
    auto const shardIndex{seqToShardIndex(seq)};
    std::shared_ptr<Shard> shard;
    {
        std::lock_guard lock(mutex_);
        assert(init_);

        if (auto const it{shards_.find(shardIndex)}; it != shards_.end() &&
            (it->second.state == ShardInfo::State::final ||
             it->second.state == ShardInfo::State::acquire))
        {
            shard = it->second.shard;
        }
        else
            return 0;
    }

    return shard->pCache()->getTargetSize() / asyncDivider;
}

float
DatabaseShardImp::getCacheHitRate()
{
    std::shared_ptr<Shard> shard;
    {
        std::lock_guard lock(mutex_);
        assert(init_);

        if (auto const it{shards_.find(acquireIndex_)}; it != shards_.end())
            shard = it->second.shard;
        else
            return 0;
    }

    return shard->pCache()->getHitRate();
}

void
DatabaseShardImp::sweep()
{
    std::vector<std::weak_ptr<Shard>> shards;
    {
        std::lock_guard lock(mutex_);
        assert(init_);

        for (auto const& e : shards_)
            if (e.second.state == ShardInfo::State::final ||
                e.second.state == ShardInfo::State::acquire)
            {
                shards.push_back(e.second.shard);
            }
    }

    for (auto const& e : shards)
    {
        if (auto shard{e.lock()}; shard)
            shard->sweep();
    }
}

bool
DatabaseShardImp::initConfig(std::lock_guard<std::mutex>&)
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
DatabaseShardImp::fetchFrom(uint256 const& hash, std::uint32_t seq)
{
    auto const shardIndex{seqToShardIndex(seq)};
    std::shared_ptr<Shard> shard;
    {
        std::lock_guard lock(mutex_);
        assert(init_);

        if (auto const it{shards_.find(shardIndex)};
            it != shards_.end() && it->second.shard)
        {
            shard = it->second.shard;
        }
        else
            return {};
    }

    return fetchInternal(hash, shard->getBackend());
}

boost::optional<std::uint32_t>
DatabaseShardImp::findAcquireIndex(
    std::uint32_t validLedgerSeq,
    std::lock_guard<std::mutex>&)
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
            if (shards_.find(shardIndex) == shards_.end())
                available.push_back(shardIndex);
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
        if (shards_.find(shardIndex) == shards_.end())
            return shardIndex;
    }

    assert(false);
    return boost::none;
}

void
DatabaseShardImp::finalizeShard(
    ShardInfo& shardInfo,
    bool writeSQLite,
    std::lock_guard<std::mutex>&,
    boost::optional<uint256> const& expectedHash)
{
    assert(shardInfo.shard);
    assert(shardInfo.shard->index() != acquireIndex_);
    assert(shardInfo.shard->isBackendComplete());
    assert(shardInfo.state != ShardInfo::State::finalize);

    auto const shardIndex{shardInfo.shard->index()};

    shardInfo.state = ShardInfo::State::finalize;
    taskQueue_->addTask([this, shardIndex, writeSQLite, expectedHash]() {
        if (isStopping())
            return;

        std::shared_ptr<Shard> shard;
        {
            std::lock_guard lock(mutex_);
            if (auto const it{shards_.find(shardIndex)}; it != shards_.end())
            {
                shard = it->second.shard;
            }
            else
            {
                JLOG(j_.error()) << "Unable to finalize shard " << shardIndex;
                return;
            }
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
            std::lock_guard lock(mutex_);
            auto const it{shards_.find(shardIndex)};
            if (it == shards_.end())
                return;
            it->second.state = ShardInfo::State::final;
            updateStatus(lock);

            auto const boundaryIndex = shardBoundaryIndex(lock);
            auto const isHistoricalShard = shardIndex < boundaryIndex;

            if (isHistoricalShard)
            {
                if (!historicalPaths_.empty() &&
                    shard->getDir().parent_path() == dir_)
                {
                    // This is a historical shard that wasn't
                    // placed at a separate historical path
                    JLOG(j_.warn()) << "shard " << shardIndex
                                    << " is not stored at a historical path";
                }
            }

            else
            {
                // Not a historical shard. Shift recent shards
                // if necessary
                relocateOutdatedShards(lock);
                assert(!boundaryIndex || shardIndex - boundaryIndex <= 1);

                auto& recentShard = shardIndex == boundaryIndex
                    ? secondLatestShardIndex_
                    : latestShardIndex_;

                // Set the appropriate recent shard
                // index
                recentShard = shardIndex;

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
            message.set_shardindexes(std::to_string(shardIndex));
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
        assert(init_);

        if (shards_.empty())
            return;

        for (auto const& e : shards_)
            if (e.second.shard)
                shards.push_back(e.second.shard);
    }

    std::uint64_t sumSz{0};
    std::uint32_t sumFd{0};
    std::uint32_t numShards{0};
    for (auto const& e : shards)
    {
        if (auto shard{e.lock()}; shard)
        {
            auto [sz, fd] = shard->fileInfo();
            sumSz += sz;
            sumFd += fd;
            ++numShards;
        }
    }

    std::lock_guard lock(mutex_);
    fileSz_ = sumSz;
    fdRequired_ = sumFd;
    avgShardFileSz_ = (numShards == 0 ? fileSz_ : fileSz_ / numShards);

    if (auto const count = numHistoricalShards(lock);
        count >= maxHistoricalShards_)
    {
        JLOG(j_.warn()) << "maximum number of historical shards reached";
        canAdd_ = false;
    }
    else if (!sufficientStorage(
                 maxHistoricalShards_ - count,
                 PathDesignation::historical,
                 lock))
    {
        JLOG(j_.warn())
            << "maximum shard store size exceeds available storage space";
    }
}

void
DatabaseShardImp::updateStatus(std::lock_guard<std::mutex>&)
{
    if (!shards_.empty())
    {
        RangeSet<std::uint32_t> rs;
        for (auto const& e : shards_)
            if (e.second.state == ShardInfo::State::final)
                rs.insert(e.second.shard->index());
        status_ = to_string(rs);
    }
    else
        status_.clear();
}

std::pair<std::shared_ptr<PCache>, std::shared_ptr<NCache>>
DatabaseShardImp::getCache(std::uint32_t seq)
{
    auto const shardIndex{seqToShardIndex(seq)};
    std::shared_ptr<Shard> shard;
    {
        std::lock_guard lock(mutex_);
        assert(init_);

        if (auto const it{shards_.find(shardIndex)};
            it != shards_.end() && it->second.shard)
        {
            shard = it->second.shard;
        }
        else
            return {};
    }

    std::shared_ptr<PCache> pCache;
    std::shared_ptr<NCache> nCache;
    std::tie(std::ignore, pCache, nCache) = shard->getBackendAll();

    return std::make_pair(pCache, nCache);
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
        JLOG(j_.error()) << "exception " << e.what() << " in function "
                         << __func__;
        return false;
    }

    return false;
}

bool
DatabaseShardImp::storeLedgerInShard(
    std::shared_ptr<Shard>& shard,
    std::shared_ptr<Ledger const> const& ledger)
{
    bool result{true};

    if (!shard->store(ledger))
    {
        // Invalid or corrupt shard, remove it
        removeFailedShard(shard);
        result = false;
    }
    else if (shard->isBackendComplete())
    {
        std::lock_guard lock(mutex_);

        if (auto const it{shards_.find(shard->index())}; it != shards_.end())
        {
            if (shard->index() == acquireIndex_)
                acquireIndex_ = 0;

            if (it->second.state != ShardInfo::State::finalize)
                finalizeShard(it->second, false, lock, boost::none);
        }
        else
        {
            JLOG(j_.debug())
                << "shard " << shard->index() << " is no longer being acquired";
        }
    }

    setFileStats();
    return result;
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

        if ((shards_.erase(shard->index()) > 0) && shard->isFinal())
            updateStatus(lock);
    }

    shard->removeOnDestroy();

    // Reset the shared_ptr to invoke the shard's
    // destructor and remove it from the server
    shard.reset();
    setFileStats();
}

std::uint32_t
DatabaseShardImp::shardBoundaryIndex(std::lock_guard<std::mutex> const&) const
{
    auto const validIndex = app_.getLedgerMaster().getValidLedgerIndex();

    // Shards with an index earlier than recentShardBoundaryIndex
    // are considered historical. The three shards at or later than
    // this index consist of the two most recently validated shards
    // and the shard still in the process of being built by live
    // transactions.
    return NodeStore::seqToShardIndex(validIndex, ledgersPerShard_) - 1;
}

std::uint32_t
DatabaseShardImp::numHistoricalShards(
    std::lock_guard<std::mutex> const& lock) const
{
    auto const recentShardBoundaryIndex = shardBoundaryIndex(lock);

    return std::count_if(
        shards_.begin(),
        shards_.end(),
        [recentShardBoundaryIndex](auto const& entry) {
            return entry.first < recentShardBoundaryIndex;
        });
}

void
DatabaseShardImp::relocateOutdatedShards(
    std::lock_guard<std::mutex> const& lock)
{
    if (auto& cur = latestShardIndex_, &prev = secondLatestShardIndex_;
        cur || prev)
    {
        auto const latestShardIndex = NodeStore::seqToShardIndex(
            app_.getLedgerMaster().getValidLedgerIndex(), ledgersPerShard_);

        auto const separateHistoricalPath = !historicalPaths_.empty();

        auto const removeShard =
            [this](std::uint32_t const shardIndex) -> void {
            canAdd_ = false;

            if (auto it = shards_.find(shardIndex); it != shards_.end())
            {
                if (it->second.shard)
                    removeFailedShard(it->second.shard);
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
                if (auto& shard = it->second.shard)
                {
                    // Close any open file descriptors before moving
                    // the shard dir. Don't call removeOnDestroy since
                    // that would attempt to close the fds after the
                    // directory has been moved.
                    shard->closeAll();

                    try
                    {
                        // Move the shard directory to the new path
                        boost::filesystem::rename(
                            shard->getDir().string(),
                            dst / std::to_string(shardIndex));
                    }
                    catch (...)
                    {
                        JLOG(j_.error())
                            << "shard " << shardIndex
                            << " failed to move to historical storage";

                        return;
                    }

                    // Create a shard instance at the new location
                    shard = std::make_unique<Shard>(
                        app_, *this, shardIndex, dst, j_);

                    // Open the new shard
                    if (!shard->open(scheduler_, *ctx_))
                    {
                        JLOG(j_.error())
                            << "shard " << shardIndex
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
            }
            else
            {
                JLOG(j_.warn())
                    << "can't find shard to move to historical path";
            }
        };

        // See if either of the recent shards
        // needs to be updated
        bool const curNotSynched =
            latestShardIndex_ && *latestShardIndex_ != latestShardIndex;
        bool const prevNotSynched = secondLatestShardIndex_ &&
            *secondLatestShardIndex_ != latestShardIndex - 1;

        // A new shard has been published. Move outdated shards
        // to historical storage as needed
        if (curNotSynched || prevNotSynched)
        {
            if (prev)
            {
                // Move the formerly second latest shard to
                // historical storage
                if (keepShard(*prev) && separateHistoricalPath)
                {
                    moveShard(*prev);
                }

                prev = boost::none;
            }

            if (cur)
            {
                // The formerly latest shard is now
                // the second latest
                if (cur == latestShardIndex - 1)
                {
                    prev = cur;
                }

                // The formerly latest shard is no
                // longer a 'recent' shard
                else
                {
                    // Move the formerly latest shard to
                    // historical storage
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
    // Any shard earlier than the two most recent shards
    // is a historical shard
    auto const boundaryIndex = shardBoundaryIndex(lock);
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
    // to a directory on a distinct device or filesystem.
    // Currently, this constraint is enforced only on
    // Linux.

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
        // Check to see if any of the paths
        // are stored on the same filesystem
        if (entry.second.size() > 1)
        {
            // Two or more historical storage paths
            // correspond to the same filesystem.
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
    // corresponds to a distinct device or filesystem is
    // enforced only on Linux, so on other platforms
    // keep track of the available capacities for each
    // path. Issue a warning if we suspect any of the paths
    // may violate this requirement.

    // Map byte counts to each path that
    // shares that byte count.
    std::unordered_map<std::uint64_t, std::vector<std::string>>
        uniqueCapacities(historicalPaths_.size());

    for (auto const& path : historicalPaths_)
        uniqueCapacities[boost::filesystem::space(path).available].push_back(
            path.string());

    for (auto const& entry : uniqueCapacities)
    {
        // Check to see if any paths have the
        // same amount of available bytes.
        if (entry.second.size() > 1)
        {
            // Two or more historical storage paths may
            // correspond to the same device or
            // filesystem.
            JLOG(j_.warn())
                << "Each of the following paths have " << entry.first
                << " bytes free, and may be located on the same device"
                   " or filesystem: "
                << boost::algorithm::join(entry.second, ", ")
                << ". Each configured historical storage path should"
                   " be on a unique device or filesystem.";
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
