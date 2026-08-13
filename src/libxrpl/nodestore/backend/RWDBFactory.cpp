#include <xrpl/basics/contract.h>
#include <xrpl/config/BasicConfig.h>
#include <xrpl/nodestore/Factory.h>
#include <xrpl/nodestore/Manager.h>
#include <xrpl/nodestore/Types.h>

#include <boost/core/ignore_unused.hpp>

#include <functional>
#include <memory>
#include <string>

namespace xrpl {
namespace node_store {

/**
 * Explicit null node-store backend used when [node_db] type=rwdb.
 *
 * fetch() always returns NotFound and store() is a no-op. Ledger state is
 * retained through Ledger → SHAMap shared_ptr chains, not this map.
 */
class RWDBBackend : public Backend
{
private:
    std::string name_;
    bool isOpen_{false};

public:
    RWDBBackend(size_t keyBytes, Section const& keyValues, beast::Journal journal)
        : name_(get(keyValues, "path"))
    {
        boost::ignore_unused(keyBytes);
        boost::ignore_unused(journal);
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
        if (isOpen_)
            Throw<std::runtime_error>("already open");
        isOpen_ = true;
    }

    bool
    isOpen() override
    {
        return isOpen_;
    }

    void
    close() override
    {
        isOpen_ = false;
    }

    Status
    fetch(uint256 const&, std::shared_ptr<NodeObject>*) override
    {
        return Status::NotFound;
    }

    void
    store(std::shared_ptr<NodeObject> const&) override
    {
    }

    void
    storeBatch(Batch const&) override
    {
    }

    void
    sync() override
    {
    }

    void
    forEach(std::function<void(std::shared_ptr<NodeObject>)>) override
    {
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

}  // namespace node_store
}  // namespace xrpl
