#pragma once

#include <xrpl/basics/TaggedCache.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/config/BasicConfig.h>
#include <xrpl/config/Constants.h>
#include <xrpl/nodestore/Database.h>

namespace xrpl::NodeStore {

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
        std::optional<int> cacheSize, cacheAge;

        if (config.exists(Keys::kCacheSize))
        {
            cacheSize = get<int>(config, Keys::kCacheSize);
            if (cacheSize.value() < 0)
                Throw<std::runtime_error>("Specified negative value for cache_size");
        }

        if (config.exists(Keys::kCacheAge))
        {
            cacheAge = get<int>(config, Keys::kCacheAge);
            if (cacheAge.value() < 0)
                Throw<std::runtime_error>("Specified negative value for cache_age");
        }

        if (cacheSize.has_value() || cacheAge.has_value())
        {
            cache_ = std::make_shared<TaggedCache<uint256, NodeObject>>(
                "DatabaseNodeImp",
                cacheSize.value_or(0),
                std::chrono::minutes(cacheAge.value_or(0)),
                stopwatch(),
                j);
        }

        XRPL_ASSERT(
            backend_,
            "xrpl::NodeStore::DatabaseNodeImp::DatabaseNodeImp : non-null "
            "backend");
    }

    ~DatabaseNodeImp() override
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

    void
    asyncFetch(
        uint256 const& hash,
        std::uint32_t ledgerSeq,
        std::function<void(std::shared_ptr<NodeObject> const&)>&& callback) override;

    void
    sweep() override;

private:
    // Cache for database objects. This cache is not always initialized. Check
    // for null before using.
    std::shared_ptr<TaggedCache<uint256, NodeObject>> cache_;
    // Persistent key/value storage
    std::shared_ptr<Backend> backend_;

    std::shared_ptr<NodeObject>
    fetchNodeObject(uint256 const& hash, std::uint32_t, FetchReport& fetchReport, bool duplicate)
        override;

    void
    forEach(std::function<void(std::shared_ptr<NodeObject>)> f) override
    {
        backend_->forEach(f);
    }
};

}  // namespace xrpl::NodeStore
