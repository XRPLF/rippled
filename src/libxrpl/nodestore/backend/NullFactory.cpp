#include <xrpl/nodestore/Factory.h>
#include <xrpl/nodestore/Manager.h>

#include <memory>

namespace ripple {
namespace NodeStore {

class NullBackend : public Backend
{
public:
    NullBackend() = default;

    ~NullBackend() = default;

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
    fetch(void const*, std::shared_ptr<NodeObject>*) override
    {
        return notFound;
    }

    std::pair<std::vector<std::shared_ptr<NodeObject>>, Status>
    fetchBatch(std::vector<uint256 const*> const& hashes) override
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
    for_each(std::function<void(std::shared_ptr<NodeObject>)> f) override
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
    int
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

    std::string
    getName() const override
    {
        return "none";
    }

    std::unique_ptr<Backend>
    createInstance(
        size_t,
        Section const&,
        std::size_t,
        Scheduler&,
        beast::Journal) override
    {
        return std::make_unique<NullBackend>();
    }
};

void
registerNullFactory(Manager& manager)
{
    static NullFactory instance{manager};
}

}  // namespace NodeStore
}  // namespace ripple
