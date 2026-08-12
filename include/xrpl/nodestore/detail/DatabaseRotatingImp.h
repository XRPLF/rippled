#pragma once

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/nodestore/Backend.h>
#include <xrpl/nodestore/Database.h>
#include <xrpl/nodestore/DatabaseRotating.h>
#include <xrpl/nodestore/NodeObject.h>
#include <xrpl/nodestore/Scheduler.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace xrpl::node_store {

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
        std::unique_ptr<node_store::Backend>&& newBackend,
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
    setRotationInFlight(bool inFlight) override;
    bool
    isRotationInFlight() const override;

    [[nodiscard]]
    std::uint64_t
    getAndResetDuplicationCount() override;

private:
    std::shared_ptr<Backend> writableBackend_;
    std::shared_ptr<Backend> archiveBackend_;
    mutable std::mutex mutex_;

    // Set to true during the entire SHAMapStore rotation process.
    // While true, archive hits on ordinary (duplicate == false)
    // fetches are copied forward into the writable backend.
    // copyForwardCount_ tallies them per rotation for the
    // summary line logged at swap.
    std::atomic<bool> rotationInFlight_{false};
    std::atomic<std::uint64_t> copyForwardCount_{0};
    // Duplication count tracks the number of nodes that are directly duplicated because they're in
    // the target ledger or rescued from a cache.
    std::atomic<std::uint64_t> duplicationCount_{0};

    std::shared_ptr<NodeObject>
    fetchNodeObject(uint256 const& hash, std::uint32_t, FetchReport& fetchReport, bool duplicate)
        override;

    void
    forEach(std::function<void(std::shared_ptr<NodeObject>)> f) override;
};

}  // namespace xrpl::node_store
