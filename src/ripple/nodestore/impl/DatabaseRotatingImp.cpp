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
#include <ripple/nodestore/impl/DatabaseRotatingImp.h>
#include <ripple/protocol/HashPrefix.h>

namespace ripple {
namespace NodeStore {

DatabaseRotatingImp::DatabaseRotatingImp(
    std::string const& name,
    Scheduler& scheduler,
    int readThreads,
    Stoppable& parent,
    std::shared_ptr<Backend> writableBackend,
    std::shared_ptr<Backend> archiveBackend,
    Section const& config,
    beast::Journal j)
    : DatabaseRotating(name, parent, scheduler, readThreads, config, j)
    , pCache_(std::make_shared<TaggedCache<uint256, NodeObject>>(
          name,
          cacheTargetSize,
          cacheTargetAge,
          stopwatch(),
          j))
    , nCache_(std::make_shared<KeyCache<uint256>>(
          name,
          stopwatch(),
          cacheTargetSize,
          cacheTargetAge))
    , writableBackend_(std::move(writableBackend))
    , archiveBackend_(std::move(archiveBackend))
{
    if (writableBackend_)
        fdRequired_ += writableBackend_->fdRequired();
    if (archiveBackend_)
        fdRequired_ += archiveBackend_->fdRequired();
    setParent(parent);
}

void
DatabaseRotatingImp::rotateWithLock(
    std::function<std::unique_ptr<NodeStore::Backend>(
        std::string const& writableBackendName)> const& f)
{
    std::lock_guard lock(mutex_);

    auto newBackend = f(writableBackend_->getName());
    archiveBackend_->setDeletePath();
    archiveBackend_ = std::move(writableBackend_);
    writableBackend_ = std::move(newBackend);
}

std::string
DatabaseRotatingImp::getName() const
{
    std::lock_guard lock(mutex_);
    return writableBackend_->getName();
}

std::int32_t
DatabaseRotatingImp::getWriteLoad() const
{
    std::lock_guard lock(mutex_);
    return writableBackend_->getWriteLoad();
}

void
DatabaseRotatingImp::import(Database& source)
{
    auto const backend = [&] {
        std::lock_guard lock(mutex_);
        return writableBackend_;
    }();

    importInternal(*backend, source);
}

bool
DatabaseRotatingImp::storeLedger(std::shared_ptr<Ledger const> const& srcLedger)
{
    auto const backend = [&] {
        std::lock_guard lock(mutex_);
        return writableBackend_;
    }();

    return Database::storeLedger(
        *srcLedger, backend, pCache_, nCache_, nullptr);
}

void
DatabaseRotatingImp::store(
    NodeObjectType type,
    Blob&& data,
    uint256 const& hash,
    std::uint32_t seq)
{
    auto nObj = NodeObject::createObject(type, std::move(data), hash);
    pCache_->canonicalize_replace_cache(hash, nObj);

    auto const backend = [&] {
        std::lock_guard lock(mutex_);
        return writableBackend_;
    }();
    backend->store(nObj);

    nCache_->erase(hash);
    storeStats(nObj->getData().size());
}

bool
DatabaseRotatingImp::asyncFetch(
    uint256 const& hash,
    std::uint32_t seq,
    std::shared_ptr<NodeObject>& object)
{
    // See if the object is in cache
    object = pCache_->fetch(hash);
    if (object || nCache_->touch_if_exists(hash))
        return true;

    // Otherwise post a read
    Database::asyncFetch(hash, seq, pCache_, nCache_);
    return false;
}

void
DatabaseRotatingImp::tune(int size, std::chrono::seconds age)
{
    pCache_->setTargetSize(size);
    pCache_->setTargetAge(age);
    nCache_->setTargetSize(size);
    nCache_->setTargetAge(age);
}

void
DatabaseRotatingImp::sweep()
{
    pCache_->sweep();
    nCache_->sweep();
}

std::shared_ptr<NodeObject>
DatabaseRotatingImp::fetchFrom(uint256 const& hash, std::uint32_t seq)
{
    auto [writable, archive] = [&] {
        std::lock_guard lock(mutex_);
        return std::make_pair(writableBackend_, archiveBackend_);
    }();

    // Try to fetch from the writable backend
    auto nObj = fetchInternal(hash, writable);
    if (!nObj)
    {
        // Otherwise try to fetch from the archive backend
        nObj = fetchInternal(hash, archive);
        if (nObj)
        {
            {
                // Refresh the writable backend pointer
                std::lock_guard lock(mutex_);
                writable = writableBackend_;
            }

            // Update writable backend with data from the archive backend
            writable->store(nObj);
            nCache_->erase(hash);
        }
    }
    return nObj;
}

void
DatabaseRotatingImp::for_each(
    std::function<void(std::shared_ptr<NodeObject>)> f)
{
    auto [writable, archive] = [&] {
        std::lock_guard lock(mutex_);
        return std::make_pair(writableBackend_, archiveBackend_);
    }();

    // Iterate the writable backend
    writable->for_each(f);

    // Iterate the archive backend
    archive->for_each(f);
}

}  // namespace NodeStore
}  // namespace ripple
