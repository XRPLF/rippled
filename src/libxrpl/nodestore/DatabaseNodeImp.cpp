#include <xrpl/nodestore/detail/DatabaseNodeImp.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/nodestore/Database.h>
#include <xrpl/nodestore/NodeObject.h>
#include <xrpl/nodestore/Scheduler.h>
#include <xrpl/nodestore/Types.h>

#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <utility>

namespace xrpl::NodeStore {

void
DatabaseNodeImp::store(NodeObjectType type, Blob&& data, uint256 const& hash, std::uint32_t)
{
    storeStats(1, data.size());

    auto obj = NodeObject::createObject(type, std::move(data), hash);
    backend_->store(obj);
    if (cache_)
    {
        // After the store, replace a negative cache entry if there is one
        cache_->canonicalize(hash, obj, [](std::shared_ptr<NodeObject> const& n) {
            return n->getType() == NodeObjectType::Dummy;
        });
    }
}

void
DatabaseNodeImp::asyncFetch(
    uint256 const& hash,
    std::uint32_t ledgerSeq,
    std::function<void(std::shared_ptr<NodeObject> const&)>&& callback)
{
    if (cache_)
    {
        std::shared_ptr<NodeObject> const obj = cache_->fetch(hash);
        if (obj)
        {
            callback(obj->getType() == NodeObjectType::Dummy ? nullptr : obj);
            return;
        }
    }
    Database::asyncFetch(hash, ledgerSeq, std::move(callback));
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
    std::shared_ptr<NodeObject> nodeObject = cache_ ? cache_->fetch(hash) : nullptr;
    if (!nodeObject)
    {
        JLOG(j_.trace()) << "fetchNodeObject " << hash << ": record not "
                         << (cache_ ? "cached" : "found");

        Status status = Status::Ok;
        try
        {
            status = backend_->fetch(hash, &nodeObject);
        }
        catch (std::exception const& e)
        {
            JLOG(j_.fatal()) << "fetchNodeObject " << hash
                             << ": Exception fetching from backend: " << e.what();
            rethrow();
        }

        switch (status)
        {
            case Status::Ok:
                if (cache_)
                {
                    if (nodeObject)
                    {
                        cache_->canonicalizeReplaceClient(hash, nodeObject);
                    }
                    else
                    {
                        auto notFound = NodeObject::createObject(NodeObjectType::Dummy, {}, hash);
                        cache_->canonicalizeReplaceClient(hash, notFound);
                        if (notFound->getType() != NodeObjectType::Dummy)
                            nodeObject = notFound;
                    }
                }
                break;
            case Status::NotFound:
                break;
            case Status::DataCorrupt:
                JLOG(j_.fatal()) << "fetchNodeObject " << hash << ": nodestore data is corrupted";
                break;
            default:
                JLOG(j_.warn()) << "fetchNodeObject " << hash << ": backend returns unknown result "
                                << static_cast<int>(status);
                break;
        }
    }
    else
    {
        JLOG(j_.trace()) << "fetchNodeObject " << hash << ": record found in cache";
        if (nodeObject->getType() == NodeObjectType::Dummy)
            nodeObject.reset();
    }

    if (nodeObject)
        fetchReport.wasFound = true;

    return nodeObject;
}

}  // namespace xrpl::NodeStore
