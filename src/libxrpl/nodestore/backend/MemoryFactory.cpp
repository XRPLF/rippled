#include <xrpl/basics/BasicConfig.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/nodestore/Backend.h>
#include <xrpl/nodestore/Factory.h>
#include <xrpl/nodestore/Manager.h>
#include <xrpl/nodestore/NodeObject.h>
#include <xrpl/nodestore/Scheduler.h>
#include <xrpl/nodestore/Types.h>

#include <boost/beast/core/string.hpp>
#include <boost/core/ignore_unused.hpp>

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace xrpl::NodeStore {

struct MemoryDB
{
    explicit MemoryDB() = default;

    std::mutex mutex;
    bool open = false;
    std::map<uint256 const, std::shared_ptr<NodeObject>> table;
};

class MemoryFactory : public Factory
{
private:
    std::mutex mutex_;
    std::map<std::string, MemoryDB, boost::beast::iless> map_;
    Manager& manager_;

public:
    explicit MemoryFactory(Manager& manager);

    [[nodiscard]] std::string
    getName() const override;

    std::unique_ptr<Backend>
    createInstance(
        size_t keyBytes,
        Section const& keyValues,
        std::size_t burstSize,
        Scheduler& scheduler,
        beast::Journal journal) override;

    MemoryDB&
    open(std::string const& path)
    {
        std::scoped_lock const _(mutex_);
        auto const result =
            map_.emplace(std::piecewise_construct, std::make_tuple(path), std::make_tuple());
        MemoryDB& db = result.first->second;
        if (db.open)
            Throw<std::runtime_error>("already open");
        return db;
    }
};

MemoryFactory* gMemoryFactory = nullptr;

void
registerMemoryFactory(Manager& manager)
{
    static MemoryFactory kInstance{manager};
    gMemoryFactory = &kInstance;
}

//------------------------------------------------------------------------------

class MemoryBackend : public Backend
{
private:
    using Map = std::map<uint256 const, std::shared_ptr<NodeObject>>;

    std::string name_;
    beast::Journal const journal_;
    MemoryDB* db_{nullptr};

public:
    MemoryBackend(size_t keyBytes, Section const& keyValues, beast::Journal journal)
        : name_(get(keyValues, "path")), journal_(journal)
    {
        boost::ignore_unused(journal_);  // Keep unused journal_ just in case.
        if (name_.empty())
            Throw<std::runtime_error>("Missing path in Memory backend");
    }

    ~MemoryBackend() override
    {
        close();
    }

    std::string
    getName() override
    {
        return name_;
    }

    void
    open(bool) override
    {
        db_ = &gMemoryFactory->open(name_);
    }

    bool
    isOpen() override
    {
        return static_cast<bool>(db_);
    }

    void
    close() override
    {
        db_ = nullptr;
    }

    //--------------------------------------------------------------------------

    Status
    fetch(uint256 const& hash, std::shared_ptr<NodeObject>* pObject) override
    {
        XRPL_ASSERT(db_, "xrpl::NodeStore::MemoryBackend::fetch : non-null database");

        std::scoped_lock const _(db_->mutex);

        Map::iterator const iter = db_->table.find(hash);
        if (iter == db_->table.end())
        {
            pObject->reset();
            return Status::NotFound;
        }
        *pObject = iter->second;
        return Status::Ok;
    }

    std::pair<std::vector<std::shared_ptr<NodeObject>>, Status>
    fetchBatch(std::vector<uint256> const& hashes) override
    {
        std::vector<std::shared_ptr<NodeObject>> results;
        results.reserve(hashes.size());
        for (auto const& h : hashes)
        {
            std::shared_ptr<NodeObject> nObj;
            Status const status = fetch(h, &nObj);
            if (status != Status::Ok)
            {
                results.push_back({});
            }
            else
            {
                results.push_back(nObj);
            }
        }

        return {results, Status::Ok};
    }

    void
    store(std::shared_ptr<NodeObject> const& object) override
    {
        XRPL_ASSERT(db_, "xrpl::NodeStore::MemoryBackend::store : non-null database");
        std::scoped_lock const _(db_->mutex);
        db_->table.emplace(object->getHash(), object);
    }

    void
    storeBatch(Batch const& batch) override
    {
        for (auto const& e : batch)
            store(e);
    }

    void
    sync() override
    {
    }

    void
    forEach(std::function<void(std::shared_ptr<NodeObject>)> f) override
    {
        XRPL_ASSERT(db_, "xrpl::NodeStore::MemoryBackend::forEach : non-null database");
        for (auto const& e : db_->table)
            f(e.second);
    }

    int
    getWriteLoad() override
    {
        return 0;
    }

    void
    setDeletePath() override
    {
    }

    [[nodiscard]] int
    fdRequired() const override
    {
        return 0;
    }
};

//------------------------------------------------------------------------------

MemoryFactory::MemoryFactory(Manager& manager) : manager_(manager)
{
    manager_.insert(*this);
}

std::string
MemoryFactory::getName() const
{
    return "Memory";
}

std::unique_ptr<Backend>
MemoryFactory::createInstance(
    size_t keyBytes,
    Section const& keyValues,
    std::size_t,
    Scheduler& scheduler,
    beast::Journal journal)
{
    return std::make_unique<MemoryBackend>(keyBytes, keyValues, journal);
}

}  // namespace xrpl::NodeStore
