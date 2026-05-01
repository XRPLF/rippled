#include <xrpl/nodestore/detail/DatabaseNodeImp.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/nodestore/Database.h>
#include <xrpl/nodestore/NodeObject.h>
#include <xrpl/nodestore/Scheduler.h>
#include <xrpl/nodestore/Types.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace xrpl::NodeStore {

void
DatabaseNodeImp::store(NodeObjectType type, Blob&& data, uint256 const& hash, std::uint32_t)
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

std::shared_ptr<NodeObject>
DatabaseNodeImp::fetchNodeObject(
    uint256 const& hash,
    std::uint32_t,
    FetchReport& fetchReport,
    bool duplicate)
{
    std::shared_ptr<NodeObject> nodeObject = nullptr;
    Status status = Status::ok;

    try
    {
        status = backend_->fetch(hash, &nodeObject);
    }
    catch (std::exception const& e)
    {
        JLOG(j_.fatal()) << "fetchNodeObject " << hash
                         << ": Exception fetching from backend: " << e.what();
        Rethrow();
    }

    switch (status)
    {
        case Status::ok:
        case Status::notFound:
            break;
        case Status::dataCorrupt:
            JLOG(j_.fatal()) << "fetchNodeObject " << hash << ": nodestore data is corrupted";
            break;
        default:
            JLOG(j_.warn()) << "fetchNodeObject " << hash << ": backend returns unknown result "
                            << static_cast<int>(status);
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

    // Get the node objects that match the hashes from the backend. To protect
    // against the backends returning fewer or more results than expected, the
    // container is resized to the number of hashes.
    auto results = backend_->fetchBatch(hashes).first;
    XRPL_ASSERT(
        results.size() == hashes.size() || results.empty(),
        "number of output objects either matches number of input hashes or is empty");
    results.resize(hashes.size());
    for (size_t i = 0; i < results.size(); ++i)
    {
        if (!results[i])
        {
            JLOG(j_.error()) << "fetchBatch - "
                             << "record not found in db. hash = " << strHex(hashes[i]);
        }
    }

    auto fetchDurationUs =
        std::chrono::duration_cast<std::chrono::microseconds>(steady_clock::now() - before).count();
    updateFetchMetrics(hashes.size(), 0, fetchDurationUs);
    return results;
}

}  // namespace xrpl::NodeStore
