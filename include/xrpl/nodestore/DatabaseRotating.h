#ifndef XRPL_NODESTORE_DATABASEROTATING_H_INCLUDED
#define XRPL_NODESTORE_DATABASEROTATING_H_INCLUDED

#include <xrpl/nodestore/Database.h>

namespace ripple {
namespace NodeStore {

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

    /** Rotates the backends.

        @param newBackend New writable backend
        @param f A function executed after the rotation outside of lock. The
       values passed to f will be the new backend database names _after_
       rotation.
    */
    virtual void
    rotate(
        std::unique_ptr<NodeStore::Backend>&& newBackend,
        std::function<void(
            std::string const& writableName,
            std::string const& archiveName)> const& f) = 0;
};

}  // namespace NodeStore
}  // namespace ripple

#endif
