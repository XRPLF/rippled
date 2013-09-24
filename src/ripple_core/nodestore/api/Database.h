//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_NODESTORE_DATABASE_H_INCLUDED
#define RIPPLE_NODESTORE_DATABASE_H_INCLUDED

namespace NodeStore
{

/** Persistency layer for NodeObject

    A Node is a ledger object which is uniquely identified by a key, which is
    the 256-bit hash of the body of the node. The payload is a variable length
    block of serialized data.

    All ledger data is stored as node objects and as such, needs to be persisted
    between launches. Furthermore, since the set of node objects will in
    general be larger than the amount of available memory, purged node objects
    which are later accessed must be retrieved from the node store.

    @see NodeObject
*/
class Database
{
public:
    /** Construct a node store database.

        The parameters are key value pairs passed to the backend. The
        'type' key must exist, it defines the choice of backend. Most
        backends also require a 'path' field.
        
        Some choices for 'type' are:
            HyperLevelDB, LevelDBFactory, SQLite, KeyvaDB, MDB

        If the fastBackendParameter is omitted or empty, no ephemeral database
        is used. If the scheduler parameter is omited or unspecified, a
        synchronous scheduler is used which performs all tasks immediately on
        the caller's thread.

        @note If the database cannot be opened or created, an exception is thrown.

        @param name A diagnostic label for the database.
        @param scheduler The scheduler to use for performing asynchronous tasks.
        @param backendParameters The parameter string for the persistent backend.
        @param fastBackendParameters [optional] The parameter string for the ephemeral backend.                        

        @return The opened database.
    */
    static Database* New (char const* name,
                          Scheduler& scheduler,
                          Parameters const& backendParameters,
                          Parameters fastBackendParameters = Parameters ());

    /** Destroy the node store.
        All pending operations are completed, pending writes flushed,
        and files closed before this returns.
    */
    virtual ~Database () { }

    /** Retrieve the name associated with this backend.
        This is used for diagnostics and may not reflect the actual path
        or paths used by the underlying backend.
    */
    virtual String getName () const = 0;

    /** Add the specified backend factory to the list of available factories.
        The names of available factories are compared against the "type"
        value in the parameter list on construction. Ownership of the object
        is transferred. The object must be allocated using new.
        @param factory The factory to add.
    */
    static void addFactory (Factory* factory);

    /** Fetch an object.
        If the object is known to be not in the database, isn't found in the
        database during the fetch, or failed to load correctly during the fetch,
        `nullptr` is returned.

        @note This can be called concurrently.
        @param hash The key of the object to retrieve.  
        @return The object, or nullptr if it couldn't be retrieved.
    */
    virtual NodeObject::pointer fetch (uint256 const& hash) = 0;

    /** Store the object.

        The caller's Blob parameter is overwritten.

        @param type The type of object.
        @param ledgerIndex The ledger in which the object appears.
        @param data The payload of the object. The caller's
                    variable is overwritten.
        @param hash The 256-bit hash of the payload data.

        @return `true` if the object was stored?              
    */
    virtual void store (NodeObjectType type,
                        uint32 ledgerIndex,
                        Blob& data,
                        uint256 const& hash) = 0;

    /** Visit every object in the database
        This is usually called during import.

        @note This routine will not be called concurrently with itself
                or other methods.
        @see import
    */
    virtual void visitAll (VisitCallback& callback) = 0;

    /** Import objects from another database. */
    virtual void import (Database& sourceDatabase) = 0;

    /** Retrieve the estimated number of pending write operations.
        This is used for diagnostics.
    */
    virtual int getWriteLoad () = 0;

    // VFALCO TODO Document this.
    virtual float getCacheHitRate () = 0;

    // VFALCO TODO Document this.
    //        TODO Document the parameter meanings.
    virtual void tune (int size, int age) = 0;

    // VFALCO TODO Document this.
    virtual void sweep () = 0;

    /** Add the known Backend factories to the singleton.
    */
    static void addAvailableBackends ();
};

}

#endif
