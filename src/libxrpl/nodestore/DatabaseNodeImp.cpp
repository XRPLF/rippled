/** @file
 *  Single-backend `Database` implementation for the XRPL NodeStore.
 *
 *  `DatabaseNodeImp` wraps one pluggable `Backend` (NuDB, RocksDB, etc.) and
 *  satisfies the `Database` contract for store, synchronous fetch, batch fetch,
 *  and async fetch. It is the primary node database path; the two-backend
 *  rotation variant is `DatabaseRotatingImp`.
 *
 *  Ledger-sequence parameters are accepted by the interface but intentionally
 *  ignored here — a single backend stores all objects regardless of which
 *  ledger they belong to.
 */

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

/** Serialize and persist a node object to the backend.
 *
 *  Records the byte count in store statistics before moving `data` into the
 *  `NodeObject`, because the move invalidates the size. The ledger sequence
 *  parameter is part of the `Database` interface contract but is unused here —
 *  the single backend accepts objects from any ledger.
 *
 *  @param type The object type tag stored alongside the payload.
 *  @param data Payload blob; ownership is transferred — the caller's variable
 *      is left in a valid but unspecified state after this call.
 *  @param hash 256-bit content-address key for the object.
 */
void
DatabaseNodeImp::store(NodeObjectType type, Blob&& data, uint256 const& hash, std::uint32_t)
{
    storeStats(1, data.size());

    auto obj = NodeObject::createObject(type, std::move(data), hash);
    backend_->store(obj);
}

/** Enqueue a non-blocking fetch request on the base-class read thread pool.
 *
 *  Delegates unconditionally to `Database::asyncFetch()`, which coalesces
 *  duplicate hash requests and dispatches the callback from a worker thread.
 *  There are no per-backend scheduling adjustments for the single-backend case.
 *
 *  @param hash Key of the object to retrieve.
 *  @param ledgerSeq Sequence of the ledger the object belongs to; forwarded
 *      to the base class for hash-coalescing decisions (`isSameDB`).
 *  @param callback Invoked with the fetched `NodeObject` (or `nullptr` if not
 *      found) once the backend read completes. Called from a worker thread.
 */
void
DatabaseNodeImp::asyncFetch(
    uint256 const& hash,
    std::uint32_t ledgerSeq,
    std::function<void(std::shared_ptr<NodeObject> const&)>&& callback)
{
    Database::asyncFetch(hash, ledgerSeq, std::move(callback));
}

/** Private virtual hook called by the base-class thread pool to perform a
 *  single synchronous backend lookup.
 *
 *  Wraps `backend_->fetch()` with structured error logging:
 *  - `Status::Ok` and `Status::NotFound` are silent — a missing object is
 *    normal during ledger history traversal.
 *  - `Status::DataCorrupt` emits a `fatal` log entry; on-disk corruption is a
 *    ledger integrity failure requiring operator intervention.
 *  - Any other status code emits a `warn` log but does not abort.
 *  - A thrown `std::exception` is logged at `fatal` level and then re-raised
 *    via `Rethrow()` so the crash propagates rather than being swallowed.
 *
 *  Sets `fetchReport.wasFound = true` only when `backend_->fetch()` populates
 *  the output pointer, which feeds the base-class hit-count metric.
 *
 *  @param hash 256-bit key of the object to retrieve.
 *  @param fetchReport In/out report updated with `wasFound` on a cache hit;
 *      timing is applied by the calling base-class wrapper.
 *  @param duplicate Whether this request was deduplicated from another in-flight
 *      fetch for the same hash; passed through from `Database::asyncFetch()`.
 *  @return The fetched `NodeObject`, or `nullptr` if not found or on error.
 *  @throws Any exception re-raised from `backend_->fetch()` after logging.
 */
std::shared_ptr<NodeObject>
DatabaseNodeImp::fetchNodeObject(
    uint256 const& hash,
    std::uint32_t,
    FetchReport& fetchReport,
    bool duplicate)
{
    std::shared_ptr<NodeObject> nodeObject = nullptr;
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

    if (nodeObject)
        fetchReport.wasFound = true;

    return nodeObject;
}

/** Synchronously fetch a batch of node objects from the backend, bypassing the
 *  async read queue.
 *
 *  Calls `backend_->fetchBatch()` directly and enforces the positional contract
 *  that `results[i]` corresponds to `hashes[i]`:
 *
 *  - Asserts the backend returned either exactly `hashes.size()` results or an
 *    empty vector (both are valid; partial batches are a backend bug).
 *  - Resizes an empty result vector to `hashes.size()` null pointers, so
 *    callers always receive a fully-sized vector regardless of which sentinel
 *    the backend chose.
 *  - Logs each null slot at `error` level to make missing history visible in
 *    diagnostics without throwing.
 *
 *  Wall-clock duration is recorded and forwarded to `updateFetchMetrics()` for
 *  operator dashboards. Hit count is not updated here — batch fetches are
 *  expected to return a mix of hits and misses, and per-slot tracking is the
 *  caller's responsibility.
 *
 *  @note The batch-level `Status` returned by `backend_->fetchBatch()` is
 *      discarded; only the object vector is used. Per-object availability is
 *      inferred from null slots.
 *
 *  @param hashes Ordered list of 256-bit keys to retrieve.
 *  @return Vector of the same length as `hashes`; null entries indicate objects
 *      not found in the backend.
 */
std::vector<std::shared_ptr<NodeObject>>
DatabaseNodeImp::fetchBatch(std::vector<uint256> const& hashes)
{
    using namespace std::chrono;
    auto const before = steady_clock::now();

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
