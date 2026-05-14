#include <xrpl/rdb/DatabaseCon.h>

#include <xrpl/basics/contract.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/rdb/SociDB.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace xrpl {

/** Process-wide registry mapping integer IDs to live `Checkpointer` instances.
 *
 *  Each `DatabaseCon` that uses WAL checkpointing registers its `Checkpointer`
 *  here on construction and removes it on destruction. The numeric ID assigned
 *  by `create()` is cast to `void*` and passed to SQLite's `sqlite3_wal_hook`,
 *  so the C callback never holds a raw pointer to a C++ object — only an integer
 *  key that it resolves through this collection. If the collection entry has
 *  been erased (because the owning `DatabaseCon` was torn down), `fromId()`
 *  returns `nullptr` and the hook deregisters itself.
 *
 *  All public methods are thread-safe; access is serialised by `mutex_`.
 */
class CheckpointersCollection
{
    std::uintptr_t nextId_{0};
    std::mutex mutex_;
    std::unordered_map<std::uintptr_t, std::shared_ptr<Checkpointer>> checkpointers_;

public:
    /** Look up a live checkpointer by its integer ID.
     *
     *  Called from the SQLite WAL hook (on the writer thread) via the free
     *  function `checkpointerFromId()`. Returns `nullptr` when the entry has
     *  been erased, signalling the hook to deregister itself.
     *
     *  @param id The integer key assigned by `create()`.
     *  @return A `shared_ptr` to the checkpointer, or `nullptr` if not found.
     */
    std::shared_ptr<Checkpointer>
    fromId(std::uintptr_t id)
    {
        std::scoped_lock const l{mutex_};
        auto it = checkpointers_.find(id);
        if (it != checkpointers_.end())
            return it->second;
        return nullptr;
    }

    /** Remove the checkpointer with the given ID from the registry.
     *
     *  Called by `DatabaseCon::~DatabaseCon()` as the first step of teardown,
     *  ensuring that any subsequent WAL hook invocations find nothing and
     *  self-deregister rather than scheduling a job on a dying connection.
     *
     *  @param id The integer key assigned by `create()`.
     */
    void
    erase(std::uintptr_t id)
    {
        std::scoped_lock const lock{mutex_};
        checkpointers_.erase(id);
    }

    /** Create and register a new checkpointer, returning its `shared_ptr`.
     *
     *  Assigns a fresh monotonically-increasing ID, calls `makeCheckpointer()`
     *  (which arms the SQLite WAL hook), and stores the result in the map —
     *  all under the same lock. Storing before returning is required because
     *  the WAL hook is live as soon as `makeCheckpointer()` returns, and the
     *  first hook invocation may arrive before the caller saves the pointer.
     *
     *  @param session The SOCI session whose WAL activity will be checkpointed.
     *  @param jobQueue The job queue on which checkpoint work is dispatched.
     *  @param registry The service registry forwarded to `makeCheckpointer()`.
     *  @return The newly created, already-registered `Checkpointer`.
     */
    std::shared_ptr<Checkpointer>
    create(
        std::shared_ptr<soci::session> const& session,
        JobQueue& jobQueue,
        ServiceRegistry& registry)
    {
        std::scoped_lock const lock{mutex_};
        auto const id = nextId_++;
        auto const r = makeCheckpointer(id, session, jobQueue, registry);
        checkpointers_[id] = r;
        return r;
    }
};

/** Process-wide singleton registry of all live WAL checkpointers.
 *
 *  Shared by every `DatabaseCon` instance in the process. The SQLite WAL hook
 *  callback resolves its integer cookie through this registry so it never
 *  holds a raw pointer to a potentially-destroyed C++ object.
 */
CheckpointersCollection gCheckpointers;

/** Resolve a WAL checkpointer by its integer ID.
 *
 *  This free function is the bridge between SQLite's C callback and the C++
 *  `Checkpointer` object. The WAL hook passes the integer ID (registered as a
 *  `void*`) to this function. If the `DatabaseCon` that owns the checkpointer
 *  has already been destroyed and its entry erased, `nullptr` is returned and
 *  the hook should call `sqlite3_wal_hook(conn, nullptr, nullptr)` to
 *  deregister itself.
 *
 *  @param id The integer ID originally assigned by `CheckpointersCollection::create()`.
 *  @return A `shared_ptr` to the checkpointer, or `nullptr` if it no longer exists.
 */
std::shared_ptr<Checkpointer>
checkpointerFromId(std::uintptr_t id)
{
    return gCheckpointers.fromId(id);
}

/** Destroy the connection, waiting for any in-flight checkpoint job to finish.
 *
 *  Teardown follows a strict sequence to avoid use-after-free and file-lock
 *  races:
 *
 *  1. Erase the checkpointer from `gCheckpointers` — future WAL hook
 *     invocations will see `nullptr` and self-deregister.
 *  2. Release `DatabaseCon`'s own `shared_ptr<Checkpointer>` so only
 *     in-flight `JobQueue` lambdas retain references.
 *  3. Busy-poll the `weak_ptr` use-count (100 ms intervals) until all
 *     lambda captures have been released, i.e., the checkpoint job has
 *     finished executing.
 *
 *  The poll is deliberate — a condvar would require additional plumbing in
 *  `WALCheckpointer`, and database teardown is rare. Without the wait, a
 *  new `DatabaseCon` opened to the same SQLite file immediately after
 *  destruction could fail because the old checkpoint job might still hold
 *  the WAL lock.
 */
DatabaseCon::~DatabaseCon()
{
    if (checkpointer_)
    {
        gCheckpointers.erase(checkpointer_->id());

        std::weak_ptr<Checkpointer> const wk(checkpointer_);
        checkpointer_.reset();

        // Both `checkpointer_` and `gCheckpointers` have released their
        // references. A nonzero use_count means a checkpoint job is still
        // running; wait for it to drain so the WAL lock is not held when
        // the caller opens a new connection to the same file.
        while (wk.use_count() != 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

/** Process-wide SQLite PRAGMA statements applied to every connection.
 *
 *  Set once at startup (e.g., `PRAGMA journal_mode=WAL`) and shared across all
 *  `DatabaseCon` instances that opt in via `Setup::useGlobalPragma`. Null when
 *  no global pragmas have been configured.
 */
std::unique_ptr<std::vector<std::string> const> DatabaseCon::Setup::globalPragma;

/** Arm WAL checkpointing on this connection.
 *
 *  Creates a `WALCheckpointer` via `makeCheckpointer()`, registers it in the
 *  process-wide `gCheckpointers` collection, and stores it as `checkpointer_`.
 *  Registration happens inside `gCheckpointers.create()` — under its mutex —
 *  before the pointer is returned, because the SQLite WAL hook is armed inside
 *  `makeCheckpointer()` and can fire on the next write commit.
 *
 *  Separated from the constructors so that checkpointing is opt-in; constructors
 *  accepting `CheckpointerSetup` call this after the base constructor has opened
 *  and initialised the database.
 *
 *  @param q The job queue on which checkpoint work is dispatched.
 *      Must not be null — passing null is a programming error.
 *  @param registry The service registry forwarded to `makeCheckpointer()`.
 *  @throws std::logic_error if `q` is null.
 */
void
DatabaseCon::setupCheckpointing(JobQueue* q, ServiceRegistry& registry)
{
    if (q == nullptr)
        Throw<std::logic_error>("No JobQueue");
    checkpointer_ = gCheckpointers.create(session_, *q, registry);
}

}  // namespace xrpl
