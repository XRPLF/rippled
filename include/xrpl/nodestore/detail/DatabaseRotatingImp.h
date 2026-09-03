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
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace xrpl::node_store {

class DatabaseRotatingImp : public DatabaseRotating
{
public:
    DatabaseRotatingImp() = delete;
    DatabaseRotatingImp(DatabaseRotatingImp const&) = delete;
    DatabaseRotatingImp&
    operator=(DatabaseRotatingImp const&) = delete;

    // Generations are ordered oldest -> newest; the last is the writable backend.
    DatabaseRotatingImp(
        Scheduler& scheduler,
        int readThreads,
        std::vector<std::shared_ptr<Backend>> generations,
        Section const& config,
        beast::Journal j);

    ~DatabaseRotatingImp() override
    {
        stop();
    }

    void
    advance(std::unique_ptr<Backend>&& newWritable, RingPersist const& persist) override;

    std::size_t
    generationCount() const override;

    std::uint64_t
    copyForwardCount() const override;

    void
    beginRetire() override;

    void
    endRetire() override;

    void
    retireOldest(RingPersist const& persist) override;

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

private:
    // Immutable snapshot of the generation ring, ordered oldest (front) -> newest (back);
    // back() is the writable backend. Replaced copy-on-write under mutex_ by advance() /
    // retireOldest(); fetchNodeObject takes a shared_ptr copy and iterates it lock-free,
    // so the hot read path never allocates and never blocks writers.
    std::shared_ptr<std::vector<std::shared_ptr<Backend>>> ring_;
    // The oldest generation while it is being retired (evacuated then dropped), else
    // null. A read served by this generation is copied forward into the writable
    // backend so its survivors are preserved before it is dropped; copy-forward is
    // scoped to this generation only (reads from other sealed generations are not
    // copied), which keeps evacuation O(churn) instead of O(total state).
    std::shared_ptr<Backend> retiring_;
    mutable std::mutex mutex_;

    // Tally of nodes copied forward out of the retiring generation, for the summary
    // line logged when the generation is dropped.
    std::atomic<std::uint64_t> copyForwardCount_{0};

    std::shared_ptr<NodeObject>
    fetchNodeObject(uint256 const& hash, std::uint32_t, FetchReport& fetchReport, bool duplicate)
        override;

    void
    forEach(std::function<void(std::shared_ptr<NodeObject>)> f) override;
};

}  // namespace xrpl::node_store
