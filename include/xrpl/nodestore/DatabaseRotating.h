/** @file
 *  Abstract interface extending `Database` with a two-backend rotation
 *  operation for online ledger history deletion.
 */

#pragma once

#include <xrpl/nodestore/Database.h>

namespace xrpl::NodeStore {

/** Abstract seam for the two-backend rotation scheme that enables online
 *  deletion of ledger history without taking the node offline.
 *
 *  The concrete subclass `DatabaseRotatingImp` maintains two physical
 *  `Backend` objects: a *writable* backend that receives all current writes
 *  and an *archive* backend holding older data. When enough new history has
 *  accumulated, `SHAMapStoreImp` calls `rotate()` to atomically promote the
 *  writable backend to the archive role, install a fresh writable backend,
 *  and discard the old archive — all without interrupting read or write
 *  traffic.
 *
 *  `DatabaseRotating` carries no state; it extends `Database` solely with
 *  the `rotate()` pure-virtual method. Components that drive rotation
 *  (currently only `SHAMapStoreImp`) hold a `DatabaseRotating*` pointer,
 *  keeping the rotation mechanism decoupled from storage format.
 *
 *  @see DatabaseRotatingImp, Database, SHAMapStoreImp
 */
class DatabaseRotating : public Database
{
public:
    /** Construct the rotating database and start the async read thread pool.
     *
     *  Delegates entirely to `Database(scheduler, readThreads, config,
     *  journal)`. The two physical backends are supplied when constructing
     *  the concrete `DatabaseRotatingImp` subclass.
     *
     *  @param scheduler   Task scheduler for async I/O dispatch and telemetry;
     *      must outlive this object.
     *  @param readThreads Number of prefetch worker threads to create.
     *  @param config      `[node_db]` config section forwarded to `Database`.
     *  @param journal     Logging sink.
     */
    DatabaseRotating(
        Scheduler& scheduler,
        int readThreads,
        Section const& config,
        beast::Journal journal)
        : Database(scheduler, readThreads, config, journal)
    {
    }

    /** Atomically replace the current writable backend with @p newBackend.
     *
     *  Performs a three-step pointer swap under the implementation's internal
     *  mutex:
     *  1. Mark the current archive backend for directory deletion and stash it
     *     in a local `shared_ptr` to keep it alive past the lock release.
     *  2. Demote the current writable backend to the archive role.
     *  3. Install @p newBackend as the new writable backend.
     *
     *  After releasing the lock, @p f is called with the new backend names.
     *  Only after @p f returns does the old archive `shared_ptr` go out of
     *  scope and its on-disk files are removed. This sequencing is
     *  **crash-safe**: if the process dies between the pointer swap and @p f
     *  completing, both directory sets still exist on disk and can be
     *  recovered from the SQL state database on restart.
     *
     *  Concurrent fetches already in flight hold `shared_ptr` references to
     *  the old backends; reference counting keeps those backends alive until
     *  all in-flight I/O completes.
     *
     *  @param newBackend  Freshly created, opened backend that becomes the new
     *      writable store; ownership is transferred.
     *  @param f  Callback invoked after the in-memory swap completes but
     *      before the old archive is deleted, and outside the implementation
     *      mutex. Receives two names post-rotation: @p writableName is the
     *      name of @p newBackend, and @p archiveName is the name of the former
     *      writable backend now serving as the archive. `SHAMapStoreImp` uses
     *      @p f to durably persist the new backend names and `lastRotated`
     *      ledger sequence to a SQL state database, creating an atomic
     *      checkpoint for crash recovery.
     */
    virtual void
    rotate(
        std::unique_ptr<NodeStore::Backend>&& newBackend,
        std::function<void(std::string const& writableName, std::string const& archiveName)> const&
            f) = 0;
};

}  // namespace xrpl::NodeStore
