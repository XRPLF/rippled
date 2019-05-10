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

#include <ripple/nodestore/impl/DatabaseShardImp.h>
#include <ripple/app/ledger/InboundLedgers.h>
#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/basics/ByteUtilities.h>
#include <ripple/basics/chrono.h>
#include <ripple/basics/random.h>
#include <ripple/core/ConfigSections.h>
#include <ripple/nodestore/DummyScheduler.h>
#include <ripple/nodestore/Manager.h>
#include <ripple/overlay/Overlay.h>
#include <ripple/overlay/predicates.h>
#include <ripple/protocol/HashPrefix.h>

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
    , earliestShardIndex_(seqToShardIndex(earliestSeq()))
    , avgShardSz_(ledgersPerShard_ * kilobytes(192))
{
}

DatabaseShardImp::~DatabaseShardImp()
{
    // Stop threads before data members are destroyed
    stopThreads();

    // Close backend databases before destroying the context
    std::lock_guard<std::mutex> lock(m_);
    complete_.clear();
    if (incomplete_)
        incomplete_.reset();
    preShards_.clear();
    ctx_.reset();
}

bool
DatabaseShardImp::init()
{
    using namespace boost::filesystem;
    using namespace boost::beast::detail;

    std::lock_guard<std::mutex> lock(m_);
    if (init_)
    {
        JLOG(j_.error()) <<
            "Already initialized";
        return false;
    }

    auto fail = [&](std::string const& msg)
    {
        JLOG(j_.error()) <<
            "[" << ConfigSection::shardDatabase() << "] " << msg;
        return false;
    };

    Config const& config {app_.config()};
    Section const& section {config.section(ConfigSection::shardDatabase())};
    if (section.empty())
        return fail("missing configuration");

    {
        // Node and shard stores must use same earliest ledger sequence
        std::uint32_t seq;
        if (get_if_exists<std::uint32_t>(
                config.section(ConfigSection::nodeDatabase()),
                "earliest_seq",
                seq))
        {
            std::uint32_t seq2;
            if (get_if_exists<std::uint32_t>(section, "earliest_seq", seq2) &&
                seq != seq2)
            {
                return fail("and [" + ConfigSection::shardDatabase() +
                    "] both define 'earliest_seq'");
            }
        }
    }

    if (!get_if_exists<boost::filesystem::path>(section, "path", dir_))
        return fail("'path' missing");

    if (boost::filesystem::exists(dir_))
    {
        if (!boost::filesystem::is_directory(dir_))
            return fail("'path' must be a directory");
    }
    else
        boost::filesystem::create_directories(dir_);

    {
        std::uint64_t i;
        if (!get_if_exists<std::uint64_t>(section, "max_size_gb", i))
            return fail("'max_size_gb' missing");

        // Minimum disk space required (in gigabytes)
        static constexpr auto minDiskSpace = 10;
        if (i < minDiskSpace)
        {
            return fail("'max_size_gb' must be at least " +
                std::to_string(minDiskSpace));
        }

        if ((i << 30) < i)
            return fail("'max_size_gb' overflow");

        // Convert to bytes
        maxDiskSpace_ = i << 30;
    }

    if (section.exists("ledgers_per_shard"))
    {
        // To be set only in standalone for testing
        if (!config.standalone())
            return fail("'ledgers_per_shard' only honored in stand alone");

        ledgersPerShard_ = get<std::uint32_t>(section, "ledgers_per_shard");
        if (ledgersPerShard_ == 0 || ledgersPerShard_ % 256 != 0)
            return fail("'ledgers_per_shard' must be a multiple of 256");
    }

    // NuDB is the default and only supported permanent storage backend
    // "Memory" and "none" types are supported for tests
    backendName_ = get<std::string>(section, "type", "nudb");
    if (!iequals(backendName_, "NuDB") &&
        !iequals(backendName_, "Memory") &&
        !iequals(backendName_, "none"))
    {
        return fail("'type' value unsupported");
    }

    // Find backend file handle requirement
    if (auto factory = Manager::instance().find(backendName_))
    {
        auto backend {factory->createInstance(
            NodeObject::keyBytes, section, scheduler_, j_)};
        backed_ = backend->backed();
        if (!backed_)
        {
            init_ = true;
            return true;
        }
        fdLimit_ = backend->fdlimit();
    }
    else
        return fail("'type' value unsupported");

    try
    {
        ctx_ = std::make_unique<nudb::context>();
        ctx_->start();

        // Find shards
        for (auto const& d : directory_iterator(dir_))
        {
            if (!is_directory(d))
                continue;

            // Validate shard directory name is numeric
            auto dirName = d.path().stem().string();
            if (!std::all_of(
                dirName.begin(),
                dirName.end(),
                [](auto c) {
                return ::isdigit(static_cast<unsigned char>(c));
            }))
            {
                continue;
            }

            auto const shardIndex {std::stoul(dirName)};
            if (shardIndex < earliestShardIndex())
            {
                JLOG(j_.fatal()) <<
                    "Invalid shard index " << shardIndex <<
                    ". Earliest shard index " << earliestShardIndex();
                return false;
            }

            // Check if a previous import failed
            if (is_regular_file(
                dir_ / std::to_string(shardIndex) / importMarker_))
            {
                JLOG(j_.warn()) <<
                    "shard " << shardIndex <<
                    " previously failed import, removing";
                remove_all(dir_ / std::to_string(shardIndex));
                continue;
            }

            auto shard {std::make_unique<Shard>(
                *this, shardIndex, cacheSz_, cacheAge_, j_)};
            if (!shard->open(section, scheduler_, *ctx_))
                return false;

            usedDiskSpace_ += shard->fileSize();
            if (shard->complete())
                complete_.emplace(shard->index(), std::move(shard));
            else
            {
                if (incomplete_)
                {
                    JLOG(j_.fatal()) <<
                        "More than one control file found";
                    return false;
                }
                incomplete_ = std::move(shard);
            }
        }
    }
    catch (std::exception const& e)
    {
        JLOG(j_.error()) <<
            "exception: " << e.what();
        return false;
    }

    if (!incomplete_ && complete_.empty())
    {
        // New Shard Store, calculate file descriptor requirements
        if (maxDiskSpace_ > available())
        {
            JLOG(j_.error()) <<
                "Insufficient disk space";
        }
        fdLimit_ = 1 + (fdLimit_ *
            std::max<std::uint64_t>(1, maxDiskSpace_ / avgShardSz_));
    }
    else
        updateStats(lock);
    init_ = true;
    return true;
}

boost::optional<std::uint32_t>
DatabaseShardImp::prepareLedger(std::uint32_t validLedgerSeq)
{
    std::lock_guard<std::mutex> lock(m_);
    assert(init_);
    if (incomplete_)
        return incomplete_->prepare();
    if (!canAdd_)
        return boost::none;
    if (backed_)
    {
        // Check available disk space
        if (usedDiskSpace_ + avgShardSz_ > maxDiskSpace_)
        {
            JLOG(j_.debug()) <<
                "Maximum size reached";
            canAdd_ = false;
            return boost::none;
        }
        if (avgShardSz_ > available())
        {
            JLOG(j_.error()) <<
                "Insufficient disk space";
            canAdd_ = false;
            return boost::none;
        }
    }

    auto const shardIndex {findShardIndexToAdd(validLedgerSeq, lock)};
    if (!shardIndex)
    {
        JLOG(j_.debug()) <<
            "No new shards to add";
        canAdd_ = false;
        return boost::none;
    }
    // With every new shard, clear family caches
    app_.shardFamily()->reset();
    int const sz {std::max(shardCacheSz, cacheSz_ / std::max(
        1, static_cast<int>(complete_.size() + 1)))};
    incomplete_ = std::make_unique<Shard>(
        *this, *shardIndex, sz, cacheAge_, j_);
    if (!incomplete_->open(
            app_.config().section(ConfigSection::shardDatabase()),
            scheduler_,
            *ctx_))
    {
        incomplete_.reset();
        return boost::none;
    }

    return incomplete_->prepare();
}

bool
DatabaseShardImp::prepareShard(std::uint32_t shardIndex)
{
    std::lock_guard<std::mutex> lock(m_);
    assert(init_);
    if (!canAdd_)
    {
        JLOG(j_.error()) <<
            "Unable to add more shards to the database";
        return false;
    }

    if (shardIndex < earliestShardIndex())
    {
        JLOG(j_.error()) <<
            "Invalid shard index " << shardIndex;
        return false;
    }

    // If we are synced to the network, check if the shard index
    // is greater or equal to the current shard.
    auto seqCheck = [&](std::uint32_t seq)
    {
        // seq will be greater than zero if valid
        if (seq > earliestSeq() && shardIndex >= seqToShardIndex(seq))
        {
            JLOG(j_.error()) <<
                "Invalid shard index " << shardIndex;
            return false;
        }
        return true;
    };
    if (!seqCheck(app_.getLedgerMaster().getValidLedgerIndex()) ||
        !seqCheck(app_.getLedgerMaster().getCurrentLedgerIndex()))
    {
        return false;
    }

    if (complete_.find(shardIndex) != complete_.end())
    {
        JLOG(j_.debug()) <<
            "Shard index " << shardIndex <<
            " stored";
        return false;
    }
    if (incomplete_ && incomplete_->index() == shardIndex)
    {
        JLOG(j_.debug()) <<
            "Shard index " << shardIndex <<
            " is being acquired";
        return false;
    }
    if (preShards_.find(shardIndex) != preShards_.end())
    {
        JLOG(j_.debug()) <<
            "Shard index " << shardIndex <<
            " is prepared for import";
        return false;
    }

    // Check limit and space requirements
    if (backed_)
    {
        std::uint64_t const sz {
            (preShards_.size() + 1 + (incomplete_ ? 1 : 0)) * avgShardSz_};
        if (usedDiskSpace_ + sz > maxDiskSpace_)
        {
            JLOG(j_.debug()) <<
                "Exceeds maximum size";
            return false;
        }
        if (sz > available())
        {
            JLOG(j_.error()) <<
                "Insufficient disk space";
            return false;
        }
    }

    // Add to shards prepared
    preShards_.emplace(shardIndex, nullptr);
    return true;
}

void
DatabaseShardImp::removePreShard(std::uint32_t shardIndex)
{
    std::lock_guard<std::mutex> lock(m_);
    assert(init_);
    preShards_.erase(shardIndex);
}

std::string
DatabaseShardImp::getPreShards()
{
    RangeSet<std::uint32_t> rs;
    {
        std::lock_guard<std::mutex> lock(m_);
        assert(init_);
        if (preShards_.empty())
            return {};
        for (auto const& ps : preShards_)
            rs.insert(ps.first);
    }
    return to_string(rs);
};

bool
DatabaseShardImp::importShard(std::uint32_t shardIndex,
    boost::filesystem::path const& srcDir, bool validate)
{
    using namespace boost::filesystem;
    try
    {
        if (!is_directory(srcDir) || is_empty(srcDir))
        {
            JLOG(j_.error()) <<
                "Invalid source directory " << srcDir.string();
            return false;
        }
    }
    catch (std::exception const& e)
    {
        JLOG(j_.error()) <<
            "exception: " << e.what();
        return false;
    }

    auto move = [&](path const& src, path const& dst)
    {
        try
        {
            rename(src, dst);
        }
        catch (std::exception const& e)
        {
            JLOG(j_.error()) <<
                "exception: " << e.what();
            return false;
        }
        return true;
    };

    std::unique_lock<std::mutex> lock(m_);
    assert(init_);

    // Check shard is prepared
    auto it {preShards_.find(shardIndex)};
    if(it == preShards_.end())
    {
        JLOG(j_.error()) <<
            "Invalid shard index " << shardIndex;
        return false;
    }

    // Move source directory to the shard database directory
    auto const dstDir {dir_ / std::to_string(shardIndex)};
    if (!move(srcDir, dstDir))
        return false;

    // Create the new shard
    auto shard {std::make_unique<Shard>(
        *this, shardIndex, cacheSz_, cacheAge_, j_)};
    auto fail = [&](std::string const& msg)
    {
        if (!msg.empty())
        {
            JLOG(j_.error()) <<
                "Import shard " << shardIndex << ": " << msg;
        }
        shard.reset();
        move(dstDir, srcDir);
        return false;
    };

    if (!shard->open(
            app_.config().section(ConfigSection::shardDatabase()),
            scheduler_,
            *ctx_))
    {
        return fail({});
    }
    if (!shard->complete())
        return fail("incomplete shard");

    try
    {
        // Verify database integrity
        shard->getBackend()->verify();
    }
    catch (std::exception const& e)
    {
        return fail(e.what());
    }

    // Validate shard ledgers
    if (validate)
    {
        // Shard validation requires releasing the lock
        // so the database can fetch data from it
        it->second = shard.get();
        lock.unlock();
        auto const valid {shard->validate(app_)};
        lock.lock();
        if (!valid)
        {
            it = preShards_.find(shardIndex);
            if(it != preShards_.end())
                it->second = nullptr;
            return fail("failed validation");
        }
    }

    // Add the shard
    usedDiskSpace_ += shard->fileSize();
    complete_.emplace(shardIndex, std::move(shard));
    preShards_.erase(shardIndex);
    return true;
}

std::shared_ptr<Ledger>
DatabaseShardImp::fetchLedger(uint256 const& hash, std::uint32_t seq)
{
    if (!contains(seq))
        return {};
    auto nObj = fetch(hash, seq);
    if (!nObj)
        return {};

    auto ledger = std::make_shared<Ledger>(
        InboundLedger::deserializeHeader(makeSlice(nObj->getData()), true),
            app_.config(), *app_.shardFamily());
    if (ledger->info().hash != hash || ledger->info().seq != seq)
    {
        JLOG(j_.error()) <<
            "shard " << seqToShardIndex(seq) <<
            " ledger seq " << seq <<
            " hash " << hash <<
            " has corrupt data";
        return {};
    }
    ledger->setFull();
    if (!ledger->stateMap().fetchRoot(
        SHAMapHash {ledger->info().accountHash}, nullptr))
    {
        JLOG(j_.error()) <<
            "shard " << seqToShardIndex(seq) <<
            " ledger seq " << seq <<
            " missing Account State root";
        return {};
    }
    if (ledger->info().txHash.isNonZero())
    {
        if (!ledger->txMap().fetchRoot(
            SHAMapHash {ledger->info().txHash}, nullptr))
        {
            JLOG(j_.error()) <<
                "shard " << seqToShardIndex(seq) <<
                " ledger seq " << seq <<
                " missing TX root";
            return {};
        }
    }
    return ledger;
}

void
DatabaseShardImp::setStored(std::shared_ptr<Ledger const> const& ledger)
{
    if (ledger->info().hash.isZero() ||
        ledger->info().accountHash.isZero())
    {
        assert(false);
        JLOG(j_.error()) <<
            "Invalid ledger";
        return;
    }
    auto const shardIndex {seqToShardIndex(ledger->info().seq)};
    std::lock_guard<std::mutex> lock(m_);
    assert(init_);
    if (!incomplete_ || shardIndex != incomplete_->index())
    {
        JLOG(j_.warn()) <<
            "ledger seq " << ledger->info().seq <<
            " is not being acquired";
        return;
    }

    auto const before {incomplete_->fileSize()};
    if (!incomplete_->setStored(ledger))
        return;
    auto const after {incomplete_->fileSize()};
     if(after > before)
         usedDiskSpace_ += (after - before);
     else if(after < before)
         usedDiskSpace_ -= std::min(before - after, usedDiskSpace_);

    if (incomplete_->complete())
    {
        complete_.emplace(incomplete_->index(), std::move(incomplete_));
        incomplete_.reset();
        updateStats(lock);

        // Update peers with new shard index
        protocol::TMPeerShardInfo message;
        PublicKey const& publicKey {app_.nodeIdentity().first};
        message.set_nodepubkey(publicKey.data(), publicKey.size());
        message.set_shardindexes(std::to_string(shardIndex));
        app_.overlay().foreach(send_always(
            std::make_shared<Message>(message, protocol::mtPEER_SHARD_INFO)));
    }
}

bool
DatabaseShardImp::contains(std::uint32_t seq)
{
    auto const shardIndex {seqToShardIndex(seq)};
    std::lock_guard<std::mutex> lock(m_);
    assert(init_);
    if (complete_.find(shardIndex) != complete_.end())
        return true;
    if (incomplete_ && incomplete_->index() == shardIndex)
        return incomplete_->contains(seq);
    return false;
}

std::string
DatabaseShardImp::getCompleteShards()
{
    std::lock_guard<std::mutex> lock(m_);
    assert(init_);
    return status_;
}

void
DatabaseShardImp::validate()
{
    {
        std::lock_guard<std::mutex> lock(m_);
        assert(init_);
        if (complete_.empty() && !incomplete_)
        {
            JLOG(j_.error()) <<
                "No shards to validate";
            return;
        }

        std::string s {"Found shards "};
        for (auto& e : complete_)
            s += std::to_string(e.second->index()) + ",";
        if (incomplete_)
            s += std::to_string(incomplete_->index());
        else
            s.pop_back();
        JLOG(j_.debug()) << s;
    }

    for (auto& e : complete_)
    {
        app_.shardFamily()->reset();
        e.second->validate(app_);
    }
    if (incomplete_)
    {
        app_.shardFamily()->reset();
        incomplete_->validate(app_);
    }
    app_.shardFamily()->reset();
}

void
DatabaseShardImp::import(Database& source)
{
    std::unique_lock<std::mutex> lock(m_);
    assert(init_);

    // Only the application local node store can be imported
    if (&source != &app_.getNodeStore())
    {
        assert(false);
        JLOG(j_.error()) <<
            "Invalid source database";
        return;
    }

    std::uint32_t earliestIndex;
    std::uint32_t latestIndex;
    {
        auto loadLedger = [&](bool ascendSort = true) ->
            boost::optional<std::uint32_t>
        {
            std::shared_ptr<Ledger> ledger;
            std::uint32_t seq;
            std::tie(ledger, seq, std::ignore) = loadLedgerHelper(
                "WHERE LedgerSeq >= " + std::to_string(earliestSeq()) +
                " order by LedgerSeq " + (ascendSort ? "asc" : "desc") +
                " limit 1", app_, false);
            if (!ledger || seq == 0)
            {
                JLOG(j_.error()) <<
                    "No suitable ledgers were found in" <<
                    " the sqlite database to import";
                return boost::none;
            }
            return seq;
        };

        // Find earliest ledger sequence stored
        auto seq {loadLedger()};
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
            JLOG(j_.error()) <<
                "No suitable ledgers were found in" <<
                " the sqlite database to import";
            return;
        }
    }

    // Import the shards
    for (std::uint32_t shardIndex = earliestIndex;
        shardIndex <= latestIndex; ++shardIndex)
    {
        if (usedDiskSpace_ + avgShardSz_ > maxDiskSpace_)
        {
            JLOG(j_.error()) <<
                "Maximum size reached";
            canAdd_ = false;
            break;
        }
        if (avgShardSz_ > available())
        {
            JLOG(j_.error()) <<
                "Insufficient disk space";
            canAdd_ = false;
            break;
        }

        // Skip if already stored
        if (complete_.find(shardIndex) != complete_.end() ||
            (incomplete_ && incomplete_->index() == shardIndex))
        {
            JLOG(j_.debug()) <<
                "shard " << shardIndex <<
                " already exists";
            continue;
        }

        // Verify sqlite ledgers are in the node store
        {
            auto const firstSeq {firstLedgerSeq(shardIndex)};
            auto const lastSeq {std::max(firstSeq, lastLedgerSeq(shardIndex))};
            auto const numLedgers {shardIndex == earliestShardIndex()
                ? lastSeq - firstSeq + 1 : ledgersPerShard_};
            auto ledgerHashes{getHashesByIndex(firstSeq, lastSeq, app_)};
            if (ledgerHashes.size() != numLedgers)
                continue;

            bool valid {true};
            for (std::uint32_t n = firstSeq; n <= lastSeq; n += 256)
            {
                if (!source.fetch(ledgerHashes[n].first, n))
                {
                    JLOG(j_.warn()) <<
                        "SQL DB ledger sequence " << n <<
                        " mismatches node store";
                    valid = false;
                    break;
                }
            }
            if (!valid)
                continue;
        }

        // Create the new shard
        app_.shardFamily()->reset();
        auto const shardDir {dir_ / std::to_string(shardIndex)};
        auto shard = std::make_unique<Shard>(
            *this, shardIndex, shardCacheSz, cacheAge_, j_);
        if (!shard->open(
                app_.config().section(ConfigSection::shardDatabase()),
                scheduler_,
                *ctx_))
        {
            shard.reset();
            continue;
        }

        // Create a marker file to signify an import in progress
        auto const markerFile {shardDir / importMarker_};
        std::ofstream ofs {markerFile.string()};
        if (!ofs.is_open())
        {
            JLOG(j_.error()) <<
                "shard " << shardIndex <<
                " unable to create temp marker file";
            shard.reset();
            removeAll(shardDir, j_);
            continue;
        }
        ofs.close();

        // Copy the ledgers from node store
        while (auto seq = shard->prepare())
        {
            auto ledger = loadByIndex(*seq, app_, false);
            if (!ledger || ledger->info().seq != seq ||
                !Database::copyLedger(*shard->getBackend(), *ledger,
                    nullptr, nullptr, shard->lastStored()))
                break;

            auto const before {shard->fileSize()};
            if (!shard->setStored(ledger))
                break;
            auto const after {shard->fileSize()};
            if (after > before)
                usedDiskSpace_ += (after - before);
            else if(after < before)
                usedDiskSpace_ -= std::min(before - after, usedDiskSpace_);

            if (shard->complete())
            {
                JLOG(j_.debug()) <<
                    "shard " << shardIndex <<
                    " successfully imported";
                removeAll(markerFile, j_);
                break;
            }
        }

        if (!shard->complete())
        {
            JLOG(j_.error()) <<
                "shard " << shardIndex <<
                " failed to import";
            shard.reset();
            removeAll(shardDir, j_);
        }
    }

    // Re initialize the shard store
    init_ = false;
    complete_.clear();
    incomplete_.reset();
    usedDiskSpace_ = 0;
    lock.unlock();

    if (!init())
        Throw<std::runtime_error>("Failed to initialize");
}

std::int32_t
DatabaseShardImp::getWriteLoad() const
{
    std::int32_t wl {0};
    {
        std::lock_guard<std::mutex> lock(m_);
        assert(init_);
        for (auto const& c : complete_)
            wl += c.second->getBackend()->getWriteLoad();
        if (incomplete_)
            wl += incomplete_->getBackend()->getWriteLoad();
    }
    return wl;
}

void
DatabaseShardImp::store(NodeObjectType type,
    Blob&& data, uint256 const& hash, std::uint32_t seq)
{
#if RIPPLE_VERIFY_NODEOBJECT_KEYS
    assert(hash == sha512Hash(makeSlice(data)));
#endif
    std::shared_ptr<NodeObject> nObj;
    auto const shardIndex {seqToShardIndex(seq)};
    {
        std::lock_guard<std::mutex> lock(m_);
        assert(init_);
        if (!incomplete_ || shardIndex != incomplete_->index())
        {
            JLOG(j_.warn()) <<
               "ledger seq " << seq <<
                " is not being acquired";
            return;
        }
        nObj = NodeObject::createObject(
            type, std::move(data), hash);
        incomplete_->pCache()->canonicalize(hash, nObj, true);
        incomplete_->getBackend()->store(nObj);
        incomplete_->nCache()->erase(hash);
    }
    storeStats(nObj->getData().size());
}

std::shared_ptr<NodeObject>
DatabaseShardImp::fetch(uint256 const& hash, std::uint32_t seq)
{
    auto cache {selectCache(seq)};
    if (cache.first)
        return doFetch(hash, seq, *cache.first, *cache.second, false);
    return {};
}

bool
DatabaseShardImp::asyncFetch(uint256 const& hash,
    std::uint32_t seq, std::shared_ptr<NodeObject>& object)
{
    auto cache {selectCache(seq)};
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
DatabaseShardImp::copyLedger(std::shared_ptr<Ledger const> const& ledger)
{
    auto const shardIndex {seqToShardIndex(ledger->info().seq)};
    std::lock_guard<std::mutex> lock(m_);
    assert(init_);
    if (!incomplete_ || shardIndex != incomplete_->index())
    {
        JLOG(j_.warn()) <<
            "source ledger seq " << ledger->info().seq <<
            " is not being acquired";
        return false;
    }

    if (!Database::copyLedger(*incomplete_->getBackend(), *ledger,
        incomplete_->pCache(), incomplete_->nCache(),
        incomplete_->lastStored()))
    {
        return false;
    }

    auto const before {incomplete_->fileSize()};
    if (!incomplete_->setStored(ledger))
        return false;
    auto const after {incomplete_->fileSize()};
     if(after > before)
         usedDiskSpace_ += (after - before);
     else if(after < before)
         usedDiskSpace_ -= std::min(before - after, usedDiskSpace_);

    if (incomplete_->complete())
    {
        complete_.emplace(incomplete_->index(), std::move(incomplete_));
        incomplete_.reset();
        updateStats(lock);
    }
    return true;
}

int
DatabaseShardImp::getDesiredAsyncReadCount(std::uint32_t seq)
{
    auto const shardIndex {seqToShardIndex(seq)};
    {
        std::lock_guard<std::mutex> lock(m_);
        assert(init_);
        auto it = complete_.find(shardIndex);
        if (it != complete_.end())
            return it->second->pCache()->getTargetSize() / asyncDivider;
        if (incomplete_ && incomplete_->index() == shardIndex)
            return incomplete_->pCache()->getTargetSize() / asyncDivider;
    }
    return cacheTargetSize / asyncDivider;
}

float
DatabaseShardImp::getCacheHitRate()
{
    float sz, f {0};
    {
        std::lock_guard<std::mutex> lock(m_);
        assert(init_);
        sz = complete_.size();
        for (auto const& c : complete_)
            f += c.second->pCache()->getHitRate();
        if (incomplete_)
        {
            f += incomplete_->pCache()->getHitRate();
            ++sz;
        }
    }
    return f / std::max(1.0f, sz);
}

void
DatabaseShardImp::tune(int size, std::chrono::seconds age)
{
    std::lock_guard<std::mutex> lock(m_);
    assert(init_);
    cacheSz_ = size;
    cacheAge_ = age;
    int const sz {calcTargetCacheSz(lock)};
    for (auto const& c : complete_)
    {
        c.second->pCache()->setTargetSize(sz);
        c.second->pCache()->setTargetAge(cacheAge_);
        c.second->nCache()->setTargetSize(sz);
        c.second->nCache()->setTargetAge(cacheAge_);
    }
    if (incomplete_)
    {
        incomplete_->pCache()->setTargetSize(sz);
        incomplete_->pCache()->setTargetAge(cacheAge_);
        incomplete_->nCache()->setTargetSize(sz);
        incomplete_->nCache()->setTargetAge(cacheAge_);
    }
}

void
DatabaseShardImp::sweep()
{
    std::lock_guard<std::mutex> lock(m_);
    assert(init_);
    int const sz {calcTargetCacheSz(lock)};
    for (auto const& c : complete_)
    {
        c.second->pCache()->sweep();
        c.second->nCache()->sweep();
        if (c.second->pCache()->getTargetSize() > sz)
            c.second->pCache()->setTargetSize(sz);
    }
    if (incomplete_)
    {
        incomplete_->pCache()->sweep();
        incomplete_->nCache()->sweep();
        if (incomplete_->pCache()->getTargetSize() > sz)
            incomplete_->pCache()->setTargetSize(sz);
    }
}

std::shared_ptr<NodeObject>
DatabaseShardImp::fetchFrom(uint256 const& hash, std::uint32_t seq)
{
    auto const shardIndex {seqToShardIndex(seq)};
    std::unique_lock<std::mutex> lock(m_);
    assert(init_);
    {
        auto it = complete_.find(shardIndex);
        if (it != complete_.end())
        {
            lock.unlock();
            return fetchInternal(hash, *it->second->getBackend());
        }
    }
    if (incomplete_ && incomplete_->index() == shardIndex)
    {
        lock.unlock();
        return fetchInternal(hash, *incomplete_->getBackend());
    }

    // Used to validate import shards
    auto it = preShards_.find(shardIndex);
    if (it != preShards_.end() && it->second)
    {
        lock.unlock();
        return fetchInternal(hash, *it->second->getBackend());
    }
    return {};
}

boost::optional<std::uint32_t>
DatabaseShardImp::findShardIndexToAdd(
    std::uint32_t validLedgerSeq, std::lock_guard<std::mutex>&)
{
    auto maxShardIndex {seqToShardIndex(validLedgerSeq)};
    if (validLedgerSeq != lastLedgerSeq(maxShardIndex))
        --maxShardIndex;

    auto const numShards {complete_.size() + (incomplete_ ? 1 : 0)};
    // If equal, have all the shards
    if (numShards >= maxShardIndex + 1)
        return boost::none;

    if (maxShardIndex < 1024 || float(numShards) / maxShardIndex > 0.5f)
    {
        // Small or mostly full index space to sample
        // Find the available indexes and select one at random
        std::vector<std::uint32_t> available;
        available.reserve(maxShardIndex - numShards + 1);
        for (std::uint32_t i = earliestShardIndex(); i <= maxShardIndex; ++i)
        {
            if (complete_.find(i) == complete_.end() &&
                (!incomplete_ || incomplete_->index() != i) &&
                preShards_.find(i) == preShards_.end())
                    available.push_back(i);
        }
        if (!available.empty())
            return available[rand_int(0u,
                static_cast<std::uint32_t>(available.size() - 1))];
    }

    // Large, sparse index space to sample
    // Keep choosing indexes at random until an available one is found
    // chances of running more than 30 times is less than 1 in a billion
    for (int i = 0; i < 40; ++i)
    {
        auto const r {rand_int(earliestShardIndex(), maxShardIndex)};
        if (complete_.find(r) == complete_.end() &&
            (!incomplete_ || incomplete_->index() != r) &&
            preShards_.find(r) == preShards_.end())
                return r;
    }
    assert(0);
    return boost::none;
}

void
DatabaseShardImp::updateStats(std::lock_guard<std::mutex>&)
{
    // Calculate shard file sizes and update status string
    std::uint32_t filesPerShard {0};
    if (!complete_.empty())
    {
        status_.clear();
        filesPerShard = complete_.begin()->second->fdlimit();
        std::uint64_t avgShardSz {0};
        for (auto it = complete_.begin(); it != complete_.end(); ++it)
        {
            if (it == complete_.begin())
                status_ = std::to_string(it->first);
            else
            {
                if (it->first - std::prev(it)->first > 1)
                {
                    if (status_.back() == '-')
                        status_ += std::to_string(std::prev(it)->first);
                    status_ += "," + std::to_string(it->first);
                }
                else
                {
                    if (status_.back() != '-')
                        status_ += "-";
                    if (std::next(it) == complete_.end())
                        status_ += std::to_string(it->first);
                }
            }
            avgShardSz += it->second->fileSize();
        }
        if (backed_)
            avgShardSz_ = avgShardSz / complete_.size();
    }
    else if(incomplete_)
        filesPerShard = incomplete_->fdlimit();

    if (!backed_)
        return;

    fdLimit_ = 1 + (filesPerShard *
        (complete_.size() + (incomplete_ ? 1 : 0)));

    if (usedDiskSpace_ >= maxDiskSpace_)
    {
        JLOG(j_.warn()) <<
            "Maximum size reached";
        canAdd_ = false;
    }
    else
    {
        auto const sz = maxDiskSpace_ - usedDiskSpace_;
        if (sz > available())
        {
            JLOG(j_.warn()) <<
                "Max Shard Store size exceeds "
                "remaining free disk space";
        }
        fdLimit_ += (filesPerShard * (sz / avgShardSz_));
    }
}

std::pair<std::shared_ptr<PCache>, std::shared_ptr<NCache>>
DatabaseShardImp::selectCache(std::uint32_t seq)
{
    auto const shardIndex {seqToShardIndex(seq)};
    std::lock_guard<std::mutex> lock(m_);
    assert(init_);
    {
        auto it = complete_.find(shardIndex);
        if (it != complete_.end())
        {
            return std::make_pair(it->second->pCache(),
                it->second->nCache());
        }
    }
    if (incomplete_ && incomplete_->index() == shardIndex)
    {
        return std::make_pair(incomplete_->pCache(),
            incomplete_->nCache());
    }

    // Used to validate import shards
    auto it = preShards_.find(shardIndex);
    if (it != preShards_.end() && it->second)
    {
        return std::make_pair(it->second->pCache(),
            it->second->nCache());
    }
    return {};
}

std::uint64_t
DatabaseShardImp::available() const
{
    try
    {
        return boost::filesystem::space(dir_).available;
    }
    catch (std::exception const& e)
    {
        JLOG(j_.error()) <<
            "exception: " << e.what();
        return 0;
    }
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
    Section const& section {
        app.config().section(ConfigSection::shardDatabase())};
    if (section.empty())
        return nullptr;

    auto shardStore = std::make_unique<DatabaseShardImp>(
        app,
        parent,
        "ShardStore",
        scheduler,
        readThreads,
        j);
    if (shardStore->init())
        shardStore->setParent(parent);
    else
        shardStore.reset();

    return shardStore;
}

} // NodeStore
} // ripple
