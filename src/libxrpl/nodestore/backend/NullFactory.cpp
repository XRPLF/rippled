/** @file
 *  No-op NodeStore backend: every read misses, every write is discarded.
 *
 *  Provides the `"none"` backend registered with the `Manager` singleton so
 *  that `type=none` in `[node_db]` is a valid, safe configuration rather than
 *  a fatal startup error. Also useful in tests that need a `Backend` interface
 *  without real storage.
 */
#include <xrpl/basics/BasicConfig.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/nodestore/Backend.h>
#include <xrpl/nodestore/Factory.h>
#include <xrpl/nodestore/Manager.h>
#include <xrpl/nodestore/NodeObject.h>
#include <xrpl/nodestore/Scheduler.h>
#include <xrpl/nodestore/Types.h>

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace xrpl::NodeStore {

/** No-op `Backend` implementation that stores nothing.
 *
 *  All reads return `Status::NotFound` immediately. All writes are silently
 *  dropped. The backend never transitions to an open state, so `isOpen()`
 *  always returns `false` and `fdRequired()` returns zero.
 *
 *  Serves as the minimal reference implementation of the `Backend` interface:
 *  every pure-virtual method is present in its simplest possible form.
 *
 *  @see NullFactory, registerNullFactory
 */
class NullBackend : public Backend
{
public:
    NullBackend() = default;

    ~NullBackend() override = default;

    /** Returns an empty string; the null backend has no named file or path. */
    std::string
    getName() override
    {
        return std::string();
    }

    /** No-op: the null backend requires no initialization. */
    void
    open(bool createIfMissing) override
    {
    }

    /** Always returns `false`; the null backend never enters an open state. */
    bool
    isOpen() override
    {
        return false;
    }

    /** No-op: there is nothing to flush or close. */
    void
    close() override
    {
    }

    /** Always reports the key as absent; no storage is consulted.
     *
     *  @param hash  Ignored.
     *  @param pObject  Ignored; left unchanged.
     *  @return `Status::NotFound` unconditionally.
     */
    Status
    fetch(uint256 const&, std::shared_ptr<NodeObject>*) override
    {
        return Status::NotFound;
    }

    /** Always reports every key as absent; no storage is consulted.
     *
     *  @param hashes  Ignored.
     *  @return A default-constructed pair: an empty results vector and a
     *      default `Status`, indicating nothing was found.
     */
    std::pair<std::vector<std::shared_ptr<NodeObject>>, Status>
    fetchBatch(std::vector<uint256> const& hashes) override
    {
        return {};
    }

    /** No-op: the object is silently discarded. */
    void
    store(std::shared_ptr<NodeObject> const& object) override
    {
    }

    /** No-op: the batch is silently discarded. */
    void
    storeBatch(Batch const& batch) override
    {
    }

    /** No-op: there is no write buffer to flush. */
    void
    sync() override
    {
    }

    /** No-op: the callback is never invoked because there are no stored objects. */
    void
    forEach(std::function<void(std::shared_ptr<NodeObject>)> f) override
    {
    }

    /** Always returns zero; all writes are immediately discarded. */
    int
    getWriteLoad() override
    {
        return 0;
    }

    /** No-op: there are no on-disk files to schedule for deletion. */
    void
    setDeletePath() override
    {
    }

    /** Always returns zero; the null backend opens no file descriptors. */
    [[nodiscard]] int
    fdRequired() const override
    {
        return 0;
    }

private:
};

//------------------------------------------------------------------------------

/** `Factory` that produces `NullBackend` instances and registers as `"none"`.
 *
 *  Instantiated as a function-local static inside `registerNullFactory()` so
 *  that it is constructed after the `Manager` singleton and destroyed before
 *  it, avoiding initialization-order hazards between translation units.
 *
 *  @see NullBackend, registerNullFactory
 */
class NullFactory : public Factory
{
private:
    Manager& manager_;

public:
    /** Register this factory with `manager` on construction.
     *
     *  @param manager The `Manager` singleton into which this factory
     *      is inserted. The reference must remain valid for the lifetime
     *      of this object (guaranteed when used as a function-local static
     *      inside `registerNullFactory`).
     */
    explicit NullFactory(Manager& manager) : manager_(manager)
    {
        manager_.insert(*this);
    }

    /** Returns `"none"`, the configuration type string for this backend.
     *
     *  Matched case-insensitively against the `type=` key in `[node_db]`,
     *  so `type=none`, `type=None`, and `type=NONE` all select this backend.
     */
    [[nodiscard]] std::string
    getName() const override
    {
        return "none";
    }

    /** Constructs a new `NullBackend`; all parameters are ignored.
     *
     *  @return An unopened `NullBackend`. Calling `open()` on it is a no-op,
     *      so the returned instance is immediately usable.
     */
    std::unique_ptr<Backend>
    createInstance(size_t, Section const&, std::size_t, Scheduler&, beast::Journal) override
    {
        return std::make_unique<NullBackend>();
    }
};

/** Register the null backend factory with the `Manager` singleton.
 *
 *  Called once by `ManagerImp`'s constructor alongside the equivalent
 *  registration functions for NuDB, RocksDB, and the memory backend. The
 *  `NullFactory` is held as a `const` function-local static, guaranteeing
 *  thread-safe, once-only initialization and a destruction order that is
 *  always before the `Manager` singleton — preventing any use-after-destroy
 *  when the factory's destructor eventually calls `manager.erase()`.
 *
 *  @param manager The `Manager` singleton to register the factory with.
 */
void
registerNullFactory(Manager& manager)
{
    static NullFactory const kINSTANCE{manager};
}

}  // namespace xrpl::NodeStore
