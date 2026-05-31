#include <xrpl/nodestore/detail/DatabaseRotatingImp.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/config/BasicConfig.h>
#include <xrpl/nodestore/Backend.h>
#include <xrpl/nodestore/Database.h>
#include <xrpl/nodestore/DatabaseRotating.h>
#include <xrpl/nodestore/NodeObject.h>
#include <xrpl/nodestore/Scheduler.h>
#include <xrpl/nodestore/Types.h>

#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

namespace xrpl::NodeStore {

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
{
    if (writableBackend_)
        fdRequired_ += writableBackend_->fdRequired();
    if (archiveBackend_)
        fdRequired_ += archiveBackend_->fdRequired();
}

void
DatabaseRotatingImp::rotate(
    std::unique_ptr<NodeStore::Backend>&& newBackend,
    std::function<void(std::string const& writableName, std::string const& archiveName)> const& f)
{
    // Pass these two names to the callback function
    std::string const newWritableBackendName = newBackend->getName();
    std::string newArchiveBackendName;
    // Hold on to current archive backend pointer until after the
    // callback finishes. Only then will the archive directory be
    // deleted.
    std::shared_ptr<NodeStore::Backend> oldArchiveBackend;
    {
        std::scoped_lock const lock(mutex_);

        archiveBackend_->setDeletePath();
        oldArchiveBackend = std::move(archiveBackend_);

        archiveBackend_ = std::move(writableBackend_);
        newArchiveBackendName = archiveBackend_->getName();

        writableBackend_ = std::move(newBackend);
    }

    f(newWritableBackendName, newArchiveBackendName);
}

std::string
DatabaseRotatingImp::getName() const
{
    std::scoped_lock const lock(mutex_);
    return writableBackend_->getName();
}

std::int32_t
DatabaseRotatingImp::getWriteLoad() const
{
    std::scoped_lock const lock(mutex_);
    return writableBackend_->getWriteLoad();
}

void
DatabaseRotatingImp::importDatabase(Database& source)
{
    auto const backend = [&] {
        std::scoped_lock const lock(mutex_);
        return writableBackend_;
    }();

    importInternal(*backend, source);
}

void
DatabaseRotatingImp::sync()
{
    std::scoped_lock const lock(mutex_);
    writableBackend_->sync();
}

void
DatabaseRotatingImp::store(NodeObjectType type, Blob&& data, uint256 const& hash, std::uint32_t)
{
    auto nObj = NodeObject::createObject(type, std::move(data), hash);

    auto const backend = [&] {
        std::scoped_lock const lock(mutex_);
        return writableBackend_;
    }();

    backend->store(nObj);
    storeStats(1, nObj->getData().size());
}

void
DatabaseRotatingImp::sweep()
{
    // Nothing to do.
}

std::shared_ptr<NodeObject>
DatabaseRotatingImp::fetchNodeObject(
    uint256 const& hash,
    std::uint32_t,
    FetchReport& fetchReport,
    bool duplicate)
{
    auto fetch = [&](std::shared_ptr<Backend> const& backend) {
        Status status = Status::Ok;
        std::shared_ptr<NodeObject> nodeObject;
        try
        {
            status = backend->fetch(hash, &nodeObject);
        }
        catch (std::exception const& e)
        {
            JLOG(j_.fatal()) << "Exception, " << e.what();
            rethrow();
        }

        switch (status)
        {
            case Status::Ok:
            case Status::NotFound:
                break;
            case Status::DataCorrupt:
                JLOG(j_.fatal()) << "Corrupt NodeObject #" << hash;
                break;
            default:
                JLOG(j_.warn()) << "Unknown status=" << static_cast<int>(status);
                break;
        }

        return nodeObject;
    };

    // See if the node object exists in the cache
    std::shared_ptr<NodeObject> nodeObject;

    auto [writable, archive] = [&] {
        std::scoped_lock const lock(mutex_);
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
                std::scoped_lock const lock(mutex_);
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
DatabaseRotatingImp::forEach(std::function<void(std::shared_ptr<NodeObject>)> f)
{
    auto [writable, archive] = [&] {
        std::scoped_lock const lock(mutex_);
        return std::make_pair(writableBackend_, archiveBackend_);
    }();

    // Iterate the writable backend
    writable->forEach(f);

    // Iterate the archive backend
    archive->forEach(f);
}

}  // namespace xrpl::NodeStore
