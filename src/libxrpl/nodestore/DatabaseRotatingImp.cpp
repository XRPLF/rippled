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
#include <xrpl/protocol/Protocol.h>

#include <atomic>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>

namespace xrpl::node_store {

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
    std::unique_ptr<node_store::Backend>&& newBackend,
    std::function<void(std::string const& writableName, std::string const& archiveName)> const& f)
{
    // Pass these two names to the callback function
    std::string const newWritableBackendName = newBackend->getName();
    std::string newArchiveBackendName;
    // Hold on to current archive backend pointer until after the
    // callback finishes. Only then will the archive directory be
    // deleted.
    std::shared_ptr<node_store::Backend> oldArchiveBackend;
    std::uint64_t copyForwards = 0;
    std::uint64_t copyRejects = 0;
    std::uint64_t duplications = 0;
    {
        std::scoped_lock const lock(mutex_);

        archiveBackend_->setDeletePath();
        oldArchiveBackend = std::move(archiveBackend_);

        archiveBackend_ = std::move(writableBackend_);
        newArchiveBackendName = archiveBackend_->getName();

        writableBackend_ = std::move(newBackend);

        copyForwards = copyForwardCount_.exchange(0, std::memory_order_relaxed);
        copyRejects = copyRejectCount_.exchange(0, std::memory_order_relaxed);
        duplications = duplicationCount_.exchange(0, std::memory_order_relaxed);
    }

    if (copyForwards > 0 || copyRejects > 0)
    {
        JLOG(j_.warn()) << "Rotating: copied forward " << copyForwards
                        << " archive-served reads into the writable backend "
                           "during the rotation window. Rejected "
                        << copyRejects;
    }
    if (duplications > 0)
    {
        JLOG(j_.warn()) << "Rotating: duplicated " << duplications
                        << " nodes into the writable backend for the current validated ledger.";
    }

    f(newWritableBackendName, newArchiveBackendName);
}

void
DatabaseRotatingImp::setRotationInFlight(LedgerIndex inFlight)
{
    rotationInFlight_.store(inFlight, std::memory_order_release);
    std::ostringstream msg;
    msg << "Rotating: copy-forward on archive reads ";
    if (inFlight == 0)
    {
        msg << "disabled";
    }
    else
    {
        msg << "from " << inFlight << " forward";
    }
    JLOG(j_.debug()) << msg.str();
}

LedgerIndex
DatabaseRotatingImp::getRotationInFlight() const
{
    return rotationInFlight_.load(std::memory_order_acquire);
}

std::uint64_t
DatabaseRotatingImp::getDuplicationCount() const
{
    return duplicationCount_.load(std::memory_order_acquire);
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
    std::uint32_t ledgerSeq,
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
            // Update writable backend with data from the archive backend.
            // While a rotation is in flight, ordinary (duplicate == false)
            // reads served by the archive are copied forward too: the
            // archive is about to be deleted, and a body canonicalized
            // into the cache after the freshen getKeys() snapshot would
            // otherwise survive only in RAM once the archive is dropped.
            auto const inFlight = getRotationInFlight();
            if (duplicate || (inFlight != 0 && (ledgerSeq == 0 || ledgerSeq >= inFlight)))
            {
                {
                    // Refresh the writable backend pointer
                    std::scoped_lock const lock(mutex_);
                    writable = writableBackend_;
                }

                if (duplicate)
                {
                    duplicationCount_.fetch_add(1, std::memory_order_relaxed);
                }
                else
                {
                    JLOG(j_.debug()) << "Rotating: copy node for ledger " << ledgerSeq
                                     << " from archive to writable backend: " << hash;
                    copyForwardCount_.fetch_add(1, std::memory_order_relaxed);
                }
                writable->store(nodeObject);
            }
            else if (inFlight != 0)
            {
                JLOG(j_.debug()) << "Rotating: DO NOT copy node for ledger " << ledgerSeq
                                 << " from archive to writable backend: " << hash;
                copyRejectCount_.fetch_add(1, std::memory_order_relaxed);
            }
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

}  // namespace xrpl::node_store
