#pragma once

#include <xrpl/basics/TaggedCache.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/nodestore/Database.h>

namespace xrpl {
namespace NodeStore {

class DatabaseNodeImp : public Database
{
public:
    DatabaseNodeImp() = delete;
    DatabaseNodeImp(DatabaseNodeImp const&) = delete;
    DatabaseNodeImp&
    operator=(DatabaseNodeImp const&) = delete;

    DatabaseNodeImp(
        Scheduler& scheduler,
        int readThreads,
        std::shared_ptr<Backend> backend,
        Section const& config,
        beast::Journal j)
        : Database(scheduler, readThreads, config, j), backend_(std::move(backend))
    {
        XRPL_ASSERT(
            backend_,
            "xrpl::NodeStore::DatabaseNodeImp::DatabaseNodeImp : non-null "
            "backend");
    }

    ~DatabaseNodeImp()
    {
        stop();
    }

    std::string
    getName() const override
    {
        return backend_->getName();
    }

    std::int32_t
    getWriteLoad() const override
    {
        return backend_->getWriteLoad();
    }

    void
    importDatabase(Database& source) override
    {
        importInternal(*backend_.get(), source);
    }

    void
    store(NodeObjectType type, Blob&& data, uint256 const& hash, std::uint32_t) override;

    bool
    isSameDB(std::uint32_t, std::uint32_t) override
    {
        // only one database
        return true;
    }

    void
    sync() override
    {
        backend_->sync();
    }

    std::vector<std::shared_ptr<NodeObject>>
    fetchBatch(std::vector<uint256> const& hashes);

    void
    asyncFetch(
        uint256 const& hash,
        std::uint32_t ledgerSeq,
        std::function<void(std::shared_ptr<NodeObject> const&)>&& callback) override;

private:
    // Persistent key/value storage
    std::shared_ptr<Backend> backend_;

    std::shared_ptr<NodeObject>
    fetchNodeObject(uint256 const& hash, std::uint32_t, FetchReport& fetchReport, bool duplicate) override;

    void
    for_each(std::function<void(std::shared_ptr<NodeObject>)> f) override
    {
        backend_->for_each(f);
    }
};

}  // namespace NodeStore
}  // namespace xrpl
