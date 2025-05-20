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

#include <xrpld/nodestore/detail/DatabaseNodeImp.h>

namespace ripple {
namespace NodeStore {

void
DatabaseNodeImp::store(
    NodeObjectType type,
    Blob&& data,
    uint256 const& hash,
    std::uint32_t)
{
    storeStats(1, data.size());

    auto obj = NodeObject::createObject(type, std::move(data), hash);
    backend_->store(obj);
}

void
DatabaseNodeImp::asyncFetch(
    uint256 const& hash,
    std::uint32_t ledgerSeq,
    std::function<void(std::shared_ptr<NodeObject> const&)>&& callback)
{
    Database::asyncFetch(hash, ledgerSeq, std::move(callback));
}

void
DatabaseNodeImp::sweep()
{
}

std::shared_ptr<NodeObject>
DatabaseNodeImp::fetchNodeObject(
    uint256 const& hash,
    std::uint32_t,
    FetchReport& fetchReport,
    bool duplicate)
{
    std::shared_ptr<NodeObject> nodeObject = nullptr;
    Status status;

    try
    {
        status = backend_->fetch(hash.data(), &nodeObject);
    }
    catch (std::exception const& e)
    {
        JLOG(j_.fatal()) << "fetchNodeObject " << hash
                         << ": Exception fetching from backend: " << e.what();
        Rethrow();
    }

    switch (status)
    {
        case ok:
        case notFound:
            break;
        case dataCorrupt:
            JLOG(j_.fatal()) << "fetchNodeObject " << hash
                             << ": nodestore data is corrupted";
            break;
        default:
            JLOG(j_.warn()) << "fetchNodeObject " << hash
                            << ": backend returns unknown result " << status;
            break;
    }

    if (nodeObject)
        fetchReport.wasFound = true;

    return nodeObject;
}

std::vector<std::shared_ptr<NodeObject>>
DatabaseNodeImp::fetchBatch(std::vector<uint256> const& hashes)
{
    using namespace std::chrono;
    auto const before = steady_clock::now();

    std::vector<uint256 const*> batch{hashes.size()};
    for (size_t i = 0; i < hashes.size(); ++i)
    {
        auto const& hash = hashes[i];
        batch.push_back(&hash);
    }

    std::vector<std::shared_ptr<NodeObject>> results{hashes.size()};
    results = backend_->fetchBatch(batch).first;
    for (size_t i = 0; i < results.size(); ++i)
    {
        if (!results[i])
        {
            JLOG(j_.error())
                << "fetchBatch - "
                << "record not found in db. hash = " << strHex(hashes[i]);
        }
    }

    auto fetchDurationUs =
        std::chrono::duration_cast<std::chrono::microseconds>(
            steady_clock::now() - before)
            .count();
    updateFetchMetrics(hashes.size(), 0, fetchDurationUs);
    return results;
}

}  // namespace NodeStore
}  // namespace ripple
