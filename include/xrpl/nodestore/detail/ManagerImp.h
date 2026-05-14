/** @file
 *  Declares `ManagerImp`, the concrete Meyers-singleton implementation of the
 *  NodeStore `Manager` interface, hidden in `detail/` as an implementation
 *  detail not intended for direct use outside the nodestore subsystem.
 */
#pragma once

#include <xrpl/nodestore/Manager.h>

namespace xrpl::NodeStore {

/** Concrete singleton implementation of the NodeStore backend registry.
 *
 *  `ManagerImp` maintains a runtime registry of `Factory` objects and
 *  orchestrates `Backend` and `Database` construction from configuration data.
 *  The four built-in backends (NuDB, RocksDB, Memory, Null) are registered
 *  during construction by calling their respective `register*Factory` free
 *  functions, each of which holds a function-local static `Factory` that
 *  self-registers via `insert()`. This avoids relying on global-variable
 *  destruction order across translation units, which would be undefined
 *  behaviour if a `Factory` destructor called `erase()` after `ManagerImp`
 *  had already been destroyed.
 *
 *  The registry is a `std::vector<Factory*>` of non-owning pointers protected
 *  by a `std::mutex`. Ownership of each `Factory` remains with the static
 *  storage managed by the `register*Factory` functions, which are guaranteed
 *  to outlive this singleton.
 *
 *  @note Callers outside the nodestore subsystem should use `Manager::instance()`
 *      rather than `ManagerImp::instance()` to avoid depending on this
 *      implementation-detail type.
 *
 *  @see Manager, Factory, DatabaseNodeImp
 */
class ManagerImp : public Manager
{
private:
    std::mutex mutex_;
    std::vector<Factory*> list_;

public:
    /** Return the process-wide ManagerImp singleton.
     *
     *  Uses a Meyers function-local static for thread-safe, once-only
     *  construction under C++11 and later. All four built-in backend factories
     *  are registered before the reference is returned for the first time.
     *
     *  @return Reference to the single ManagerImp instance.
     */
    static ManagerImp&
    instance();

    /** Throw a user-facing error when the backend configuration is absent or
     *  names an unrecognised type.
     *
     *  Both the missing-`type`-key and the unrecognised-type code paths in
     *  `makeBackend()` converge on this helper so the operator-facing message
     *  is consistent.
     *
     *  @throws std::runtime_error Always — message directs the operator to add
     *      or correct the `[node_db]` section in `xrpld.cfg`.
     */
    static void
    missingBackend();

    /** Register all built-in backend factories.
     *
     *  Calls `registerNuDBFactory`, `registerRocksDBFactory`,
     *  `registerNullFactory`, and `registerMemoryFactory`. Each function
     *  creates a function-local static `Factory` that calls `insert()` on this
     *  manager. The function-local-static lifetime guarantee ensures all
     *  factories are destroyed before this `ManagerImp`.
     */
    ManagerImp();

    ~ManagerImp() override = default;

    /** @copydoc Manager::find */
    Factory*
    find(std::string const& name) override;

    /** @copydoc Manager::insert */
    void
    insert(Factory& factory) override;

    /** @copydoc Manager::erase */
    void
    erase(Factory& factory) override;

    /** @copydoc Manager::makeBackend */
    std::unique_ptr<Backend>
    makeBackend(
        Section const& parameters,
        std::size_t burstSize,
        Scheduler& scheduler,
        beast::Journal journal) override;

    /** @copydoc Manager::makeDatabase */
    std::unique_ptr<Database>
    makeDatabase(
        std::size_t burstSize,
        Scheduler& scheduler,
        int readThreads,
        Section const& config,
        beast::Journal journal) override;
};

}  // namespace xrpl::NodeStore
