#include <xrpl/basics/contract.h>
#include <xrpl/nodestore/Factory.h>
#include <xrpl/nodestore/Manager.h>
#include <xrpl/nodestore/detail/DecodedBlob.h>
#include <xrpl/nodestore/detail/EncodedBlob.h>
#include <xrpl/nodestore/detail/codec.h>

#include <boost/core/ignore_unused.hpp>

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace xrpl {
namespace NodeStore {

class RWDBBackend : public Backend
{
private:
    using DataStore = std::map<uint256 const, std::shared_ptr<NodeObject>>;

    std::string name_;
    beast::Journal const journal_;
    bool isOpen_{false};
    mutable std::recursive_mutex mutex_;
    DataStore table_;

public:
    RWDBBackend(size_t keyBytes, Section const& keyValues, beast::Journal journal)
        : name_(get(keyValues, "path")), journal_(journal)
    {
        boost::ignore_unused(keyBytes);
        boost::ignore_unused(journal_);
        if (name_.empty())
            name_ = "node_db";
    }

    ~RWDBBackend() override
    {
        try
        {
            close();
        }
        catch (std::exception const&)  // NOLINT(bugprone-empty-catch)
        {
        }
    }

    std::string
    getName() override
    {
        return name_;
    }

    void
    open(bool) override
    {
        std::lock_guard lock(mutex_);
        if (isOpen_)
            Throw<std::runtime_error>("already open");
        isOpen_ = true;
    }

    bool
    isOpen() override
    {
        std::lock_guard lock(mutex_);
        return isOpen_;
    }

    void
    close() override
    {
        std::lock_guard lock(mutex_);
        isOpen_ = false;
        table_.clear();
    }

    Status
    fetch(uint256 const& hash, std::shared_ptr<NodeObject>* pObject) override
    {
        std::lock_guard lock(mutex_);
        if (!isOpen_)
            return notFound;

        auto const iter = table_.find(hash);
        if (iter == table_.end())
            return notFound;

        *pObject = iter->second;
        return ok;
    }

    std::pair<std::vector<std::shared_ptr<NodeObject>>, Status>
    fetchBatch(std::vector<uint256> const& hashes) override
    {
        std::vector<std::shared_ptr<NodeObject>> results;
        results.reserve(hashes.size());
        for (auto const& h : hashes)
        {
            std::shared_ptr<NodeObject> nObj;
            Status status = fetch(h, &nObj);
            if (status != ok)
                results.push_back({});
            else
                results.push_back(nObj);
        }

        return {results, ok};
    }

    void
    store(std::shared_ptr<NodeObject> const& object) override
    {
        std::lock_guard lock(mutex_);
        if (!isOpen_ || !object)
            return;

        table_[object->getHash()] = object;
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
    for_each(std::function<void(std::shared_ptr<NodeObject>)> f) override
    {
        std::lock_guard lock(mutex_);
        if (!isOpen_)
            return;

        for (auto const& entry : table_)
            f(entry.second);
    }

    int
    getWriteLoad() override
    {
        return 0;
    }

    void
    setDeletePath() override
    {
        close();
    }

    int
    fdRequired() const override
    {
        return 0;
    }
};

class RWDBFactory : public Factory
{
public:
    explicit RWDBFactory(Manager& manager)
    {
        manager.insert(*this);
    }

    std::string
    getName() const override
    {
        return "RWDB";
    }

    std::unique_ptr<Backend>
    createInstance(
        size_t keyBytes,
        Section const& keyValues,
        std::size_t burstSize,
        Scheduler& scheduler,
        beast::Journal journal) override
    {
        boost::ignore_unused(burstSize);
        boost::ignore_unused(scheduler);
        return std::make_unique<RWDBBackend>(keyBytes, keyValues, journal);
    }
};

void
registerRWDBFactory(Manager& manager)
{
    static RWDBFactory instance{manager};
}

}  // namespace NodeStore
}  // namespace xrpl
