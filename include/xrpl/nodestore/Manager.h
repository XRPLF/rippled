#ifndef XRPL_NODESTORE_MANAGER_H_INCLUDED
#define XRPL_NODESTORE_MANAGER_H_INCLUDED

#include <xrpl/nodestore/DatabaseRotating.h>
#include <xrpl/nodestore/Factory.h>

namespace ripple {

namespace NodeStore {

/** Singleton for managing NodeStore factories and back ends. */
class Manager
{
public:
    virtual ~Manager() = default;
    Manager() = default;
    Manager(Manager const&) = delete;
    Manager&
    operator=(Manager const&) = delete;

    /** Returns the instance of the manager singleton. */
    static Manager&
    instance();

    /** Add a factory. */
    virtual void
    insert(Factory& factory) = 0;

    /** Remove a factory. */
    virtual void
    erase(Factory& factory) = 0;

    /** Return a pointer to the matching factory if it exists.
        @param  name The name to match, performed case-insensitive.
        @return `nullptr` if a match was not found.
    */
    virtual Factory*
    find(std::string const& name) = 0;

    /** Create a backend. */
    virtual std::unique_ptr<Backend>
    make_Backend(
        Section const& parameters,
        std::size_t burstSize,
        Scheduler& scheduler,
        beast::Journal journal) = 0;

    /** Construct a NodeStore database.

        The parameters are key value pairs passed to the backend. The
        'type' key must exist, it defines the choice of backend. Most
        backends also require a 'path' field.

        Some choices for 'type' are:
            HyperLevelDB, LevelDBFactory, SQLite, MDB

        If the fastBackendParameter is omitted or empty, no ephemeral database
        is used. If the scheduler parameter is omited or unspecified, a
        synchronous scheduler is used which performs all tasks immediately on
        the caller's thread.

        @note If the database cannot be opened or created, an exception is
       thrown.

        @param name A diagnostic label for the database.
        @param burstSize Backend burst size in bytes.
        @param scheduler The scheduler to use for performing asynchronous tasks.
        @param readThreads The number of async read threads to create
        @param backendParameters The parameter string for the persistent
       backend.
        @param fastBackendParameters [optional] The parameter string for the
       ephemeral backend.

        @return The opened database.
    */
    virtual std::unique_ptr<Database>
    make_Database(
        std::size_t burstSize,
        Scheduler& scheduler,
        int readThreads,
        Section const& backendParameters,
        beast::Journal journal) = 0;
};

}  // namespace NodeStore
}  // namespace ripple

#endif
