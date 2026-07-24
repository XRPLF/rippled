#pragma once

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/nodestore/Backend.h>
#include <xrpl/nodestore/Database.h>
#include <xrpl/nodestore/DatabaseRotating.h>
#include <xrpl/nodestore/NodeObject.h>
#include <xrpl/nodestore/Scheduler.h>
#include <xrpl/protocol/Protocol.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace xrpl::NodeStore {

class DatabaseRotatingImp : public DatabaseRotating
{
public:
    DatabaseRotatingImp() = delete;
    DatabaseRotatingImp(DatabaseRotatingImp const&) = delete;
    DatabaseRotatingImp&
    operator=(DatabaseRotatingImp const&) = delete;

    DatabaseRotatingImp(
        Scheduler& scheduler,
        int readThreads,
        std::shared_ptr<Backend> writableBackend,
        std::shared_ptr<Backend> archiveBackend,
        Section const& config,
        beast::Journal j);

    ~DatabaseRotatingImp() override
    {
        stop();
    }

    void
    rotate(
        std::unique_ptr<NodeStore::Backend>&& newBackend,
        std::function<void(std::string const& writableName, std::string const& archiveName)> const&
            f) override;

    std::string
    getName() const override;

    std::int32_t
    getWriteLoad() const override;

    void
    importDatabase(Database& source) override;

    bool
    isSameDB(std::uint32_t, std::uint32_t) override
    {
        // rotating store acts as one logical database
        return true;
    }

    void
    store(NodeObjectType type, Blob&& data, uint256 const& hash, std::uint32_t) override;

    void
    sync() override;

    void
    sweep() override;

    void
    setRotationInFlight(LedgerIndex inFlight) override;
    LedgerIndex
    getRotationInFlight() const override;

    std::uint64_t
    getDuplicationCount() const override;

private:
    std::shared_ptr<Backend> writableBackend_;
    std::shared_ptr<Backend> archiveBackend_;
    mutable std::mutex mutex_;

    // Set to the index of the last rotated ledger between SHAMapStore
    // starting the cache-freshen phase and the completion of rotate().
    // While non-zero, archive hits on ordinary (duplicate == false)
    // fetches are copied forward into the writable backend if they are
    // for that ledger or later, since those are the ones we'll keep.
    // To be safe, copy forward if the provided ledger index is 0.
    // copyForwardCount_ tallies them per rotation for the
    // summary line logged at swap.
    // copyRejectCount_ tallies the ones that weren't copied.
    std::atomic<LedgerIndex> rotationInFlight_{0};
    std::atomic<std::uint64_t> copyForwardCount_{0};
    std::atomic<std::uint64_t> copyRejectCount_{0};
    // Duplication count tracks the number of nodes that are directly duplicated because they're in
    // the target ledger or cache.
    std::atomic<std::uint64_t> duplicationCount_{0};

    std::shared_ptr<NodeObject>
    fetchNodeObject(uint256 const& hash, std::uint32_t, FetchReport& fetchReport, bool duplicate)
        override;

    void
    forEach(std::function<void(std::shared_ptr<NodeObject>)> f) override;
};

}  // namespace xrpl::NodeStore
