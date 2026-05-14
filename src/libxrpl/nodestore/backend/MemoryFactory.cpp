/** @file
 *  In-memory NodeStore backend for testing and ephemeral storage.
 *
 *  Defines `MemoryDB` (the raw storage cell), `MemoryBackend` (the `Backend`
 *  interface implementation), and `MemoryFactory` (the `Factory` and named-DB
 *  registry). Multiple `MemoryBackend` instances opened with the same path
 *  share a single `MemoryDB`, so the store survives backend close/reopen
 *  within a process. No data is ever written to disk.
 */
#include <xrpl/basics/BasicConfig.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/nodestore/Backend.h>
#include <xrpl/nodestore/Factory.h>
#include <xrpl/nodestore/Manager.h>
#include <xrpl/nodestore/NodeObject.h>
#include <xrpl/nodestore/Scheduler.h>
#include <xrpl/nodestore/Types.h>

#include <boost/beast/core/string.hpp>
#include <boost/core/ignore_unused.hpp>

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace xrpl::NodeStore {

/** Raw storage cell shared by all `MemoryBackend` instances with the same path.
 *
 *  `MemoryFactory` owns one `MemoryDB` per distinct path name and hands out
 *  raw pointers to `MemoryBackend`. Separating storage into this struct lets
 *  a backend close (nulling its pointer) without discarding the map — a
 *  subsequent `open()` on the same path recovers the same data.
 *
 *  @note `mutex` must be held for all reads and writes to `table`.
 *      `forEach` intentionally reads `table` without the lock; callers must
 *      guarantee no concurrent writes during enumeration.
 */
struct MemoryDB
{
    explicit MemoryDB() = default;

    /** Protects concurrent access to `table`. */
    std::mutex mutex;

    /** Guard flag checked by `MemoryFactory::open()`; never set to `true` in
     *  current code — the open-guard is effectively dead. */
    bool open = false;

    /** Hash-keyed object store; keys are `const` to prevent accidental rehash. */
    std::map<uint256 const, std::shared_ptr<NodeObject>> table;
};

/** Factory and named-registry for in-memory NodeStore backends.
 *
 *  Implements `Factory` (creating `MemoryBackend` instances) and acts as the
 *  owner of all `MemoryDB` storage cells, keyed by path name. Path lookups
 *  are case-insensitive via `boost::beast::iless`.
 *
 *  A single process-level instance is created by `registerMemoryFactory()`;
 *  its address is also stored in `gMemoryFactory` so `MemoryBackend::open()`
 *  can call back without holding a reference.
 */
class MemoryFactory : public Factory
{
private:
    std::mutex mutex_;
    std::map<std::string, MemoryDB, boost::beast::iless> map_;
    Manager& manager_;

public:
    /** Constructs and self-registers with `manager`.
     *
     *  @param manager The `Manager` singleton that this factory registers with.
     */
    explicit MemoryFactory(Manager& manager);

    /** Returns `"Memory"`, the config `type=` token for this backend. */
    [[nodiscard]] std::string
    getName() const override;

    /** Creates a new `MemoryBackend` for the given configuration.
     *
     *  @param keyBytes  Size of the hash key in bytes (must be 32 for
     *      `uint256`).
     *  @param keyValues Configuration section; must contain a non-empty
     *      `"path"` key.
     *  @param burstSize Ignored by this backend.
     *  @param scheduler Ignored by this backend.
     *  @param journal   Retained but currently unused by the backend.
     *  @return Owning pointer to the new `MemoryBackend`.
     *  @throw std::runtime_error if `"path"` is absent or empty.
     */
    std::unique_ptr<Backend>
    createInstance(
        size_t keyBytes,
        Section const& keyValues,
        std::size_t burstSize,
        Scheduler& scheduler,
        beast::Journal journal) override;

    /** Returns a reference to the `MemoryDB` for the given path, creating it
     *  if it does not yet exist.
     *
     *  Multiple backends opened with the same path share the returned
     *  `MemoryDB`, so the map contents survive individual backend lifecycles.
     *
     *  @param path The logical store name (case-insensitive).
     *  @return Reference into `map_`; valid for the lifetime of this factory.
     *  @throw std::runtime_error if `db.open` is `true` (dead code in the
     *      current implementation — `open` is never set to `true`).
     */
    MemoryDB&
    open(std::string const& path)
    {
        std::scoped_lock const _(mutex_);
        auto const result =
            map_.emplace(std::piecewise_construct, std::make_tuple(path), std::make_tuple());
        MemoryDB& db = result.first->second;
        if (db.open)
            Throw<std::runtime_error>("already open");
        return db;
    }
};

/** Module-level pointer to the singleton `MemoryFactory`.
 *
 *  Set by `registerMemoryFactory()` and used by `MemoryBackend::open()` to
 *  call back into the factory without requiring a stored reference. Valid
 *  for the entire process lifetime after `registerMemoryFactory()` is called.
 */
MemoryFactory* gMemoryFactory = nullptr;

/** Registers the in-memory backend with the global `Manager`.
 *
 *  Creates a function-local static `MemoryFactory`, which self-registers via
 *  its constructor and stores its address in `gMemoryFactory`. Safe to call
 *  from multiple threads — C++11 guarantees static-local initialization is
 *  performed exactly once. Must be called before any `type=Memory` config
 *  section is instantiated.
 *
 *  @param manager The `Manager` singleton to register with.
 */
void
registerMemoryFactory(Manager& manager)
{
    static MemoryFactory kINSTANCE{manager};
    gMemoryFactory = &kINSTANCE;
}

//------------------------------------------------------------------------------

/** `Backend` implementation backed by a heap-resident `std::map`.
 *
 *  Wraps a `MemoryDB` pointer obtained from `MemoryFactory`. All reads and
 *  writes are protected by `MemoryDB::mutex` except `forEach`, which requires
 *  the caller to guarantee no concurrent writes. The `createIfMissing`
 *  argument to `open()` is silently ignored — the store always starts empty
 *  and is created implicitly.
 *
 *  @note This backend consumes no file descriptors and has no write queue,
 *      so `fdRequired()` returns 0 and `getWriteLoad()` returns 0.
 *  @note `sync()` and `setDeletePath()` are intentional no-ops; there is no
 *      I/O to flush and no filesystem path to remove.
 */
class MemoryBackend : public Backend
{
private:
    using Map = std::map<uint256 const, std::shared_ptr<NodeObject>>;

    std::string name_;
    beast::Journal const journal_;
    MemoryDB* db_{nullptr};

public:
    /** Constructs the backend and validates configuration.
     *
     *  @param keyBytes  Key size in bytes; expected to be 32 for `uint256`.
     *  @param keyValues Config section; must contain a non-empty `"path"` key.
     *  @param journal   Retained for future diagnostics; currently unused.
     *  @throw std::runtime_error if `"path"` is absent or empty.
     */
    MemoryBackend(size_t keyBytes, Section const& keyValues, beast::Journal journal)
        : name_(get(keyValues, "path")), journal_(journal)
    {
        boost::ignore_unused(journal_);  // Keep unused journal_ just in case.
        if (name_.empty())
            Throw<std::runtime_error>("Missing path in Memory backend");
    }

    ~MemoryBackend() override
    {
        close();
    }

    /** Returns the path name used to identify the underlying `MemoryDB`. */
    std::string
    getName() override
    {
        return name_;
    }

    /** Acquires a pointer to the shared `MemoryDB` for this backend's path.
     *
     *  The `createIfMissing` flag is ignored; an in-memory store is always
     *  created on first access and never needs to be opened from disk.
     */
    void
    open(bool) override
    {
        db_ = &gMemoryFactory->open(name_);
    }

    /** Returns `true` while the backend holds a valid `MemoryDB` pointer. */
    bool
    isOpen() override
    {
        return static_cast<bool>(db_);
    }

    /** Releases the `MemoryDB` pointer without destroying the underlying data.
     *
     *  The `MemoryDB` remains alive in `MemoryFactory::map_`, so a subsequent
     *  `open()` with the same path recovers all previously stored objects.
     */
    void
    close() override
    {
        db_ = nullptr;
    }

    //--------------------------------------------------------------------------

    /** Looks up a single object by hash.
     *
     *  @param hash    The 256-bit key to look up.
     *  @param pObject Set to the found object on success; reset to null if not
     *      found.
     *  @return `Status::Ok` if found, `Status::NotFound` otherwise.
     *  @pre `isOpen()` must be true; asserted via `XRPL_ASSERT`.
     */
    Status
    fetch(uint256 const& hash, std::shared_ptr<NodeObject>* pObject) override
    {
        XRPL_ASSERT(db_, "xrpl::NodeStore::MemoryBackend::fetch : non-null database");

        std::scoped_lock const _(db_->mutex);

        Map::iterator const iter = db_->table.find(hash);
        if (iter == db_->table.end())
        {
            pObject->reset();
            return Status::NotFound;
        }
        *pObject = iter->second;
        return Status::Ok;
    }

    /** Looks up multiple objects by hash, one at a time.
     *
     *  Calls `fetch()` in a loop; the mutex is acquired and released for each
     *  element individually. Missing hashes produce a null entry in the result
     *  vector. The overall status is always `Status::Ok`.
     *
     *  @param hashes  Ordered list of hashes to fetch.
     *  @return A pair of (result vector, `Status::Ok`). The result vector has
     *      the same length as `hashes`; missing entries are null pointers.
     *  @pre `isOpen()` must be true (asserted inside each `fetch()` call).
     */
    std::pair<std::vector<std::shared_ptr<NodeObject>>, Status>
    fetchBatch(std::vector<uint256> const& hashes) override
    {
        std::vector<std::shared_ptr<NodeObject>> results;
        results.reserve(hashes.size());
        for (auto const& h : hashes)
        {
            std::shared_ptr<NodeObject> nObj;
            Status const status = fetch(h, &nObj);
            if (status != Status::Ok)
            {
                results.push_back({});
            }
            else
            {
                results.push_back(nObj);
            }
        }

        return {results, Status::Ok};
    }

    /** Inserts a single object, keyed by its hash.
     *
     *  If an object with the same hash already exists, `emplace` is a no-op
     *  (content-addressed: same hash implies same data).
     *
     *  @param object The object to store.
     *  @pre `isOpen()` must be true; asserted via `XRPL_ASSERT`.
     */
    void
    store(std::shared_ptr<NodeObject> const& object) override
    {
        XRPL_ASSERT(db_, "xrpl::NodeStore::MemoryBackend::store : non-null database");
        std::scoped_lock const _(db_->mutex);
        db_->table.emplace(object->getHash(), object);
    }

    /** Stores each object in `batch` by calling `store()` individually.
     *
     *  @param batch The collection of objects to persist.
     *  @pre `isOpen()` must be true (asserted inside each `store()` call).
     */
    void
    storeBatch(Batch const& batch) override
    {
        for (auto const& e : batch)
            store(e);
    }

    /** No-op. There is no I/O to flush for an in-memory store. */
    void
    sync() override
    {
    }

    /** Invokes `f` for every object in the store.
     *
     *  Iterates `db_->table` without holding `db_->mutex`. The caller must
     *  ensure no concurrent writes occur during enumeration.
     *
     *  @param f Callback invoked once per stored object.
     *  @pre `isOpen()` must be true; asserted via `XRPL_ASSERT`.
     */
    void
    forEach(std::function<void(std::shared_ptr<NodeObject>)> f) override
    {
        XRPL_ASSERT(db_, "xrpl::NodeStore::MemoryBackend::forEach : non-null database");
        for (auto const& e : db_->table)
            f(e.second);
    }

    /** Returns 0; there is no write queue for an in-memory backend. */
    int
    getWriteLoad() override
    {
        return 0;
    }

    /** No-op. There is no filesystem path to schedule for deletion. */
    void
    setDeletePath() override
    {
    }

    /** Returns 0; the memory backend uses no file descriptors. */
    [[nodiscard]] int
    fdRequired() const override
    {
        return 0;
    }
};

//------------------------------------------------------------------------------

MemoryFactory::MemoryFactory(Manager& manager) : manager_(manager)
{
    manager_.insert(*this);
}

std::string
MemoryFactory::getName() const
{
    return "Memory";
}

std::unique_ptr<Backend>
MemoryFactory::createInstance(
    size_t keyBytes,
    Section const& keyValues,
    std::size_t,
    Scheduler& scheduler,
    beast::Journal journal)
{
    return std::make_unique<MemoryBackend>(keyBytes, keyValues, journal);
}

}  // namespace xrpl::NodeStore
