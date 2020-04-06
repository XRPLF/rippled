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
    auto nObj = NodeObject::createObject(type, std::move(data), hash);
    pCache_->canonicalize_replace_cache(hash, nObj);
    backend_->store(nObj);
    nCache_->erase(hash);
    storeStats(1, nObj->getData().size());
}

bool
DatabaseNodeImp::asyncFetch(
    uint256 const& hash,
    std::uint32_t ledgerSeq,
    std::shared_ptr<NodeObject>& nodeObject)
{
    // See if the object is in cache
    nodeObject = pCache_->fetch(hash);
    if (nodeObject || nCache_->touch_if_exists(hash))
        return true;

    // Otherwise post a read
    Database::asyncFetch(hash, ledgerSeq);
    return false;
}

void
DatabaseNodeImp::tune(int size, std::chrono::seconds age)
{
    pCache_->setTargetSize(size);
    pCache_->setTargetAge(age);
    nCache_->setTargetSize(size);
    nCache_->setTargetAge(age);
}

void
DatabaseNodeImp::sweep()
{
    pCache_->sweep();
    nCache_->sweep();
}

std::shared_ptr<NodeObject>
DatabaseNodeImp::fetchNodeObject(
    uint256 const& hash,
    std::uint32_t,
    FetchReport& fetchReport)
{
    // See if the node object exists in the cache
    auto nodeObject{pCache_->fetch(hash)};
    if (!nodeObject && !nCache_->touch_if_exists(hash))
    {
        // Try the backend
        fetchReport.wentToDisk = true;

        Status status;
        try
        {
            status = backend_->fetch(hash.data(), &nodeObject);
        }
        catch (std::exception const& e)
        {
            JLOG(j_.fatal()) << "Exception, " << e.what();
            Rethrow();
        }

        switch (status)
        {
            case ok:
                ++fetchHitCount_;
                if (nodeObject)
                    fetchSz_ += nodeObject->getData().size();
                break;
            case notFound:
                break;
            case dataCorrupt:
                JLOG(j_.fatal()) << "Corrupt NodeObject #" << hash;
                break;
            default:
                JLOG(j_.warn()) << "Unknown status=" << status;
                break;
        }

        if (!nodeObject)
        {
            // Just in case a write occurred
            nodeObject = pCache_->fetch(hash);
            if (!nodeObject)
                // We give up
                nCache_->insert(hash);
        }
        else
        {
            fetchReport.wasFound = true;

            // Ensure all threads get the same object
            pCache_->canonicalize_replace_client(hash, nodeObject);

            // Since this was a 'hard' fetch, we will log it
            JLOG(j_.trace()) << "HOS: " << hash << " fetch: in shard db";
        }
    }

    return nodeObject;
}

}  // namespace NodeStore
}  // namespace ripple
