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
#include <ripple/basics/chrono.h>
#include <ripple/beast/core/CurrentThreadName.h>
#include <ripple/protocol/HashPrefix.h>

namespace ripple {
namespace NodeStore {

Database::Database(std::string name, Stoppable& parent,
    Scheduler& scheduler, int readThreads, beast::Journal journal)
    : Stoppable(name, parent)
    , j_(journal)
    , scheduler_(scheduler)
{
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
Database::fetchInternal(uint256 const& hash, Backend& backend)
{
    std::shared_ptr<NodeObject> nObj;
    Status status;
    try
    {
        status = backend.fetch(hash.begin(), &nObj);
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
Database::importInternal(Database& source, Backend& dest)
{
    Batch b;
    b.reserve(batchWritePreallocationSize);
    source.for_each(
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
                dest.storeBatch(b);
                b.clear();
                b.reserve(batchWritePreallocationSize);
            }
        });
    if (! b.empty())
        dest.storeBatch(b);
}

// Perform a fetch and report the time it took
std::shared_ptr<NodeObject>
Database::doFetch(uint256 const& hash, std::uint32_t seq,
    std::shared_ptr<TaggedCache<uint256, NodeObject>> const& pCache,
        std::shared_ptr<KeyCache<uint256>> const& nCache, bool isAsync)
{
    FetchReport report;
    report.isAsync = isAsync;
    report.wentToDisk = false;

    using namespace std::chrono;
    auto const before = steady_clock::now();

    // See if the object already exists in the cache
    auto nObj = pCache->fetch(hash);
    if (! nObj && ! nCache->touch_if_exists(hash))
    {
        // Try the database(s)
        report.wentToDisk = true;
        nObj = fetchFrom(hash, seq);
        ++fetchTotalCount_;
        if (! nObj)
        {
            // Just in case a write occurred
            nObj = pCache->fetch(hash);
            if (! nObj)
                // We give up
                nCache->insert(hash);
        }
        else
        {
            // Ensure all threads get the same object
            pCache->canonicalize(hash, nObj);

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
            doFetch(lastHash, lastSeq, lastPcache, lastNcache, true);
    }
}

} // NodeStore
} // ripple
