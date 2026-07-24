#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/nodestore/Backend.h>
#include <xrpl/nodestore/Database.h>
#include <xrpl/nodestore/Scheduler.h>
#include <xrpl/protocol/Protocol.h>

#include <functional>
#include <memory>
#include <string>

namespace xrpl::NodeStore {

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
        std::unique_ptr<NodeStore::Backend>&& newBackend,
        std::function<void(std::string const& writableName, std::string const& archiveName)> const&
            f) = 0;

    /**
     * Marks an online-delete rotation as in progress (or completed).
     *
     * While in flight, a read for ledgers after the inFlight value served by the archive backend is
     * copied forward into the writable backend even for ordinary (duplicate == false) fetches: the
     * archive is about to be deleted, and a node body canonicalized into caches during the rotation
     * window would otherwise survive only in RAM once the archive is dropped.
     */
    virtual void
    setRotationInFlight(LedgerIndex inFlight) = 0;
    virtual LedgerIndex
    getRotationInFlight() const = 0;
};

}  // namespace xrpl::NodeStore
