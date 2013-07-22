//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_NODESTORE_H_INCLUDED
#define RIPPLE_NODESTORE_H_INCLUDED

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
class NodeStore
{
public:
    enum
    {
        // This is only used to pre-allocate the array for
        // batch objects and does not affect the amount written.
        //
        batchWritePreallocationSize = 128
    };

    typedef std::vector <NodeObject::Ptr> Batch;

    typedef StringPairArray Parameters;

    //--------------------------------------------------------------------------

    /** Parsed key/value blob into NodeObject components.

        This will extract the information required to construct a NodeObject. It
        also does consistency checking and returns the result, so it is possible
        to determine if the data is corrupted without throwing an exception. Not
        all forms of corruption are detected so further analysis will be needed
        to eliminate false negatives.

        @note This defines the database format of a NodeObject!
    */
    class DecodedBlob
    {
    public:
        /** Construct the decoded blob from raw data. */
        DecodedBlob (void const* key, void const* value, int valueBytes);

        /** Determine if the decoding was successful. */
        bool wasOk () const noexcept { return m_success; }

        /** Create a NodeObject from this data. */
        NodeObject::Ptr createObject ();

    private:
        bool m_success;

        void const* m_key;
        LedgerIndex m_ledgerIndex;
        NodeObjectType m_objectType;
        unsigned char const* m_objectData;
        int m_dataBytes;
    };

    //--------------------------------------------------------------------------

    /** Utility for producing flattened node objects.

        These get recycled to prevent many small allocations.

        @note This defines the database format of a NodeObject!
    */
    struct EncodedBlob
    {
        typedef RecycledObjectPool <EncodedBlob> Pool;

        void prepare (NodeObject::Ptr const& object);

        void const* getKey () const noexcept { return m_key; }

        size_t getSize () const noexcept { return m_size; }

        void const* getData () const noexcept { return m_data.getData (); }

    private:
        void const* m_key;
        MemoryBlock m_data;
        size_t m_size;
    };

    //--------------------------------------------------------------------------

    /** Provides optional asynchronous scheduling for backends.

        For improved performance, a backend has the option of performing writes
        in batches. These writes can be scheduled using the provided scheduler
        object.
    */
    class Scheduler
    {
    public:
        /** Derived classes perform scheduled tasks. */
        struct Task
        {
            virtual ~Task () { }

            /** Performs the task.

                The call may take place on a foreign thread.
            */
            virtual void performScheduledTask () = 0;
        };

        /** Schedules a task.

            Depending on the implementation, this could happen
            immediately or get deferred.
        */
        virtual void scheduleTask (Task* task) = 0;
    };

    //--------------------------------------------------------------------------

    /** Helps with batch writing.

        The batch writes are performed with a scheduled task. Use of the
        class it not required. A backend can implement its own write batching,
        or skip write batching if doing so yields a performance benefit.

        @see Scheduler
    */
    // VFALCO NOTE I'm not entirely happy having placed this here,
    //             because whoever needs to use NodeStore certainly doesn't
    //             need to see the implementation details of BatchWriter.
    //
    class BatchWriter : private Scheduler::Task
    {
    public:
        /** This callback does the actual writing. */
        struct Callback
        {
            virtual void writeBatch (Batch const& batch) = 0;
        };

        /** Create a batch writer. */
        BatchWriter (Callback& callback, Scheduler& scheduler);

        /** Destroy a batch writer.

            Anything pending in the batch is written out before this returns.
        */
        ~BatchWriter ();

        /** Store the object.

            This will add to the batch and initiate a scheduled task to
            write the batch out.
        */
        void store (NodeObject::Ptr const& object);

        /** Get an estimate of the amount of writing I/O pending. */
        int getWriteLoad ();

    private:
        void performScheduledTask ();
        void writeBatch ();
        void waitForWriting ();

    private:
        typedef boost::recursive_mutex LockType;
        typedef boost::condition_variable_any CondvarType;

        Callback& m_callback;
        Scheduler& m_scheduler;
        LockType mWriteMutex;
        CondvarType mWriteCondition;
        int mWriteGeneration;
        int mWriteLoad;
        bool mWritePending;
        Batch mWriteSet;
    };

    //--------------------------------------------------------------------------

    /** A backend used for the store.

        The NodeStore uses a swappable backend so that other database systems
        can be tried. Different databases may offer various features such
        as improved performance, fault tolerant or distributed storage, or
        all in-memory operation.

        A given instance of a backend is fixed to a particular key size.
    */
    class Backend
    {
    public:
        /** Return codes from operations. */
        enum Status
        {
            ok,
            notFound,
            dataCorrupt,
            unknown
        };

        /** Destroy the backend.

            All open files are closed and flushed. If there are batched writes
            or other tasks scheduled, they will be completed before this call
            returns.
        */
        virtual ~Backend () { }

        /** Get the human-readable name of this backend.

            This is used for diagnostic output.
        */
        virtual std::string getName() = 0;

        /** Fetch a single object.

            If the object is not found or an error is encountered, the
            result will indicate the condition.

            @note This will be called concurrently.

            @param key A pointer to the key data.
            @param pObject [out] The created object if successful.

            @return The result of the operation.
        */
        virtual Status fetch (void const* key, NodeObject::Ptr* pObject) = 0;

        /** Store a single object.

            Depending on the implementation this may happen immediately
            or deferred using a scheduled task.

            @note This will be called concurrently.

            @param object The object to store.
        */
        virtual void store (NodeObject::Ptr const& object) = 0;

        /** Store a group of objects.
            
            @note This function will not be called concurrently with
                  itself or @ref store.
        */
        virtual void storeBatch (Batch const& batch) = 0;

        /** Callback for iterating through objects.

            @see visitAll
        */
        struct VisitCallback
        {
            virtual void visitObject (NodeObject::Ptr const& object) = 0;
        };

        /** Visit every object in the database
            
            This is usually called during import.

            @note This routine will not be called concurrently with itself
                  or other methods.

            @see import
        */
        virtual void visitAll (VisitCallback& callback) = 0;

        /** Estimate the number of write operations pending. */
        virtual int getWriteLoad () = 0;
    };

    //--------------------------------------------------------------------------

    /** Factory to produce backends.
    */
    class BackendFactory
    {
    public:
        virtual ~BackendFactory () { }

        /** Retrieve the name of this factory. */
        virtual String getName () const = 0;

        /** Create an instance of this factory's backend.
            
            @param keyBytes The fixed number of bytes per key.
            @param keyValues A set of key/value configuration pairs.
            @param scheduler The scheduler to use for running tasks.

            @return A pointer to the Backend object.
        */
        virtual Backend* createInstance (size_t keyBytes,
                                         Parameters const& parameters,
                                         Scheduler& scheduler) = 0;
    };

    //--------------------------------------------------------------------------

    /** Create a Parameters from a String.

        Parameter strings have the format:

        <key>=<value>['|'<key>=<value>]

        The key "type" must exist, it defines the choice of backend.
        For example
            `type=LevelDB|path=/mnt/ephemeral`

        This is a convenience function for unit tests.
    */
    static Parameters parseDelimitedKeyValueString (String s, beast_wchar delimiter='|');

    /** Construct a node store.

        Parameter strings have the format:

        <key>=<value>['|'<key>=<value>]

        The key "type" must exist, it defines the choice of backend.
        For example
            `type=LevelDB|path=/mnt/ephemeral`

        @param backendParameters The parameter string for the persistent backend.
        @param fastBackendParameters The parameter string for the ephemeral backend.
        @param cacheSize ?
        @param cacheAge ?
        @param scheduler The scheduler to use for performing asynchronous tasks.

        @return A pointer to the created object.
    */
    static NodeStore* New (Parameters const& backendParameters,
                           Parameters const& fastBackendParameters,
                           Scheduler& scheduler);

    /** Construct a node store from a pipe delimited parameter string.

        This is used for unit tests.
    */
    static NodeStore* New (String const& backendParameters,
                           String const& fastBackendParameters,
                           Scheduler& scheduler);

    /** Destroy the node store.

        All pending operations are completed, pending writes flushed,
        and files closed before this returns.
    */
    virtual ~NodeStore () { }

    /** Retrieve the name associated with this backend.

        This is used for diagnostics and may not reflect the actual path
        or paths used by the underlying backend.
    */
    virtual String getName () const = 0;

    /** Add the specified backend factory to the list of available factories.

        The names of available factories are compared against the "type"
        value in the parameter list on construction.

        @param factory The factory to add.
    */
    static void addBackendFactory (BackendFactory& factory);

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

    /** Import objects from another database.

        The other NodeStore database is constructed using the specified
        backend parameters.
    */
    virtual void import (Parameters const& sourceBackendParameters) = 0;

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

};

#endif
