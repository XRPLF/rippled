/** @file
 *  SOCI/SQLite adapter: session lifecycle, WAL checkpointing, memory
 *  diagnostics, and blob conversion helpers.
 *
 *  This translation unit is wrapped in a Clang `-Wdeprecated` suppression
 *  pragma because SOCI's own headers use deprecated constructs.  The pragma
 *  scope is kept as narrow as possible (this file only).
 *
 *  Only `"sqlite"` is a valid backend; any other value in the `[sqdb]`
 *  config section causes an immediate `std::runtime_error`.
 */
#include <xrpl/basics/BasicConfig.h>
#include <xrpl/basics/Log.h>
#include <xrpl/core/Job.h>
#include <xrpl/core/JobQueue.h>
#include <xrpl/core/ServiceRegistry.h>

#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>

#include <soci/blob.h>

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated"
#endif

#include <xrpl/basics/ByteUtilities.h>
#include <xrpl/basics/contract.h>
#include <xrpl/rdb/DatabaseCon.h>
#include <xrpl/rdb/SociDB.h>

#include <soci/sqlite3/soci-sqlite3.h>  // IWYU pragma: keep

#include <memory>

namespace xrpl {

/** WAL frame threshold that triggers an off-thread checkpoint.
 *
 *  Mirrors SQLite's own auto-checkpoint default so behaviour is consistent
 *  whether or not the WAL hook fires first.  Tune here to change the
 *  trade-off between WAL file growth and checkpoint frequency.
 */
static auto gCheckpointPageCount = 1000;

namespace detail {

/** Build the SQLite connection string for a named database.
 *
 *  Appends @p name + @p ext to @p dir to produce the full filesystem path
 *  that SOCI uses as its connection string.
 *
 *  @param name  Database filename stem (e.g. `"ledger"`).
 *  @param dir   Directory that holds the database files.
 *  @param ext   File extension, either `".db"` or `".sqlite"`.
 *  @return The absolute file path as a string.
 *  @throws std::runtime_error if @p name is empty.
 */
std::string
getSociSqliteInit(std::string const& name, std::string const& dir, std::string const& ext)
{
    if (name.empty())
    {
        Throw<std::runtime_error>(
            "Sqlite databases must specify a dir and a name. Name: " + name + " Dir: " + dir);
    }
    boost::filesystem::path file(dir);
    if (is_directory(file))
        file /= name + ext;
    return file.string();
}

/** Resolve the SOCI connection string from the node configuration.
 *
 *  Reads `[sqdb] backend` (defaulting to `"sqlite"`).  Any value other than
 *  `"sqlite"` throws immediately — no other backends are supported.  The
 *  `validators` and `peerfinder` databases receive the `.sqlite` extension;
 *  all other databases receive `.db`.  This extension asymmetry is a
 *  historical artifact.
 *
 *  @param config  Node configuration supplying `[sqdb]` and `database_path`.
 *  @param dbName  Logical database name (e.g. `"ledger"`, `"peerfinder"`).
 *  @return Filesystem path suitable for passing to `soci::session::open`.
 *  @throws std::runtime_error if the backend is not `"sqlite"`.
 */
std::string
getSociInit(BasicConfig const& config, std::string const& dbName)
{
    auto const& section = config.section("sqdb");
    auto const backendName = get(section, "backend", "sqlite");

    if (backendName != "sqlite")
        Throw<std::runtime_error>("Unsupported soci backend: " + backendName);

    auto const path = config.legacy("database_path");
    auto const ext = dbName == "validators" || dbName == "peerfinder" ? ".sqlite" : ".db";
    return detail::getSociSqliteInit(dbName, path, ext);
}

}  // namespace detail

DBConfig::DBConfig(std::string dbPath) : connectionString_(std::move(dbPath))
{
}

DBConfig::DBConfig(BasicConfig const& config, std::string const& dbName)
    : DBConfig(detail::getSociInit(config, dbName))
{
}

std::string
DBConfig::connectionString() const
{
    return connectionString_;
}

void
DBConfig::open(soci::session& s) const
{
    s.open(soci::sqlite3, connectionString());
}

void
open(soci::session& s, BasicConfig const& config, std::string const& dbName)
{
    DBConfig(config, dbName).open(s);
}

void
open(soci::session& s, std::string const& beName, std::string const& connectionString)
{
    if (beName == "sqlite")
    {
        s.open(soci::sqlite3, connectionString);
    }
    else
    {
        Throw<std::runtime_error>("Unsupported soci backend: " + beName);
    }
}

/** Recover the raw SQLite connection pointer from a SOCI session.
 *
 *  This is the only intentional breach of the SOCI abstraction in this file.
 *  A `dynamic_cast` to `soci::sqlite3_session_backend` extracts the native
 *  `sqlite3*` handle required by WAL and memory-statistics APIs that SOCI
 *  does not expose.
 *
 *  @param s  An open SOCI session backed by SQLite.
 *  @return   The raw `sqlite3*` connection pointer.
 *  @throws std::logic_error if @p s is not a SQLite session or the connection
 *      pointer is null.
 */
static sqlite_api::sqlite3*
getConnection(soci::session& s)
{
    sqlite_api::sqlite3* result = nullptr;  // NOLINT(misc-const-correctness)
    auto be = s.get_backend();
    if (auto b = dynamic_cast<soci::sqlite3_session_backend*>(be))
        result = b->conn_;

    if (result == nullptr)
        Throw<std::logic_error>("Didn't get a database connection.");

    return result;
}

/** Return total SQLite memory in use across the entire process, in kilobytes.
 *
 *  Delegates to `sqlite3_memory_used()`, which is a process-global counter
 *  independent of any individual connection.
 *
 *  @param s  Any open SOCI/SQLite session (used only to validate the backend).
 *  @return   Process-wide SQLite heap usage in kilobytes.
 *  @throws std::logic_error if @p s is not a SQLite session.
 */
std::uint32_t
getKBUsedAll(soci::session& s)
{
    if (getConnection(s) == nullptr)
        Throw<std::logic_error>("No connection found.");
    return static_cast<size_t>(sqlite_api::sqlite3_memory_used() / kilobytes(1));
}

/** Return the page-cache memory used by a specific database connection, in kilobytes.
 *
 *  Calls `sqlite3_db_status(..., SQLITE_DBSTATUS_CACHE_USED, ...)` on the
 *  connection underlying @p s.
 *
 *  @param s  An open SOCI/SQLite session.
 *  @return   Page-cache footprint of @p s in kilobytes.
 *  @throws std::logic_error if @p s is not a SQLite session.
 *  @note This function will need to be extended if additional SOCI backends
 *      are ever supported.
 */
std::uint32_t
getKBUsedDB(soci::session& s)
{
    if (auto conn = getConnection(s))
    {
        int cur = 0, hiw = 0;
        sqlite_api::sqlite3_db_status(conn, SQLITE_DBSTATUS_CACHE_USED, &cur, &hiw, 0);
        return cur / kilobytes(1);
    }
    Throw<std::logic_error>("");
    return 0;  // Silence compiler warning.
}

/** Copy a SOCI blob into a byte vector.
 *
 *  Resizes @p to to match the blob length and reads all bytes via SOCI's
 *  `blob::read` API, centralising the `reinterpret_cast` from `char*` to
 *  `uint8_t*`.
 *
 *  @param from  Source SOCI blob.
 *  @param to    Destination vector; resized and overwritten on return.
 */
void
convert(soci::blob& from, std::vector<std::uint8_t>& to)
{
    to.resize(from.get_len());
    if (to.empty())
        return;
    from.read(0, reinterpret_cast<char*>(&to[0]), from.get_len());
}

/** Copy a SOCI blob into a string.
 *
 *  Delegates to the vector overload then constructs the string from that
 *  buffer.
 *
 *  @param from  Source SOCI blob.
 *  @param to    Destination string; overwritten on return.
 */
void
convert(soci::blob& from, std::string& to)
{
    std::vector<std::uint8_t> tmp;
    convert(from, tmp);
    to.assign(tmp.begin(), tmp.end());
}

/** Write a byte vector into a SOCI blob.
 *
 *  Uses `blob::write` for non-empty input and `blob::trim(0)` for an empty
 *  vector.  Calling `blob::write` with a null pointer on an empty vector is
 *  undefined behaviour in SOCI, hence the explicit branch.
 *
 *  @param from  Source byte vector.
 *  @param to    Destination SOCI blob; overwritten on return.
 */
void
convert(std::vector<std::uint8_t> const& from, soci::blob& to)
{
    if (!from.empty())
    {
        to.write(0, reinterpret_cast<char const*>(&from[0]), from.size());
    }
    else
    {
        to.trim(0);
    }
}

/** Write a string into a SOCI blob.
 *
 *  Mirrors the vector overload: `blob::write` for non-empty strings,
 *  `blob::trim(0)` for empty strings.
 *
 *  @param from  Source string.
 *  @param to    Destination SOCI blob; overwritten on return.
 */
void
convert(std::string const& from, soci::blob& to)
{
    if (!from.empty())
    {
        to.write(0, from.data(), from.size());
    }
    else
    {
        to.trim(0);
    }
}

namespace {

/** SQLite WAL checkpointer that offloads checkpoint work to the XRPL JobQueue.
 *
 *  Installs a `sqlite3_wal_hook` that fires after every WAL write.  When the
 *  accumulated WAL size reaches `gCheckpointPageCount` frames the hook enqueues
 *  a `jtWAL` job rather than running `sqlite3_wal_checkpoint_v2` on the
 *  writer's thread.  This prevents database writers from stalling on I/O.
 *
 *  The hook cookie is the checkpointer's integer `id_`, not a raw `this`
 *  pointer.  `checkpointerFromId()` performs the lookup against the
 *  process-wide `CheckpointersCollection`; if lookup returns null (because the
 *  owning `DatabaseCon` has been destroyed) the hook unregisters itself and
 *  returns without touching freed memory.
 *
 *  At most one checkpoint job is in-flight at any time, enforced by the
 *  `running_` flag under `mutex_`.  The job lambda captures a `weak_ptr` to
 *  this object so a destroyed `DatabaseCon` causes it to exit silently.
 *
 *  @note SQLite already auto-checkpoints at 1000 pages; the primary benefit of
 *      this class is routing that work onto the XRPL job queue rather than the
 *      calling thread.
 *  @see DatabaseCon, checkpointerFromId, makeCheckpointer
 */
class WALCheckpointer : public Checkpointer
{
public:
    /** Construct and arm the WAL hook on the underlying SQLite connection.
     *
     *  Registers `sqliteWALHook` via `sqlite3_wal_hook` using @p id cast to
     *  `void*` as the cookie.  The hook fires immediately upon the next WAL
     *  write, so the checkpointer must already be registered in
     *  `CheckpointersCollection` before this constructor returns.
     *
     *  @param id        Unique integer identifier for this checkpointer; must
     *      match the ID stored in `CheckpointersCollection`.
     *  @param session   Weak reference to the owning session; the checkpointer
     *      must not outlive the object that holds the corresponding
     *      `shared_ptr`.
     *  @param q         Job queue on which checkpoint jobs are scheduled.
     *  @param registry  Service registry used to obtain the WALCheckpointer
     *      journal.
     */
    WALCheckpointer(
        std::uintptr_t id,
        std::weak_ptr<soci::session> session,
        JobQueue& q,
        ServiceRegistry& registry)
        : id_(id)
        , session_(std::move(session))
        , jobQueue_(q)
        , j_(registry.getJournal("WALCheckpointer"))
    {
        if (auto [conn, keepAlive] = getConnection(); conn)
        {
            (void)keepAlive;
            sqlite_api::sqlite3_wal_hook(conn, &sqliteWALHook, reinterpret_cast<void*>(id_));
        }
    }

    /** Attempt to lock the session and retrieve the underlying connection.
     *
     *  Returns `{nullptr, {}}` if the session `weak_ptr` has expired (i.e.,
     *  the owning `DatabaseCon` has been destroyed).  The caller must keep the
     *  returned `shared_ptr` alive for as long as it uses the `sqlite3*`.
     *
     *  @return A pair of the raw connection pointer (or null) and the
     *      locked session `shared_ptr` that keeps the connection valid.
     */
    std::pair<sqlite_api::sqlite3*, std::shared_ptr<soci::session>>
    getConnection() const
    {
        if (auto p = session_.lock())
        {
            return {xrpl::getConnection(*p), p};
        }
        return {nullptr, std::shared_ptr<soci::session>{}};
    }

    std::uintptr_t
    id() const override
    {
        return id_;
    }

    ~WALCheckpointer() override = default;

    /** Enqueue a checkpoint job on the `JobQueue`, if none is already running.
     *
     *  Guards against concurrent submissions with `running_` under `mutex_`.
     *  If the `JobQueue` rejects the job (e.g., during shutdown) `running_`
     *  is reset so the next WAL hook invocation can retry.  The job lambda
     *  holds only a `weak_ptr` to this checkpointer so a destroyed
     *  `DatabaseCon` causes it to exit without touching freed memory.
     */
    void
    schedule() override
    {
        {
            std::scoped_lock const lock(mutex_);
            if (running_)
                return;
            running_ = true;
        }

        // If the Job is not added to the JobQueue then we're not running_.
        if (!jobQueue_.addJob(
                JtWal,
                "WAL",
                // If the owning DatabaseCon is destroyed, no need to checkpoint
                // or keep the checkpointer alive so use a weak_ptr to this.
                // There is a separate check in `checkpoint` for a valid
                // connection in the rare case when the DatabaseCon is destroyed
                // after locking this weak_ptr
                [wp = std::weak_ptr<Checkpointer>{shared_from_this()}]() {
                    if (auto self = wp.lock())
                        self->checkpoint();
                }))
        {
            std::scoped_lock const lock(mutex_);
            running_ = false;
        }
    }

    /** Perform a passive WAL checkpoint and reset the `running_` flag.
     *
     *  Calls `sqlite3_wal_checkpoint_v2` with `SQLITE_CHECKPOINT_PASSIVE`,
     *  which checkpoints only WAL frames that are not currently held by any
     *  reader.  `SQLITE_LOCKED` is expected under reader contention and is
     *  logged at trace level; any other non-OK result is logged as a warning.
     *  `running_` is always reset under `mutex_` before returning so future
     *  hook invocations can schedule new jobs.
     *
     *  Exits immediately if the session `weak_ptr` has expired.
     */
    void
    checkpoint() override
    {
        auto [conn, keepAlive] = getConnection();
        (void)keepAlive;
        if (conn == nullptr)
            return;

        int log = 0, ckpt = 0;
        int const ret = sqlite_api::sqlite3_wal_checkpoint_v2(
            conn, nullptr, SQLITE_CHECKPOINT_PASSIVE, &log, &ckpt);

        auto fname = sqlite_api::sqlite3_db_filename(conn, "main");
        if (ret != SQLITE_OK)
        {
            auto jm = (ret == SQLITE_LOCKED) ? j_.trace() : j_.warn();
            JLOG(jm) << "WAL(" << fname << "): error " << ret;
        }
        else
        {
            JLOG(j_.trace()) << "WAL(" << fname << "): frames=" << log << ", written=" << ckpt;
        }

        std::scoped_lock const lock(mutex_);
        running_ = false;
    }

protected:
    /** Unique integer identifier used as the WAL hook cookie. */
    std::uintptr_t const id_;

    /** Weak reference to the owning session.
     *
     *  `DatabaseCon` holds the `shared_ptr`; this weak reference lets
     *  in-flight checkpoint jobs detect that the connection has been torn
     *  down without preventing the destructor from completing.
     */
    std::weak_ptr<soci::session> session_;

    /** Guards `running_` against concurrent WAL hook and job-completion access. */
    std::mutex mutex_;

    /** Job queue on which checkpoint jobs are submitted. */
    JobQueue& jobQueue_;

    /** True while a checkpoint job is enqueued or executing. */
    bool running_ = false;

    /** Journal for WAL checkpoint trace and warning messages. */
    beast::Journal const j_;

    /** SQLite WAL hook callback.
     *
     *  SQLite invokes this function on the writing thread after each WAL
     *  write.  The cookie @p cpId is the checkpointer's integer ID (not a
     *  pointer); `checkpointerFromId` resolves it to the live
     *  `shared_ptr<Checkpointer>`.  If lookup fails the hook unregisters
     *  itself to prevent future calls after the `DatabaseCon` is gone.
     *
     *  @param cpId    Checkpointer ID cast to `void*` by the constructor.
     *  @param conn    SQLite connection that triggered the hook.
     *  @param dbName  Name of the attached database (unused).
     *  @param walSize Current WAL size in pages.
     *  @return Always `SQLITE_OK`.
     */
    static int
    sqliteWALHook(void* cpId, sqlite_api::sqlite3* conn, char const* dbName, int walSize)
    {
        if (walSize >= gCheckpointPageCount)
        {
            if (auto checkpointer = checkpointerFromId(reinterpret_cast<std::uintptr_t>(cpId)))
            {
                checkpointer->schedule();
            }
            else
            {
                sqlite_api::sqlite3_wal_hook(conn, nullptr, nullptr);
            }
        }
        return SQLITE_OK;
    }
};

}  // namespace

/** Create and arm a WAL checkpointer for a SQLite session.
 *
 *  Constructs a `WALCheckpointer` and registers `sqlite3_wal_hook` on the
 *  underlying connection.  The caller (typically `DatabaseCon::setupCheckpointing`)
 *  must insert the returned pointer into `CheckpointersCollection` under @p id
 *  **before** this call returns, because WAL writes can trigger the hook
 *  immediately.
 *
 *  @param id       Integer ID that will be passed as the WAL hook cookie and
 *      used for lookup in `checkpointerFromId`.
 *  @param session  Weak reference to the session to checkpoint; must not
 *      outlive the `shared_ptr` held by the owning `DatabaseCon`.
 *  @param queue    Job queue on which checkpoint jobs will be scheduled.
 *  @param registry Service registry used to obtain the WALCheckpointer journal.
 *  @return A `shared_ptr` to the new `Checkpointer`.
 */
std::shared_ptr<Checkpointer>
makeCheckpointer(
    std::uintptr_t id,
    std::weak_ptr<soci::session> session,
    JobQueue& queue,
    ServiceRegistry& registry)
{
    return std::make_shared<WALCheckpointer>(id, std::move(session), queue, registry);
}

}  // namespace xrpl

#if defined(__clang__)
#pragma clang diagnostic pop
#endif
