#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/nodestore/Backend.h>
#include <xrpl/nodestore/NodeObject.h>
#include <xrpl/nodestore/Types.h>

#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace xrpl::node_store {

/**
 * Do-nothing backend. fetch() is always NotFound; store() is a no-op.
 *
 * The default constructor is the historic `type=none` backend: empty
 * name, isOpen() always false. The named constructor is used for
 * `type=rwdb`, which reports a path and tracks open/close.
 */
class NullBackend : public Backend
{
    std::string name_;
    bool const trackOpen_{false};
    bool isOpen_{false};

public:
    NullBackend() = default;

    explicit NullBackend(std::string name) : name_(std::move(name)), trackOpen_(true)
    {
        if (name_.empty())
            name_ = "node_db";
    }

    ~NullBackend() override
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
        if (!trackOpen_)
            return;
        if (isOpen_)
            Throw<std::runtime_error>("already open");
        isOpen_ = true;
    }

    bool
    isOpen() override
    {
        return trackOpen_ && isOpen_;
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

}  // namespace xrpl::node_store
