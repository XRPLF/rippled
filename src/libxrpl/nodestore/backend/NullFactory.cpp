#include <xrpl/basics/BasicConfig.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/nodestore/Backend.h>
#include <xrpl/nodestore/Factory.h>
#include <xrpl/nodestore/Manager.h>
#include <xrpl/nodestore/NodeObject.h>
#include <xrpl/nodestore/Scheduler.h>
#include <xrpl/nodestore/Types.h>
#include <xrpl/basics/TraceLog.h>

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
    TRACE_FUNC();
        return std::string();
    }

    void
    open(bool createIfMissing) override
    {
    }

    bool
    isOpen() override
    {
    TRACE_FUNC();
        return false;
    }

    void
    close() override
    {
    }

    Status
    fetch(uint256 const&, std::shared_ptr<NodeObject>*) override
    {
    TRACE_FUNC();
        return Status::NotFound;
    }

    std::pair<std::vector<std::shared_ptr<NodeObject>>, Status>
    fetchBatch(std::vector<uint256> const& hashes) override
    {
    TRACE_FUNC();
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
    TRACE_FUNC();
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
    TRACE_FUNC();
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
    TRACE_FUNC();
        manager_.insert(*this);
    }

    [[nodiscard]] std::string
    getName() const override
    {
    TRACE_FUNC();
        return "none";
    }

    std::unique_ptr<Backend>
    createInstance(size_t, Section const&, std::size_t, Scheduler&, beast::Journal) override
    {
    TRACE_FUNC();
        return std::make_unique<NullBackend>();
    }
};

void
registerNullFactory(Manager& manager)
{
    TRACE_FUNC();
    static NullFactory const kINSTANCE{manager};
}

}  // namespace xrpl::NodeStore
