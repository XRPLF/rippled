#include <xrpl/basics/BasicConfig.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/nodestore/Backend.h>
#include <xrpl/nodestore/Factory.h>
#include <xrpl/nodestore/Manager.h>
#include <xrpl/nodestore/NodeObject.h>
#include <xrpl/nodestore/Scheduler.h>
#include <xrpl/nodestore/Types.h>

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace xrpl::NodeStore {

class NullBackend : public Backend
{
public:
    NullBackend() = default;

    ~NullBackend() override = default;

    std::string
    getName() override
    {
        return std::string();
    }

    void
    open(bool createIfMissing) override
    {
    }

    bool
    isOpen() override
    {
        return false;
    }

    void
    close() override
    {
    }

    Status
    fetch(uint256 const&, std::shared_ptr<NodeObject>*) override
    {
        return Status::NotFound;
    }

    std::pair<std::vector<std::shared_ptr<NodeObject>>, Status>
    fetchBatch(std::vector<uint256> const& hashes) override
    {
        return {};
    }

    void
    store(std::shared_ptr<NodeObject> const& object) override
    {
    }

    void
    storeBatch(Batch const& batch) override
    {
    }

    void
    sync() override
    {
    }

    void
    forEach(std::function<void(std::shared_ptr<NodeObject>)> f) override
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
    }

    /** Returns the number of file descriptors the backend expects to need */
    [[nodiscard]] int
    fdRequired() const override
    {
        return 0;
    }

private:
};

//------------------------------------------------------------------------------

class NullFactory : public Factory
{
private:
    Manager& manager_;

public:
    explicit NullFactory(Manager& manager) : manager_(manager)
    {
        manager_.insert(*this);
    }

    [[nodiscard]] std::string
    getName() const override
    {
        return "none";
    }

    std::unique_ptr<Backend>
    createInstance(size_t, Section const&, std::size_t, Scheduler&, beast::Journal) override
    {
        return std::make_unique<NullBackend>();
    }
};

void
registerNullFactory(Manager& manager)
{
    static NullFactory const kInstance{manager};
}

}  // namespace xrpl::NodeStore
