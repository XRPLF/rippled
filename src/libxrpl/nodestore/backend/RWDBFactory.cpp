#include <xrpl/basics/ReaderPreferringSharedMutex.h>
#include <xrpl/basics/contract.h>
#include <xrpl/nodestore/Factory.h>
#include <xrpl/nodestore/Manager.h>
#include <xrpl/nodestore/detail/DecodedBlob.h>
#include <xrpl/nodestore/detail/EncodedBlob.h>
#include <xrpl/nodestore/detail/codec.h>

#include <boost/core/ignore_unused.hpp>

#include <cstdint>
#include <cstdlib>
#include <map>
#include <memory>
#include <shared_mutex>
#include <string_view>
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
    mutable reader_preferring_shared_mutex mutex_;
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
        std::unique_lock lock(mutex_);
        if (isOpen_)
            Throw<std::runtime_error>("already open");
        isOpen_ = true;
    }

    bool
    isOpen() override
    {
        std::shared_lock lock(mutex_);
        return isOpen_;
    }

    void
    close() override
    {
        DataStore old;
        {
            std::unique_lock lock(mutex_);
            isOpen_ = false;
            old.swap(table_);  // O(1) swap; release lock before destructor runs
        }
        // 'old' is now destroyed outside the lock — no fetch() can be
        // blocked by the (potentially millions-of-entries) map destructor.
    }

    static bool
    nullMode()
    {
        static bool const v = [] {
            char const* e = std::getenv("XRPL_RWDB_NULL");
            return e && *e && std::string_view{e} != "0";
        }();
        return v;
    }

    Status
    fetch(uint256 const& hash, std::shared_ptr<NodeObject>* pObject) override
    {
        if (nullMode())
            return notFound;

        std::shared_lock lock(mutex_);
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
        if (!object)
            return;

        if (nullMode())
            return;

        std::unique_lock lock(mutex_);
        if (!isOpen_)
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
        std::shared_lock lock(mutex_);
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
