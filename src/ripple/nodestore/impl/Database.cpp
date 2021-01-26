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

#include <ripple/app/ledger/Ledger.h>
#include <ripple/basics/chrono.h>
#include <ripple/beast/core/CurrentThreadName.h>
#include <ripple/json/json_value.h>
#include <ripple/nodestore/Database.h>
#include <ripple/protocol/HashPrefix.h>
#include <ripple/protocol/jss.h>
#include <chrono>

namespace ripple {
namespace NodeStore {

Database::Database(
    std::string name,
    Stoppable& parent,
    Scheduler& scheduler,
    int readThreads,
    Section const& config,
    beast::Journal journal)
    : Stoppable(name, parent.getRoot())
    , j_(journal)
    , scheduler_(scheduler)
    , earliestLedgerSeq_(
          get<std::uint32_t>(config, "earliest_seq", XRP_LEDGER_EARLIEST_SEQ))
{
    if (earliestLedgerSeq_ < 1)
        Throw<std::runtime_error>("Invalid earliest_seq");

    while (readThreads-- > 0)
        readThreads_.emplace_back(&Database::threadEntry, this);
}

Database::~Database()
{
    // NOTE!
    // Any derived class should call the stopReadThreads() method in its
    // destructor.  Otherwise, occasionally, the derived class may
    // crash during shutdown when its members are accessed by one of
    // these threads after the derived class is destroyed but before
    // this base class is destroyed.
    stopReadThreads();
}

void
Database::onStop()
{
    // After stop time we can no longer use the JobQueue for background
    // reads.  Join the background read threads.
    stopReadThreads();
}

void
Database::onChildrenStopped()
{
    stopped();
}

void
Database::stopReadThreads()
{
    {
        std::lock_guard lock(readLock_);
        if (readShut_)  // Only stop threads once.
            return;

        readShut_ = true;
        readCondVar_.notify_all();
    }

    for (auto& e : readThreads_)
        e.join();
}

void
Database::asyncFetch(
    uint256 const& hash,
    std::uint32_t ledgerSeq,
    std::function<void(std::shared_ptr<NodeObject> const&)>&& cb)
{
    // Post a read
    std::lock_guard lock(readLock_);
    read_[hash].emplace_back(ledgerSeq, std::move(cb));
    readCondVar_.notify_one();
}

void
Database::importInternal(Backend& dstBackend, Database& srcDB)
{
    Batch batch;
    batch.reserve(batchWritePreallocationSize);
    auto storeBatch = [&]() {
        try
        {
            dstBackend.storeBatch(batch);
        }
        catch (std::exception const& e)
        {
            JLOG(j_.error()) << "Exception caught in function " << __func__
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
        assert(nodeObject);
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
    FetchType fetchType)
{
    FetchReport fetchReport(fetchType);

    using namespace std::chrono;
    auto const begin{steady_clock::now()};

    auto nodeObject{fetchNodeObject(hash, ledgerSeq, fetchReport)};
    if (nodeObject)
    {
        ++fetchHitCount_;
        fetchSz_ += nodeObject->getData().size();
    }
    ++fetchTotalCount_;

    fetchReport.elapsed =
        duration_cast<milliseconds>(steady_clock::now() - begin);
    scheduler_.onFetch(fetchReport);
    return nodeObject;
}

bool
Database::storeLedger(
    Ledger const& srcLedger,
    std::shared_ptr<Backend> dstBackend)
{
    auto fail = [&](std::string const& msg) {
        JLOG(j_.error()) << "Source ledger sequence " << srcLedger.info().seq
                         << ". " << msg;
        return false;
    };

    if (srcLedger.info().hash.isZero())
        return fail("Invalid hash");
    if (srcLedger.info().accountHash.isZero())
        return fail("Invalid account hash");

    auto& srcDB = const_cast<Database&>(srcLedger.stateMap().family().db());
    if (&srcDB == this)
        return fail("Source and destination databases are the same");

    Batch batch;
    batch.reserve(batchWritePreallocationSize);
    auto storeBatch = [&]() {
        std::uint64_t sz{0};
        for (auto const& nodeObject : batch)
            sz += nodeObject->getData().size();

        try
        {
            dstBackend->storeBatch(batch);
        }
        catch (std::exception const& e)
        {
            fail(
                std::string("Exception caught in function ") + __func__ +
                ". Error: " + e.what());
            return false;
        }

        storeStats(batch.size(), sz);
        batch.clear();
        return true;
    };

    // Store ledger header
    {
        Serializer s(sizeof(std::uint32_t) + sizeof(LedgerInfo));
        s.add32(HashPrefix::ledgerMaster);
        addRaw(srcLedger.info(), s);
        auto nObj = NodeObject::createObject(
            hotLEDGER, std::move(s.modData()), srcLedger.info().hash);
        batch.emplace_back(std::move(nObj));
    }

    bool error = false;
    auto visit = [&](SHAMapTreeNode& node) {
        if (!isStopping())
        {
            if (auto nodeObject = srcDB.fetchNodeObject(
                    node.getHash().as_uint256(), srcLedger.info().seq))
            {
                batch.emplace_back(std::move(nodeObject));
                if (batch.size() < batchWritePreallocationSize || storeBatch())
                    return true;
            }
        }

        error = true;
        return false;
    };

    // Store the state map
    if (srcLedger.stateMap().getHash().isNonZero())
    {
        if (!srcLedger.stateMap().isValid())
            return fail("Invalid state map");

        srcLedger.stateMap().snapShot(false)->visitNodes(visit);
        if (error)
            return fail("Failed to store state map");
    }

    // Store the transaction map
    if (srcLedger.info().txHash.isNonZero())
    {
        if (!srcLedger.txMap().isValid())
            return fail("Invalid transaction map");

        srcLedger.txMap().snapShot(false)->visitNodes(visit);
        if (error)
            return fail("Failed to store transaction map");
    }

    if (!batch.empty() && !storeBatch())
        return fail("Failed to store");

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
        std::vector<std::pair<
            std::uint32_t,
            std::function<void(std::shared_ptr<NodeObject> const&)>>>
            entry;

        {
            std::unique_lock<std::mutex> lock(readLock_);
            while (!readShut_ && read_.empty())
            {
                // All work is done
                readCondVar_.wait(lock);
            }
            if (readShut_)
                break;

            // Read in key order to make the back end more efficient
            auto it = read_.lower_bound(readLastHash_);
            if (it == read_.end())
            {
                // start over from the beginning
                it = read_.begin();
            }
            lastHash = it->first;
            entry = std::move(it->second);
            read_.erase(it);
            readLastHash_ = lastHash;
        }

        auto seq = entry[0].first;
        auto obj = fetchNodeObject(lastHash, seq, FetchType::async);

        for (auto const& req : entry)
        {
            if ((seq == req.first) || isSameDB(req.first, seq))
                req.second(obj);
            else
                req.second(
                    fetchNodeObject(lastHash, req.first, FetchType::async));
        }
    }
}

void
Database::getCountsJson(Json::Value& obj)
{
    assert(obj.isObject());
    obj[jss::node_writes] = std::to_string(storeCount_);
    obj[jss::node_reads_total] = std::to_string(fetchTotalCount_);
    obj[jss::node_reads_hit] = std::to_string(fetchHitCount_);
    obj[jss::node_written_bytes] = std::to_string(storeSz_);
    obj[jss::node_read_bytes] = std::to_string(fetchSz_);
    obj[jss::node_reads_duration_us] = std::to_string(fetchDurationUs_);
    auto const& c = getBackend().counters();
    obj[jss::node_read_errors] = std::to_string(c.readErrors);
    obj[jss::node_read_retries] = std::to_string(c.readRetries);
    obj[jss::node_write_retries] = std::to_string(c.writeRetries);
    obj[jss::node_writes_delayed] = std::to_string(c.writesDelayed);
    obj[jss::node_writes_duration_us] = std::to_string(c.writeDurationUs);
}

}  // namespace NodeStore
}  // namespace ripple
