#include <xrpl/config/BasicConfig.h>
#include <xrpl/nodestore/Factory.h>
#include <xrpl/nodestore/Manager.h>
#include <xrpl/nodestore/detail/NullBackend.h>

#include <boost/core/ignore_unused.hpp>

#include <memory>
#include <string>

namespace xrpl {
namespace node_store {

/**
 * [node_db] type=rwdb is a NullBackend. Ledger state is retained
 * through Ledger → SHAMap shared_ptr chains, not this map.
 */
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
        boost::ignore_unused(keyBytes);
        boost::ignore_unused(burstSize);
        boost::ignore_unused(scheduler);
        boost::ignore_unused(journal);
        return std::make_unique<NullBackend>(get(keyValues, "path"));
    }
};

void
registerRWDBFactory(Manager& manager)
{
    static RWDBFactory instance{manager};
}

}  // namespace node_store
}  // namespace xrpl
