#pragma once

#include <xrpl/basics/BasicConfig.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/nodestore/Backend.h>
#include <xrpl/nodestore/Scheduler.h>

#include <nudb/store.hpp>

namespace xrpl::NodeStore {

/** Abstract factory for constructing pluggable NodeStore `Backend` instances.
 *
 *  Each concrete subclass wraps one storage engine (NuDB, RocksDB, memory,
 *  null). Subclasses register themselves with the `Manager` singleton at
 *  program startup by calling `Manager::insert(*this)` from their constructor,
 *  typically via a module-level `register*Factory()` free function that holds
 *  the factory as a function-local static. `Manager::find()` then resolves the
 *  `type=` configuration string to the matching factory by name.
 *
 *  @note Factory objects are stored as raw (non-owning) pointers in
 *      `ManagerImp`. Concrete factories registered as function-local statics
 *      have program lifetime and must outlive the `Manager`.
 *
 *  @see Backend, Manager
 */
class Factory
{
public:
    virtual ~Factory() = default;

    /** Return the configuration type string that identifies this backend.
     *
     *  The returned name is used as the lookup key by `Manager::find()`,
     *  which compares case-insensitively against the `type=` value in the
     *  `[node_db]` config section (e.g., `"NuDB"`, `"RocksDB"`, `"memory"`).
     *
     *  @return The backend type name (e.g., `"NuDB"`).
     */
    [[nodiscard]] virtual std::string
    getName() const = 0;

    /** Construct a Backend from configuration, without a shared NuDB context.
     *
     *  The returned backend has not yet been opened; the caller must invoke
     *  `Backend::open()` before performing any I/O. In production, this is
     *  done by `ManagerImp::makeDatabase()`.
     *
     *  @param keyBytes Fixed width of every storage key in bytes. Always 32
     *      (SHA-512 Half) in production; may differ in tests.
     *  @param parameters Key/value pairs from the `[node_db]` config section,
     *      supplying backend-specific settings such as `path` and
     *      `nudb_block_size`.
     *  @param burstSize Maximum bytes the backend may buffer before flushing.
     *      Flows directly into NuDB's `db_.set_burst()` after open; other
     *      backends may use or ignore it.
     *  @param scheduler Async task dispatcher for background write jobs.
     *  @param journal Logging sink for backend diagnostics.
     *  @return An unopened, uniquely-owned Backend instance.
     */
    virtual std::unique_ptr<Backend>
    createInstance(
        size_t keyBytes,
        Section const& parameters,
        std::size_t burstSize,
        Scheduler& scheduler,
        beast::Journal journal) = 0;

    /** Construct a Backend sharing an existing NuDB I/O context.
     *
     *  This overload is provided for NuDB backends that share a `nudb::context`
     *  thread pool across multiple backends (e.g., the rotating database used
     *  for shard imports). Non-NuDB factories inherit a default implementation
     *  that returns an empty `unique_ptr`, signaling to `ManagerImp` that this
     *  backend does not use a NuDB context; the caller falls back to the
     *  context-free overload in that case.
     *
     *  @param keyBytes Fixed width of every storage key in bytes.
     *  @param parameters Key/value pairs from the `[node_db]` config section.
     *  @param burstSize Maximum bytes the backend may buffer before flushing.
     *  @param scheduler Async task dispatcher for background write jobs.
     *  @param context Shared NuDB I/O thread pool. Ignored by non-NuDB backends.
     *  @param journal Logging sink for backend diagnostics.
     *  @return An unopened Backend, or an empty `unique_ptr` if this factory
     *      does not support the NuDB context overload.
     */
    virtual std::unique_ptr<Backend>
    createInstance(
        size_t keyBytes,
        Section const& parameters,
        std::size_t burstSize,
        Scheduler& scheduler,
        nudb::context& context,
        beast::Journal journal)
    {
        return {};
    }
};

}  // namespace xrpl::NodeStore
