#include <xrpl/nodestore/detail/DatabaseNodeImp.h>
#include <xrpl/nodestore/detail/ManagerImp.h>

#include <boost/algorithm/string/predicate.hpp>

namespace ripple {

namespace NodeStore {

ManagerImp&
ManagerImp::instance()
{
    static ManagerImp _;
    return _;
}

void
ManagerImp::missing_backend()
{
    Throw<std::runtime_error>(
        "Your rippled.cfg is missing a [node_db] entry, "
        "please see the rippled-example.cfg file!");
}

// We shouldn't rely on global variables for lifetime management because their
// lifetime is not well-defined. ManagerImp may get destroyed before the Factory
// classes, and then, calling Manager::instance().erase() in the destructors of
// the Factory classes is an undefined behaviour.
void
registerNuDBFactory(Manager& manager);
void
registerRocksDBFactory(Manager& manager);
void
registerNullFactory(Manager& manager);
void
registerMemoryFactory(Manager& manager);

ManagerImp::ManagerImp()
{
    registerNuDBFactory(*this);
    registerRocksDBFactory(*this);
    registerNullFactory(*this);
    registerMemoryFactory(*this);
}

std::unique_ptr<Backend>
ManagerImp::make_Backend(
    Section const& parameters,
    std::size_t burstSize,
    Scheduler& scheduler,
    beast::Journal journal)
{
    std::string const type{get(parameters, "type")};
    if (type.empty())
        missing_backend();

    auto factory{find(type)};
    if (!factory)
    {
        missing_backend();
    }

    return factory->createInstance(
        NodeObject::keyBytes, parameters, burstSize, scheduler, journal);
}

std::unique_ptr<Database>
ManagerImp::make_Database(
    std::size_t burstSize,
    Scheduler& scheduler,
    int readThreads,
    Section const& config,
    beast::Journal journal)
{
    auto backend{make_Backend(config, burstSize, scheduler, journal)};
    backend->open();
    return std::make_unique<DatabaseNodeImp>(
        scheduler, readThreads, std::move(backend), config, journal);
}

void
ManagerImp::insert(Factory& factory)
{
    std::lock_guard _(mutex_);
    list_.push_back(&factory);
}

void
ManagerImp::erase(Factory& factory)
{
    std::lock_guard _(mutex_);
    auto const iter =
        std::find_if(list_.begin(), list_.end(), [&factory](Factory* other) {
            return other == &factory;
        });
    XRPL_ASSERT(
        iter != list_.end(),
        "ripple::NodeStore::ManagerImp::erase : valid input");
    list_.erase(iter);
}

Factory*
ManagerImp::find(std::string const& name)
{
    std::lock_guard _(mutex_);
    auto const iter =
        std::find_if(list_.begin(), list_.end(), [&name](Factory* other) {
            return boost::iequals(name, other->getName());
        });
    if (iter == list_.end())
        return nullptr;
    return *iter;
}

//------------------------------------------------------------------------------

Manager&
Manager::instance()
{
    return ManagerImp::instance();
}

}  // namespace NodeStore
}  // namespace ripple
