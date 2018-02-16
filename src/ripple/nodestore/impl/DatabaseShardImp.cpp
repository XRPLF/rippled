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

#include <BeastConfig.h>

#include <ripple/nodestore/impl/DatabaseShardImp.h>
#include <ripple/app/ledger/InboundLedgers.h>
#include <ripple/app/ledger/Ledger.h>
#include <ripple/basics/chrono.h>
#include <ripple/basics/random.h>
#include <ripple/nodestore/Manager.h>
#include <ripple/protocol/HashPrefix.h>

namespace ripple {
namespace NodeStore {

constexpr std::uint32_t DatabaseShard::ledgersPerShardDefault;

DatabaseShardImp::DatabaseShardImp(Application& app,
    std::string const& name, Stoppable& parent, Scheduler& scheduler,
    int readThreads, Section const& config, beast::Journal j)
    : DatabaseShard(name, parent, scheduler, readThreads, config, j)
    , app_(app)
    , config_(config)
    , dir_(get<std::string>(config, "path"))
    , maxDiskSpace_(get<std::uint64_t>(config, "max_size_gb") << 30)
    , ledgersPerShard_(get<std::uint32_t>(
        config, "ledgers_per_shard", ledgersPerShardDefault))
    , earliestShardIndex_(seqToShardIndex(earliestSeq()))
    , avgShardSz_(ledgersPerShard() * (192 * 1024))
{
    if (ledgersPerShard_ == 0 || ledgersPerShard_ % 256 != 0)
        Throw<std::runtime_error>(
            "ledgers_per_shard must be a multiple of 256");
}

DatabaseShardImp::~DatabaseShardImp()
{
    // Stop threads before data members are destroyed
    stopThreads();
}

bool
DatabaseShardImp::init()
{
    std::lock_guard<std::mutex> l(m_);
    if (init_)
    {
        JLOG(j_.error()) <<
            "Already initialized";
        return false;
    }

    using namespace boost::filesystem;
    // Find backend type and file handle requirement
    try
    {
        fdLimit_ = Manager::instance().make_Backend(
            config_, scheduler_, j_)->fdlimit();
    }
    catch (std::exception const&)
    {
        JLOG(j_.error()) <<
            "Invalid or missing shard store "
            "type specified in [shard_db]";
        return false;
    }

    backed_ = static_cast<bool>(fdLimit_);
    if (!backed_)
    {
        init_ = true;
        return true;
    }

    // Find shards
    for (auto const& d : directory_iterator(dir_))
    {
        if (!is_directory(d))
            continue;
        auto dirName = d.path().stem().string();
        if (!std::all_of(dirName.begin(), dirName.end(), ::isdigit))
            continue;
        auto const shardIndex {std::stoul(dirName)};
        if (shardIndex < earliestShardIndex())
        {
            JLOG(j_.fatal()) <<
                "Invalid shard index " << shardIndex <<
                ". Earliest shard index " << earliestShardIndex();
            return false;
        }
        auto shard = std::make_unique<Shard>(
            *this, shardIndex, cacheSz_, cacheAge_, j_);
        if (!shard->open(config_, scheduler_, dir_))
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
    if (!incomplete_ && complete_.empty())
    {
        // New Shard Store, calculate file descriptor requirements
        if (maxDiskSpace_ > space(dir_).free)
        {
            JLOG(j_.error()) <<
                "Insufficient disk space";
        }
        fdLimit_ = 1 + (fdLimit_ *
            std::max<std::uint64_t>(1, maxDiskSpace_ / avgShardSz_));
    }
    else
        updateStats(l);
    init_ = true;
    return true;
}

boost::optional<std::uint32_t>
DatabaseShardImp::prepare(std::uint32_t validLedgerSeq)
{
    std::lock_guard<std::mutex> l(m_);
    assert(init_);
    if (incomplete_)
        return incomplete_->prepare();
    if (!canAdd_)
        return boost::none;
    if (backed_)
    {
        // Create a new shard to acquire
        if (usedDiskSpace_ + avgShardSz_ > maxDiskSpace_)
        {
            JLOG(j_.debug()) <<
                "Maximum size reached";
            canAdd_ = false;
            return boost::none;
        }
        if (avgShardSz_ > boost::filesystem::space(dir_).free)
        {
            JLOG(j_.warn()) <<
                "Insufficient disk space";
            canAdd_ = false;
            return boost::none;
        }
    }

    auto const shardIndex {findShardIndexToAdd(validLedgerSeq, l)};
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
    if (!incomplete_->open(config_, scheduler_, dir_))
    {
        incomplete_.reset();
        remove_all(dir_ / std::to_string(*shardIndex));
        return boost::none;
    }
    return incomplete_->prepare();
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
    std::lock_guard<std::mutex> l(m_);
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
        updateStats(l);
    }
}

bool
DatabaseShardImp::contains(std::uint32_t seq)
{
    auto const shardIndex {seqToShardIndex(seq)};
    std::lock_guard<std::mutex> l(m_);
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
    std::lock_guard<std::mutex> l(m_);
    assert(init_);
    return status_;
}

void
DatabaseShardImp::validate()
{
    {
        std::lock_guard<std::mutex> l(m_);
        assert(init_);
        if (complete_.empty() && !incomplete_)
        {
            JLOG(j_.fatal()) <<
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
        JLOG(j_.fatal()) << s;
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

std::int32_t
DatabaseShardImp::getWriteLoad() const
{
    std::int32_t wl {0};
    {
        std::lock_guard<std::mutex> l(m_);
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
        std::lock_guard<std::mutex> l(m_);
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
        return doFetch(hash, seq, cache.first, cache.second, false);
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
    if (ledger->info().hash.isZero() ||
        ledger->info().accountHash.isZero())
    {
        assert(false);
        JLOG(j_.error()) <<
            "source ledger seq " << ledger->info().seq <<
            " is invalid";
        return false;
    }
    auto& srcDB = const_cast<Database&>(
        ledger->stateMap().family().db());
    if (&srcDB == this)
    {
        assert(false);
        JLOG(j_.error()) <<
            "same source and destination databases";
        return false;
    }
    auto const shardIndex {seqToShardIndex(ledger->info().seq)};
    std::lock_guard<std::mutex> l(m_);
    assert(init_);
    if (!incomplete_ || shardIndex != incomplete_->index())
    {
        JLOG(j_.warn()) <<
            "source ledger seq " << ledger->info().seq <<
            " is not being acquired";
        return false;
    }

    // Store the ledger header
    {
        Serializer s(1024);
        s.add32(HashPrefix::ledgerMaster);
        addRaw(ledger->info(), s);
        auto nObj = NodeObject::createObject(hotLEDGER,
            std::move(s.modData()), ledger->info().hash);
#if RIPPLE_VERIFY_NODEOBJECT_KEYS
        assert(nObj->getHash() == sha512Hash(makeSlice(nObj->getData())));
#endif
        incomplete_->pCache()->canonicalize(
            nObj->getHash(), nObj, true);
        incomplete_->getBackend()->store(nObj);
        incomplete_->nCache()->erase(nObj->getHash());
        storeStats(nObj->getData().size());
    }
    auto next = incomplete_->lastStored();
    bool error = false;
    auto f = [&](SHAMapAbstractNode& node) {
        if (auto nObj = srcDB.fetch(
            node.getNodeHash().as_uint256(), ledger->info().seq))
        {
#if RIPPLE_VERIFY_NODEOBJECT_KEYS
            assert(nObj->getHash() == sha512Hash(makeSlice(nObj->getData())));
#endif
            incomplete_->pCache()->canonicalize(
                nObj->getHash(), nObj, true);
            incomplete_->getBackend()->store(nObj);
            incomplete_->nCache()->erase(nObj->getHash());
            storeStats(nObj->getData().size());
        }
        else
            error = true;
        return !error;
    };
    // Store the state map
    if (ledger->stateMap().getHash().isNonZero())
    {
        if (!ledger->stateMap().isValid())
        {
            JLOG(j_.error()) <<
                "source ledger seq " << ledger->info().seq <<
                " state map invalid";
            return false;
        }
        if (next && next->info().parentHash == ledger->info().hash)
        {
            auto have = next->stateMap().snapShot(false);
            ledger->stateMap().snapShot(false)->visitDifferences(&(*have), f);
        }
        else
            ledger->stateMap().snapShot(false)->visitNodes(f);
        if (error)
            return false;
    }
    // Store the transaction map
    if (ledger->info().txHash.isNonZero())
    {
        if (!ledger->txMap().isValid())
        {
            JLOG(j_.error()) <<
                "source ledger seq " << ledger->info().seq <<
                " transaction map invalid";
            return false;
        }
        ledger->txMap().snapShot(false)->visitNodes(f);
        if (error)
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
        updateStats(l);
    }
    return true;
}

int
DatabaseShardImp::getDesiredAsyncReadCount(std::uint32_t seq)
{
    auto const shardIndex {seqToShardIndex(seq)};
    {
        std::lock_guard<std::mutex> l(m_);
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
        std::lock_guard<std::mutex> l(m_);
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
DatabaseShardImp::tune(int size, int age)
{
    std::lock_guard<std::mutex> l(m_);
    assert(init_);
    cacheSz_ = size;
    cacheAge_ = age;
    int const sz {calcTargetCacheSz(l)};
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
    std::lock_guard<std::mutex> l(m_);
    assert(init_);
    int const sz {calcTargetCacheSz(l)};
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
    std::shared_ptr<Backend> backend;
    auto const shardIndex {seqToShardIndex(seq)};
    {
        std::unique_lock<std::mutex> l(m_);
        assert(init_);
        auto it = complete_.find(shardIndex);
        if (it != complete_.end())
            backend = it->second->getBackend();
        else if (incomplete_ && incomplete_->index() == shardIndex)
            backend = incomplete_->getBackend();
        else
            return {};
    }
    return fetchInternal(hash, *backend);
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
                (!incomplete_ || incomplete_->index() != i))
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
        auto const r = rand_int(earliestShardIndex(), maxShardIndex);
        if (complete_.find(r) == complete_.end() &&
            (!incomplete_ || incomplete_->index() != r))
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
        if (sz > space(dir_).free)
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
    std::pair<std::shared_ptr<PCache>,
        std::shared_ptr<NCache>> cache;
    auto const shardIndex {seqToShardIndex(seq)};
    {
        std::lock_guard<std::mutex> l(m_);
        assert(init_);
        auto it = complete_.find(shardIndex);
        if (it != complete_.end())
            cache = std::make_pair(it->second->pCache(),
                it->second->nCache());
        else if (incomplete_ && incomplete_->index() == shardIndex)
            cache = std::make_pair(incomplete_->pCache(),
                incomplete_->nCache());
    }
    return cache;
}


} // NodeStore
} // ripple
