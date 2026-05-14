#pragma once

#include <xrpl/nodestore/DatabaseRotating.h>
#include <xrpl/nodestore/Factory.h>

namespace xrpl::NodeStore {

/** Abstract interface for the NodeStore backend registry and factory.
 *
 *  `Manager` maps the `type=` string from `[node_db]` in `xrpld.cfg` to the
 *  concrete `Backend` implementation that implements it, and exposes the two
 *  construction entry points the rest of the application needs: `makeBackend()`
 *  for a raw storage engine and `makeDatabase()` for a fully-wired `Database`.
 *
 *  The concrete implementation is `ManagerImp`, a Meyers singleton returned by
 *  `instance()`. Its constructor eagerly registers the four built-in backends
 *  (NuDB, RocksDB, memory, null). Additional backends may be registered at
 *  runtime via `insert()`. The abstract base class is exposed here so callers
 *  depend only on the interface without being coupled to `ManagerImp` or its
 *  dependencies.
 *
 *  All registry operations (`insert`, `erase`, `find`) are protected by an
 *  internal mutex and are safe to call concurrently.
 *
 *  @note Copy construction and copy assignment are deleted — there is exactly
 *      one manager for the lifetime of the process.
 *
 *  @see Factory, Backend, Database
 */
class Manager
{
public:
    virtual ~Manager() = default;
    Manager() = default;
    Manager(Manager const&) = delete;
    Manager&
    operator=(Manager const&) = delete;

    /** Return the process-wide Manager singleton.
     *
     *  Delegates to `ManagerImp::instance()`, which uses a Meyers static local
     *  for thread-safe, once-only initialization under C++11 and later. The
     *  four built-in backends are registered before the reference is returned
     *  for the first time.
     *
     *  @return A reference to the singleton `ManagerImp`.
     */
    static Manager&
    instance();

    /** Register a backend factory with the manager.
     *
     *  After insertion, `find(factory.getName())` will return `&factory`. The
     *  call is protected by a mutex and safe to make concurrently. The manager
     *  stores a non-owning pointer; the caller is responsible for ensuring the
     *  factory outlives the manager (function-local statics are the idiomatic
     *  approach).
     *
     *  @param factory The factory to register. Must remain alive for the
     *      lifetime of the manager.
     */
    virtual void
    insert(Factory& factory) = 0;

    /** Deregister a previously inserted backend factory.
     *
     *  Removes `factory` from the internal list. The call is protected by a
     *  mutex. Passing a pointer that was never inserted triggers an
     *  `XRPL_ASSERT`.
     *
     *  @note Built-in backend factories registered by `ManagerImp`'s
     *      constructor are intentionally never erased: because static-storage
     *      destruction order across translation units is undefined, calling
     *      `erase()` from a `Factory` destructor could invoke a destroyed
     *      `ManagerImp`. The built-in factories use function-local statics
     *      that outlive the manager.
     *
     *  @param factory The factory to remove. Must have been previously passed
     *      to `insert()`.
     */
    virtual void
    erase(Factory& factory) = 0;

    /** Look up a factory by its type name.
     *
     *  Comparison is case-insensitive (via `boost::iequals`), so `"NuDB"`,
     *  `"nudb"`, and `"NUDB"` all resolve to the same factory. The call is
     *  protected by a mutex.
     *
     *  @param name The backend type name to search for (e.g., `"NuDB"`).
     *  @return Pointer to the matching `Factory`, or `nullptr` if none found.
     */
    virtual Factory*
    find(std::string const& name) = 0;

    /** Construct an unopened Backend from configuration parameters.
     *
     *  Reads the `type` key from `parameters`, resolves it to a registered
     *  `Factory` via `find()`, and delegates to `Factory::createInstance()`.
     *  The returned backend has not yet been opened; the caller must invoke
     *  `Backend::open()` before performing any I/O (this is done automatically
     *  by `makeDatabase()`).
     *
     *  @param parameters Key/value pairs from the `[node_db]` config section.
     *      Must contain a `type` key naming a registered backend.
     *  @param burstSize Maximum bytes the backend may buffer before flushing.
     *  @param scheduler Async task dispatcher for background write jobs.
     *  @param journal Logging sink for backend diagnostics.
     *  @return A uniquely-owned, unopened Backend instance.
     *  @throws std::runtime_error If the `type` key is absent or names an
     *      unrecognised backend, with a message directing the operator to
     *      check `xrpld.cfg`.
     */
    virtual std::unique_ptr<Backend>
    makeBackend(
        Section const& parameters,
        std::size_t burstSize,
        Scheduler& scheduler,
        beast::Journal journal) = 0;

    /** Construct and open a fully-wired Database backed by a single backend.
     *
     *  Calls `makeBackend()` to create and open the backend, then wraps it in
     *  a `DatabaseNodeImp` which adds an async read-thread pool and the full
     *  `Database` API. The `backendParameters` section must contain a `type`
     *  key naming a registered backend; most backends also require a `path`
     *  key. Currently registered built-in types are: `NuDB`, `RocksDB`,
     *  `memory`, `none`.
     *
     *  @param burstSize Maximum bytes the backend may buffer before flushing.
     *  @param scheduler Async task dispatcher for read and write jobs.
     *  @param readThreads Number of threads in the async read pool.
     *  @param backendParameters Key/value pairs for the persistent backend,
     *      including at minimum a `type` key.
     *  @param journal Logging sink for database diagnostics.
     *  @return A uniquely-owned, open Database ready for I/O.
     *  @throws std::runtime_error If the backend cannot be created or opened,
     *      or if the `type` key is missing or unrecognised.
     */
    virtual std::unique_ptr<Database>
    makeDatabase(
        std::size_t burstSize,
        Scheduler& scheduler,
        int readThreads,
        Section const& backendParameters,
        beast::Journal journal) = 0;
};

}  // namespace xrpl::NodeStore
