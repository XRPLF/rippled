#include <xrpl/beast/utility/Journal.h>
#include <xrpl/config/BasicConfig.h>
#include <xrpl/nodestore/Backend.h>
#include <xrpl/nodestore/Factory.h>
#include <xrpl/nodestore/Manager.h>
#include <xrpl/nodestore/Scheduler.h>
#include <xrpl/nodestore/detail/NullBackend.h>

#include <cstddef>
#include <memory>
#include <string>

namespace xrpl::node_store {

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

}  // namespace xrpl::node_store
