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

#include <ripple/nodestore/Database.h>
#include <ripple/app/ledger/Ledger.h>
#include <ripple/basics/chrono.h>
#include <ripple/beast/core/CurrentThreadName.h>
#include <ripple/protocol/HashPrefix.h>

namespace ripple {
namespace NodeStore {

Database::Database(
    std::string name,
    Stoppable& parent,
    Scheduler& scheduler,
    int readThreads,
    Section const& config,
    beast::Journal journal)
    : Stoppable(name, parent)
    , j_(journal)
    , scheduler_(scheduler)
{
    std::uint32_t seq;
    if (get_if_exists<std::uint32_t>(config, "earliest_seq", seq))
    {
        if (seq < 1)
            Throw<std::runtime_error>("Invalid earliest_seq");
        earliestSeq_ = seq;
    }

    while (readThreads-- > 0)
        readThreads_.emplace_back(&Database::threadEntry, this);
}

Database::~Database()
{
    // NOTE!
    // Any derived class should call the stopThreads() method in its
    // destructor.  Otherwise, occasionally, the derived class may
    // crash during shutdown when its members are accessed by one of
    // these threads after the derived class is destroyed but before
    // this base class is destroyed.
    stopThreads();
}

void
Database::waitReads()
{
    std::unique_lock<std::mutex> l(readLock_);
    // Wake in two generations.
    // Each generation is a full pass over the space.
    // If we're in generation N and you issue a request,
    // that request will only be done during generation N
    // if it happens to land after where the pass currently is.
    // But, if not, it will definitely be done during generation
    // N+1 since the request was in the table before that pass
    // even started. So when you reach generation N+2,
    // you know the request is done.
    std::uint64_t const wakeGen = readGen_ + 2;
    while (! readShut_ && ! read_.empty() && (readGen_ < wakeGen))
        readGenCondVar_.wait(l);
}

void
Database::onStop()
{
    // After stop time we can no longer use the JobQueue for background
    // reads.  Join the background read threads.
    stopThreads();
    stopped();
}

void
Database::stopThreads()
{
    {
        std::lock_guard <std::mutex> l(readLock_);
        if (readShut_) // Only stop threads once.
            return;

        readShut_ = true;
        readCondVar_.notify_all();
        readGenCondVar_.notify_all();
    }

    for (auto& e : readThreads_)
        e.join();
}

void
Database::asyncFetch(uint256 const& hash, std::uint32_t seq,
    std::shared_ptr<TaggedCache<uint256, NodeObject>> const& pCache,
        std::shared_ptr<KeyCache<uint256>> const& nCache)
{
    // Post a read
    std::lock_guard <std::mutex> l(readLock_);
    if (read_.emplace(hash, std::make_tuple(seq, pCache, nCache)).second)
        readCondVar_.notify_one();
}

std::shared_ptr<NodeObject>
Database::fetchInternal(uint256 const& hash, Backend& srcBackend)
{
    std::shared_ptr<NodeObject> nObj;
    Status status;
    try
    {
        status = srcBackend.fetch(hash.begin(), &nObj);
    }
    catch (std::exception const& e)
    {
        JLOG(j_.fatal()) <<
            "Exception, " << e.what();
        Rethrow();
    }

    switch(status)
    {
    case ok:
        ++fetchHitCount_;
        if (nObj)
            fetchSz_ += nObj->getData().size();
        break;
    case notFound:
        break;
    case dataCorrupt:
        // VFALCO TODO Deal with encountering corrupt data!
        JLOG(j_.fatal()) <<
            "Corrupt NodeObject #" << hash;
        break;
    default:
        JLOG(j_.warn()) <<
            "Unknown status=" << status;
        break;
    }
    return nObj;
}

void
Database::importInternal(Backend& dstBackend, Database& srcDB)
{
    Batch b;
    b.reserve(batchWritePreallocationSize);
    srcDB.for_each(
        [&](std::shared_ptr<NodeObject> nObj)
        {
            assert(nObj);
            if (! nObj) // This should never happen
                return;

            ++storeCount_;
            storeSz_ += nObj->getData().size();

            b.push_back(nObj);
            if (b.size() >= batchWritePreallocationSize)
            {
                dstBackend.storeBatch(b);
                b.clear();
                b.reserve(batchWritePreallocationSize);
            }
        });
    if (! b.empty())
        dstBackend.storeBatch(b);
}

// Perform a fetch and report the time it took
std::shared_ptr<NodeObject>
Database::doFetch(uint256 const& hash, std::uint32_t seq,
    TaggedCache<uint256, NodeObject>& pCache,
        KeyCache<uint256>& nCache, bool isAsync)
{
    FetchReport report;
    report.isAsync = isAsync;
    report.wentToDisk = false;

    using namespace std::chrono;
    auto const before = steady_clock::now();

    // See if the object already exists in the cache
    auto nObj = pCache.fetch(hash);
    if (! nObj && ! nCache.touch_if_exists(hash))
    {
        // Try the database(s)
        report.wentToDisk = true;
        nObj = fetchFrom(hash, seq);
        ++fetchTotalCount_;
        if (! nObj)
        {
            // Just in case a write occurred
            nObj = pCache.fetch(hash);
            if (! nObj)
                // We give up
                nCache.insert(hash);
        }
        else
        {
            // Ensure all threads get the same object
            pCache.canonicalize(hash, nObj);

            // Since this was a 'hard' fetch, we will log it.
            JLOG(j_.trace()) <<
                "HOS: " << hash << " fetch: in db";
        }
    }
    report.wasFound = static_cast<bool>(nObj);
    report.elapsed = duration_cast<milliseconds>(
        steady_clock::now() - before);
    scheduler_.onFetch(report);
    return nObj;
}

bool
Database::copyLedger(Backend& dstBackend, Ledger const& srcLedger,
    std::shared_ptr<TaggedCache<uint256, NodeObject>> const& pCache,
        std::shared_ptr<KeyCache<uint256>> const& nCache,
            std::shared_ptr<Ledger const> const& srcNext)
{
    assert(static_cast<bool>(pCache) == static_cast<bool>(nCache));
    if (srcLedger.info().hash.isZero() ||
        srcLedger.info().accountHash.isZero())
    {
        assert(false);
        JLOG(j_.error()) <<
            "source ledger seq " << srcLedger.info().seq <<
            " is invalid";
        return false;
    }
    auto& srcDB = const_cast<Database&>(
        srcLedger.stateMap().family().db());
    if (&srcDB == this)
    {
        assert(false);
        JLOG(j_.error()) <<
            "source and destination databases are the same";
        return false;
    }

    Batch batch;
    batch.reserve(batchWritePreallocationSize);
    auto storeBatch = [&]() {
#if RIPPLE_VERIFY_NODEOBJECT_KEYS
        for (auto& nObj : batch)
        {
            assert(nObj->getHash() ==
                sha512Hash(makeSlice(nObj->getData())));
            if (pCache && nCache)
            {
                pCache->canonicalize(nObj->getHash(), nObj, true);
                nCache->erase(nObj->getHash());
                storeStats(nObj->getData().size());
            }
        }
#else
        if (pCache && nCache)
            for (auto& nObj : batch)
            {
                pCache->canonicalize(nObj->getHash(), nObj, true);
                nCache->erase(nObj->getHash());
                storeStats(nObj->getData().size());
            }
#endif
        dstBackend.storeBatch(batch);
        batch.clear();
        batch.reserve(batchWritePreallocationSize);
    };
    bool error = false;
    auto f = [&](SHAMapAbstractNode& node) {
        if (auto nObj = srcDB.fetch(
            node.getNodeHash().as_uint256(), srcLedger.info().seq))
        {
            batch.emplace_back(std::move(nObj));
            if (batch.size() >= batchWritePreallocationSize)
                storeBatch();
        }
        else
            error = true;
        return !error;
    };

    // Store ledger header
    {
        Serializer s(1024);
        s.add32(HashPrefix::ledgerMaster);
        addRaw(srcLedger.info(), s);
        auto nObj = NodeObject::createObject(hotLEDGER,
            std::move(s.modData()), srcLedger.info().hash);
        batch.emplace_back(std::move(nObj));
    }

    // Store the state map
    if (srcLedger.stateMap().getHash().isNonZero())
    {
        if (!srcLedger.stateMap().isValid())
        {
            JLOG(j_.error()) <<
                "source ledger seq " << srcLedger.info().seq <<
                " state map invalid";
            return false;
        }
        if (srcNext && srcNext->info().parentHash == srcLedger.info().hash)
        {
            auto have = srcNext->stateMap().snapShot(false);
            srcLedger.stateMap().snapShot(
                false)->visitDifferences(&(*have), f);
        }
        else
            srcLedger.stateMap().snapShot(false)->visitNodes(f);
        if (error)
            return false;
    }

    // Store the transaction map
    if (srcLedger.info().txHash.isNonZero())
    {
        if (!srcLedger.txMap().isValid())
        {
            JLOG(j_.error()) <<
                "source ledger seq " << srcLedger.info().seq <<
                " transaction map invalid";
            return false;
        }
        srcLedger.txMap().snapShot(false)->visitNodes(f);
        if (error)
            return false;
    }

    if (!batch.empty())
        storeBatch();
    return true;
}

// Entry point for async read threads
void
Database::threadEntry()
{
    beast::setCurrentThreadName("prefetch");
    while (true)
    {
        uint256 lastHash;
        std::uint32_t lastSeq;
        std::shared_ptr<TaggedCache<uint256, NodeObject>> lastPcache;
        std::shared_ptr<KeyCache<uint256>> lastNcache;
        {
            std::unique_lock<std::mutex> l(readLock_);
            while (! readShut_ && read_.empty())
            {
                // All work is done
                readGenCondVar_.notify_all();
                readCondVar_.wait(l);
            }
            if (readShut_)
                break;

            // Read in key order to make the back end more efficient
            auto it = read_.lower_bound(readLastHash_);
            if (it == read_.end())
            {
                it = read_.begin();
                // A generation has completed
                ++readGen_;
                readGenCondVar_.notify_all();
            }
            lastHash = it->first;
            lastSeq = std::get<0>(it->second);
            lastPcache = std::get<1>(it->second).lock();
            lastNcache = std::get<2>(it->second).lock();
            read_.erase(it);
            readLastHash_ = lastHash;
        }

        // Perform the read
        if (lastPcache && lastPcache)
            doFetch(lastHash, lastSeq, *lastPcache, *lastNcache, true);
    }
}

} // NodeStore
} // ripple
