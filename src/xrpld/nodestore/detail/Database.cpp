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

#include <xrpld/nodestore/Database.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/core/CurrentThreadName.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/jss.h>
#include <chrono>

namespace ripple {
namespace NodeStore {

Database::Database(
    Scheduler& scheduler,
    int readThreads,
    Section const& config,
    beast::Journal journal)
    : j_(journal)
    , scheduler_(scheduler)
    , earliestLedgerSeq_(
          get<std::uint32_t>(config, "earliest_seq", XRP_LEDGER_EARLIEST_SEQ))
    , requestBundle_(get<int>(config, "rq_bundle", 4))
    , readThreads_(std::max(1, readThreads))
{
    ASSERT(
        readThreads != 0,
        "ripple::NodeStore::Database::Database : nonzero threads input");

    if (earliestLedgerSeq_ < 1)
        Throw<std::runtime_error>("Invalid earliest_seq");

    if (requestBundle_ < 1 || requestBundle_ > 64)
        Throw<std::runtime_error>("Invalid rq_bundle");

    for (int i = readThreads_.load(); i != 0; --i)
    {
        std::thread t(
            [this](int i) {
                runningThreads_++;

                beast::setCurrentThreadName(
                    "db prefetch #" + std::to_string(i));

                decltype(read_) read;

                while (true)
                {
                    {
                        std::unique_lock<std::mutex> lock(readLock_);

                        if (isStopping())
                            break;

                        if (read_.empty())
                        {
                            runningThreads_--;
                            readCondVar_.wait(lock);
                            runningThreads_++;
                        }

                        if (isStopping())
                            break;

                        // extract multiple object at a time to minimize the
                        // overhead of acquiring the mutex.
                        for (int cnt = 0;
                             !read_.empty() && cnt != requestBundle_;
                             ++cnt)
                            read.insert(read_.extract(read_.begin()));
                    }

                    for (auto it = read.begin(); it != read.end(); ++it)
                    {
                        ASSERT(
                            !it->second.empty(),
                            "ripple::NodeStore::Database::Database : non-empty "
                            "data");

                        auto const& hash = it->first;
                        auto const& data = it->second;
                        auto const seqn = data[0].first;

                        auto obj =
                            fetchNodeObject(hash, seqn, FetchType::async);

                        // This could be further optimized: if there are
                        // multiple requests for sequence numbers mapping to
                        // multiple databases by sorting requests such that all
                        // indices mapping to the same database are grouped
                        // together and serviced by a single read.
                        for (auto const& req : data)
                        {
                            req.second(
                                (seqn == req.first) || isSameDB(req.first, seqn)
                                    ? obj
                                    : fetchNodeObject(
                                          hash, req.first, FetchType::async));
                        }
                    }

                    read.clear();
                }

                --runningThreads_;
                --readThreads_;
            },
            i);
        t.detach();
    }
}

Database::~Database()
{
    // NOTE!
    // Any derived class should call the stop() method in its
    // destructor.  Otherwise, occasionally, the derived class may
    // crash during shutdown when its members are accessed by one of
    // these threads after the derived class is destroyed but before
    // this base class is destroyed.
    stop();
}

bool
Database::isStopping() const
{
    return readStopping_.load(std::memory_order_relaxed);
}

void
Database::stop()
{
    {
        std::lock_guard lock(readLock_);

        if (!readStopping_.exchange(true, std::memory_order_relaxed))
        {
            JLOG(j_.debug()) << "Clearing read queue because of stop request";
            read_.clear();
            readCondVar_.notify_all();
        }
    }

    JLOG(j_.debug()) << "Waiting for stop request to complete...";

    using namespace std::chrono;

    auto const start = steady_clock::now();

    while (readThreads_.load() != 0)
    {
        ASSERT(
            steady_clock::now() - start < 30s,
            "ripple::NodeStore::Database::stop : maximum stop duration");
        std::this_thread::yield();
    }

    JLOG(j_.debug()) << "Stop request completed in "
                     << duration_cast<std::chrono::milliseconds>(
                            steady_clock::now() - start)
                            .count()
                     << " millseconds";
}

void
Database::asyncFetch(
    uint256 const& hash,
    std::uint32_t ledgerSeq,
    std::function<void(std::shared_ptr<NodeObject> const&)>&& cb)
{
    std::lock_guard lock(readLock_);

    if (!isStopping())
    {
        read_[hash].emplace_back(ledgerSeq, std::move(cb));
        readCondVar_.notify_one();
    }
}

void
Database::importInternal(Backend& dstBackend, Database& srcDB)
{
    Batch batch;
    batch.reserve(batchWritePreallocationSize);
    auto storeBatch = [&, fname = __func__]() {
        try
        {
            dstBackend.storeBatch(batch);
        }
        catch (std::exception const& e)
        {
            JLOG(j_.error()) << "Exception caught in function " << fname
                             << ". Error: " << e.what();
            return;
        }

        std::uint64_t sz{0};
        for (auto const& nodeObject : batch)
            sz += nodeObject->getData().size();
        storeStats(batch.size(), sz);
        batch.clear();
    };

    srcDB.for_each([&](std::shared_ptr<NodeObject> nodeObject) {
        ASSERT(
            nodeObject != nullptr,
            "ripple::NodeStore::Database::importInternal : non-null node");
        if (!nodeObject)  // This should never happen
            return;

        batch.emplace_back(std::move(nodeObject));
        if (batch.size() >= batchWritePreallocationSize)
            storeBatch();
    });

    if (!batch.empty())
        storeBatch();
}

// Perform a fetch and report the time it took
std::shared_ptr<NodeObject>
Database::fetchNodeObject(
    uint256 const& hash,
    std::uint32_t ledgerSeq,
    FetchType fetchType,
    bool duplicate)
{
    FetchReport fetchReport(fetchType);

    using namespace std::chrono;
    auto const begin{steady_clock::now()};

    auto nodeObject{fetchNodeObject(hash, ledgerSeq, fetchReport, duplicate)};
    auto dur = steady_clock::now() - begin;
    fetchDurationUs_ += duration_cast<microseconds>(dur).count();
    if (nodeObject)
    {
        ++fetchHitCount_;
        fetchSz_ += nodeObject->getData().size();
    }
    ++fetchTotalCount_;

    fetchReport.elapsed = duration_cast<milliseconds>(dur);
    scheduler_.onFetch(fetchReport);
    return nodeObject;
}

void
Database::getCountsJson(Json::Value& obj)
{
    ASSERT(
        obj.isObject(),
        "ripple::NodeStore::Database::getCountsJson : valid input type");

    {
        std::unique_lock<std::mutex> lock(readLock_);
        obj["read_queue"] = static_cast<Json::UInt>(read_.size());
    }

    obj["read_threads_total"] = readThreads_.load();
    obj["read_threads_running"] = runningThreads_.load();
    obj["read_request_bundle"] = requestBundle_;

    obj[jss::node_writes] = std::to_string(storeCount_);
    obj[jss::node_reads_total] = std::to_string(fetchTotalCount_);
    obj[jss::node_reads_hit] = std::to_string(fetchHitCount_);
    obj[jss::node_written_bytes] = std::to_string(storeSz_);
    obj[jss::node_read_bytes] = std::to_string(fetchSz_);
    obj[jss::node_reads_duration_us] = std::to_string(fetchDurationUs_);
}

}  // namespace NodeStore
}  // namespace ripple
