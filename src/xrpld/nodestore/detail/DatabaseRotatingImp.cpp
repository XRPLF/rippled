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

#include <xrpld/nodestore/detail/DatabaseRotatingImp.h>
#include <xrpl/protocol/HashPrefix.h>
#include <boost/thread/locks.hpp>

namespace ripple {
namespace NodeStore {

DatabaseRotatingImp::DatabaseRotatingImp(
    Scheduler& scheduler,
    int readThreads,
    std::shared_ptr<Backend> writableBackend,
    std::shared_ptr<Backend> archiveBackend,
    Section const& config,
    beast::Journal j)
    : DatabaseRotating(scheduler, readThreads, config, j)
    , writableBackend_(std::move(writableBackend))
    , archiveBackend_(std::move(archiveBackend))
    , unitTest_(get<bool>(config, unitTestFlag, false))
{
    if (writableBackend_)
        fdRequired_ += writableBackend_->fdRequired();
    if (archiveBackend_)
        fdRequired_ += archiveBackend_->fdRequired();
}

[[nodiscard]] bool
DatabaseRotatingImp::rotateWithLock(
    std::function<std::unique_ptr<NodeStore::Backend>(
        std::string const& writableBackendName)> const& f)
{
    // This function should be the only one taking any kind of unique/write
    // lock, and should only be called once at a time by its syncronous caller.

    boost::upgrade_lock readLock(mutex_, boost::defer_lock);
    if (!readLock.try_lock())
    {
        // If anything other than a unit test gets here, something has gone very
        // wrong
        XRPL_ASSERT(
            unitTest_,
            "ripple::NodeStore::DatabaseRotatingImp::rotateWithLock "
            "unit testing");
        return false;
    }
    auto newBackend = f(writableBackend_->getName());

    // boost::upgrade_mutex guarantees that only one thread can have "upgrade
    // ownership" at a time, so this is 100% safe, and guaranteed to avoid
    // deadlock.
    boost::upgrade_to_unique_lock writeLock(readLock);

    archiveBackend_->setDeletePath();
    archiveBackend_ = std::move(writableBackend_);
    writableBackend_ = std::move(newBackend);

    return true;
}

std::string
DatabaseRotatingImp::getName() const
{
    boost::shared_lock lock(mutex_);
    return writableBackend_->getName();
}

std::int32_t
DatabaseRotatingImp::getWriteLoad() const
{
    boost::shared_lock lock(mutex_);
    return writableBackend_->getWriteLoad();
}

void
DatabaseRotatingImp::importDatabase(Database& source)
{
    auto const backend = [&] {
        boost::shared_lock lock(mutex_);
        return writableBackend_;
    }();

    importInternal(*backend, source);
}

void
DatabaseRotatingImp::sync()
{
    boost::shared_lock lock(mutex_);
    writableBackend_->sync();
}

void
DatabaseRotatingImp::store(
    NodeObjectType type,
    Blob&& data,
    uint256 const& hash,
    std::uint32_t)
{
    auto nObj = NodeObject::createObject(type, std::move(data), hash);

    auto const backend = [&] {
        boost::shared_lock lock(mutex_);
        return writableBackend_;
    }();

    backend->store(nObj);
    storeStats(1, nObj->getData().size());
}

void
DatabaseRotatingImp::sweep()
{
    // nothing to do
}

std::shared_ptr<NodeObject>
DatabaseRotatingImp::fetchNodeObject(
    uint256 const& hash,
    std::uint32_t,
    FetchReport& fetchReport,
    bool duplicate)
{
    auto fetch = [&](std::shared_ptr<Backend> const& backend) {
        Status status;
        std::shared_ptr<NodeObject> nodeObject;
        try
        {
            status = backend->fetch(hash.data(), &nodeObject);
        }
        catch (std::exception const& e)
        {
            JLOG(j_.fatal()) << "Exception, " << e.what();
            Rethrow();
        }

        switch (status)
        {
            case ok:
            case notFound:
                break;
            case dataCorrupt:
                JLOG(j_.fatal()) << "Corrupt NodeObject #" << hash;
                break;
            default:
                JLOG(j_.warn()) << "Unknown status=" << status;
                break;
        }

        return nodeObject;
    };

    // See if the node object exists in the cache
    std::shared_ptr<NodeObject> nodeObject;

    auto [writable, archive] = [&] {
        boost::shared_lock lock(mutex_);
        return std::make_pair(writableBackend_, archiveBackend_);
    }();

    // Try to fetch from the writable backend
    nodeObject = fetch(writable);
    if (!nodeObject)
    {
        // Otherwise try to fetch from the archive backend
        nodeObject = fetch(archive);
        if (nodeObject)
        {
            {
                // Refresh the writable backend pointer
                boost::shared_lock lock(mutex_);
                writable = writableBackend_;
            }

            // Update writable backend with data from the archive backend
            if (duplicate)
                writable->store(nodeObject);
        }
    }

    if (nodeObject)
        fetchReport.wasFound = true;

    return nodeObject;
}

void
DatabaseRotatingImp::for_each(
    std::function<void(std::shared_ptr<NodeObject>)> f)
{
    auto [writable, archive] = [&] {
        boost::shared_lock lock(mutex_);
        return std::make_pair(writableBackend_, archiveBackend_);
    }();

    // Iterate the writable backend
    writable->for_each(f);

    // Iterate the archive backend
    archive->for_each(f);
}

}  // namespace NodeStore
}  // namespace ripple
