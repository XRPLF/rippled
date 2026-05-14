/** @file
 *  Concrete implementation of the NodeStore Manager singleton.
 *
 *  Provides the Meyers-singleton `ManagerImp::instance()`, registers the four
 *  built-in storage backends (NuDB, RocksDB, Null, Memory), and implements the
 *  `Manager` interface for backend/database construction.
 */
#include <xrpl/nodestore/detail/ManagerImp.h>

#include <xrpl/basics/BasicConfig.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/nodestore/Backend.h>
#include <xrpl/nodestore/Database.h>
#include <xrpl/nodestore/Manager.h>
#include <xrpl/nodestore/NodeObject.h>
#include <xrpl/nodestore/Scheduler.h>
#include <xrpl/nodestore/detail/DatabaseNodeImp.h>

#include <boost/algorithm/string/predicate.hpp>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>

namespace xrpl::NodeStore {

/** Return the process-wide ManagerImp singleton.
 *
 *  Uses a Meyers function-local static so construction is thread-safe and
 *  lazy. All four built-in backends are registered during the first call.
 *
 *  @return Reference to the single ManagerImp instance.
 */
ManagerImp&
ManagerImp::instance()
{
    static ManagerImp k_;
    return k_;
}

/** Throw a user-facing error when the `[node_db]` config entry is absent or
 *  names an unrecognised backend type.
 *
 *  @throws std::runtime_error Always — directs the operator to add a
 *      `[node_db]` section to `xrpld.cfg`.
 */
void
ManagerImp::missingBackend()
{
    Throw<std::runtime_error>(
        "Your xrpld.cfg is missing a [node_db] entry, "
        "please see the xrpld-example.cfg file!");
}

// Each register* function creates a function-local static Factory whose
// constructor calls manager.insert(*this). Function-local statics are
// initialised after ManagerImp and destroyed before it, so the erase() calls
// in their destructors are safe. Using global variables instead would leave
// the destruction order undefined across translation units and could invoke
// erase() on an already-destroyed ManagerImp — undefined behaviour.
void
registerNuDBFactory(Manager& manager);
void
registerRocksDBFactory(Manager& manager);
void
registerNullFactory(Manager& manager);
void
registerMemoryFactory(Manager& manager);

/** Register all built-in backend factories.
 *
 *  Calls the four `register*Factory` free functions, each of which holds a
 *  function-local static factory that self-registers via `insert()`. The
 *  function-local-static lifetime guarantee ensures factories are always
 *  destroyed before this ManagerImp.
 */
ManagerImp::ManagerImp()
{
    registerNuDBFactory(*this);
    registerRocksDBFactory(*this);
    registerNullFactory(*this);
    registerMemoryFactory(*this);
}

/** Construct an unopened backend from a configuration section.
 *
 *  Reads the `"type"` key from `parameters`, performs a case-insensitive
 *  lookup against registered factories, and delegates to
 *  `Factory::createInstance`. The returned backend has not yet had `open()`
 *  called on it.
 *
 *  @param parameters Config section containing at minimum `type=<name>` and
 *      typically `path=<directory>`.
 *  @param burstSize Backend burst size hint passed through to the factory.
 *  @param scheduler Scheduler for async tasks used by the backend.
 *  @param journal Logging sink.
 *  @return An unopened `Backend` instance for the requested type.
 *  @throws std::runtime_error If the `"type"` key is absent or names no
 *      registered factory — the message directs the operator to fix
 *      `xrpld.cfg`.
 */
std::unique_ptr<Backend>
ManagerImp::makeBackend(
    Section const& parameters,
    std::size_t burstSize,
    Scheduler& scheduler,
    beast::Journal journal)
{
    std::string const type{get(parameters, "type")};
    if (type.empty())
        missingBackend();

    auto factory{find(type)};
    if (factory == nullptr)
    {
        missingBackend();
    }

    return factory->createInstance(
        NodeObject::kKEY_BYTES, parameters, burstSize, scheduler, journal);
}

/** Construct and open a fully ready `Database` from a configuration section.
 *
 *  Calls `makeBackend` to create the backend, explicitly calls `open()` on it
 *  so that I/O errors surface before the `Database` stack is assembled, then
 *  wraps it in a `DatabaseNodeImp` with the requested read-thread pool.
 *
 *  @param burstSize Backend burst size hint forwarded to `makeBackend`.
 *  @param scheduler Scheduler for async tasks.
 *  @param readThreads Number of async read threads to spawn.
 *  @param config Config section forwarded to `makeBackend`; must contain
 *      `type=<name>` at minimum.
 *  @param journal Logging sink.
 *  @return An open, ready-to-use `Database` instance.
 *  @throws std::runtime_error From `makeBackend` on bad config, or from
 *      `Backend::open()` if the storage layer cannot be initialised.
 */
std::unique_ptr<Database>
ManagerImp::makeDatabase(
    std::size_t burstSize,
    Scheduler& scheduler,
    int readThreads,
    Section const& config,
    beast::Journal journal)
{
    auto backend{makeBackend(config, burstSize, scheduler, journal)};
    backend->open();
    return std::make_unique<DatabaseNodeImp>(
        scheduler, readThreads, std::move(backend), config, journal);
}

/** Register a factory so it can be found by `find()` and `makeBackend()`.
 *
 *  Appends `factory` to the internal registry under `mutex_`. Typically
 *  called only from the `register*Factory` free functions during
 *  `ManagerImp` construction.
 *
 *  @param factory The factory to register; the caller retains ownership.
 */
void
ManagerImp::insert(Factory& factory)
{
    std::scoped_lock const _(mutex_);
    list_.push_back(&factory);
}

/** Remove a previously registered factory from the registry.
 *
 *  Uses `XRPL_ASSERT` to verify the pointer is present — passing an
 *  unregistered factory is a programming error, not a recoverable condition.
 *
 *  @param factory The factory to remove; must currently be in the registry.
 */
void
ManagerImp::erase(Factory& factory)
{
    std::scoped_lock const _(mutex_);
    auto const iter =
        std::ranges::find_if(list_, [&factory](Factory* other) { return other == &factory; });
    XRPL_ASSERT(iter != list_.end(), "xrpl::NodeStore::ManagerImp::erase : valid input");
    list_.erase(iter);
}

/** Find a registered factory by name, using case-insensitive comparison.
 *
 *  @param name The backend type name to look up (e.g. `"NuDB"`, `"nudb"`).
 *  @return Pointer to the matching `Factory`, or `nullptr` if not found.
 */
Factory*
ManagerImp::find(std::string const& name)
{
    std::scoped_lock const _(mutex_);
    auto const iter = std::ranges::find_if(
        list_, [&name](Factory* other) { return boost::iequals(name, other->getName()); });
    if (iter == list_.end())
        return nullptr;
    return *iter;
}

//------------------------------------------------------------------------------

/** Return the process-wide Manager singleton.
 *
 *  Forwards to `ManagerImp::instance()`, keeping the abstract `Manager`
 *  header free of implementation details.
 *
 *  @return Reference to the singleton Manager (actually a ManagerImp).
 */
Manager&
Manager::instance()
{
    return ManagerImp::instance();
}

}  // namespace xrpl::NodeStore
