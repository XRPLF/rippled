#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/nodestore/Backend.h>
#include <xrpl/nodestore/Database.h>
#include <xrpl/nodestore/Scheduler.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace xrpl::node_store {

/* This class has two key-value store Backend objects for persisting SHAMap
 * records. This facilitates online deletion of data. New backends are
 * rotated in. Old ones are rotated out and deleted.
 */

class DatabaseRotating : public Database
{
public:
    DatabaseRotating(
        Scheduler& scheduler,
        int readThreads,
        Section const& config,
        beast::Journal journal)
        : Database(scheduler, readThreads, config, journal)
    {
    }

    /**
     * Rotates the backends.
     *
     * @param newBackend New writable backend
     * @param f A function executed after the rotation outside of lock. The
     * values passed to f will be the new backend database names _after_
     * rotation.
     */
    virtual void
    rotate(
        std::unique_ptr<node_store::Backend>&& newBackend,
        std::function<void(std::string const& writableName, std::string const& archiveName)> const&
            f) = 0;

    /**
     * Marks an online-delete rotation as in progress (or completed).
     *
     * While in flight, a read served by the archive backend is copied
     * forward into the writable backend even for ordinary
     * (duplicate == false) fetches: the archive is about to be deleted,
     * and a node body canonicalized into caches during the rotation
     * window would otherwise survive only in RAM once the archive is
     * dropped.
     */
    virtual void
    setRotationInFlight(bool inFlight) = 0;

    /**
     * Whether an online-delete rotation is in progress right now.
     *
     * A rotation's extra writes only happen inside this window, so a panel
     * reading the copy-forward total needs this to know when to expect it to
     * move. Outside the window a flat total is correct, not a broken signal.
     *
     * @return true between the cache-freshen phase starting and rotate()
     *         completing.
     */
    [[nodiscard]] virtual bool
    isRotationInFlight() const = 0;

    /**
     * Nodes copied forward from the archive backend into the writable one
     * during rotation windows, since this process started.
     *
     * These are writes an ordinary fetch would not have performed: the archive
     * is about to be deleted, so a body it served has to be rewritten to
     * survive. The count therefore scales with how much of the archive is read
     * during a rotation, which is why it appears only on a populated,
     * already-rotated online_delete database.
     *
     * Cumulative for the lifetime of the process, deliberately: the per-rotation
     * tally that `rotate()` logs is reset on every swap, and a counter that goes
     * backwards cannot be rated. A panel takes the rate of this instead.
     *
     * @return Monotonic count of copy-forward writes.
     */
    [[nodiscard]] virtual std::uint64_t
    copyForwardTotal() const = 0;
};

}  // namespace xrpl::node_store
