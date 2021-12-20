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
#include <ripple/nodestore/impl/DatabaseNodeImp.h>
#include <ripple/protocol/HashPrefix.h>

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

    backend_->store(NodeObject::createObject(type, std::move(data), hash));
}

void
DatabaseNodeImp::sweep()
{
    if (cache_)
        cache_->sweep();
}

std::shared_ptr<NodeObject>
DatabaseNodeImp::fetchNodeObject(
    uint256 const& hash,
    std::uint32_t,
    FetchReport& fetchReport,
    bool duplicate)
{
    std::shared_ptr<NodeObject> nodeObject =
        cache_ ? cache_->fetch(hash) : nullptr;

    if (!nodeObject)
    {
        JLOG(j_.trace()) << "fetchNodeObject " << hash << ": record not "
                         << (cache_ ? "cached" : "found");

        Status status;

        try
        {
            status = backend_->fetch(hash.data(), &nodeObject);
        }
        catch (std::exception const& e)
        {
            JLOG(j_.fatal())
                << "fetchNodeObject " << hash
                << ": Exception fetching from backend: " << e.what();
            Rethrow();
        }

        switch (status)
        {
            case ok:
                if (nodeObject && cache_)
                    cache_->canonicalize_replace_client(hash, nodeObject);
                break;
            case notFound:
                break;
            case dataCorrupt:
                JLOG(j_.fatal()) << "fetchNodeObject " << hash
                                 << ": nodestore data is corrupted";
                break;
            default:
                JLOG(j_.warn())
                    << "fetchNodeObject " << hash
                    << ": backend returns unknown result " << status;
                break;
        }
    }
    else
    {
        JLOG(j_.trace()) << "fetchNodeObject " << hash
                         << ": record found in cache";
    }

    if (nodeObject)
        fetchReport.wasFound = true;

    return nodeObject;
}

std::vector<std::shared_ptr<NodeObject>>
DatabaseNodeImp::fetchBatch(std::vector<uint256> const& hashes)
{
    std::vector<std::shared_ptr<NodeObject>> results{hashes.size()};
    using namespace std::chrono;
    auto const before = steady_clock::now();
    std::unordered_map<uint256 const*, size_t> indexMap;
    std::vector<uint256 const*> cacheMisses;
    uint64_t hits = 0;
    uint64_t fetches = 0;
    for (size_t i = 0; i < hashes.size(); ++i)
    {
        auto const& hash = hashes[i];
        // See if the object already exists in the cache
        auto nObj = cache_ ? cache_->fetch(hash) : nullptr;
        ++fetches;
        if (!nObj)
        {
            // Try the database
            indexMap[&hash] = i;
            cacheMisses.push_back(&hash);
        }
        else
        {
            results[i] = nObj;
            // It was in the cache.
            ++hits;
        }
    }

    JLOG(j_.debug()) << "fetchBatch - cache hits = "
                     << (hashes.size() - cacheMisses.size())
                     << " - cache misses = " << cacheMisses.size();
    auto dbResults = backend_->fetchBatch(cacheMisses).first;

    for (size_t i = 0; i < dbResults.size(); ++i)
    {
        auto nObj = dbResults[i];
        size_t index = indexMap[cacheMisses[i]];
        results[index] = nObj;
        auto const& hash = hashes[index];

        if (nObj)
        {
            // Ensure all threads get the same object
            if (cache_)
                cache_->canonicalize_replace_client(hash, nObj);
        }
        else
        {
            JLOG(j_.error())
                << "fetchBatch - "
                << "record not found in db or cache. hash = " << strHex(hash);
        }
    }

    auto fetchDurationUs =
        std::chrono::duration_cast<std::chrono::microseconds>(
            steady_clock::now() - before)
            .count();
    updateFetchMetrics(fetches, hits, fetchDurationUs);
    return results;
}

}  // namespace NodeStore
}  // namespace ripple
