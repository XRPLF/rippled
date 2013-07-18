//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_NODESTORE_H_INCLUDED
#define RIPPLE_NODESTORE_H_INCLUDED

/** Persistency layer for NodeObject
*/
class NodeStore : LeakChecked <NodeStore>
{
public:
    enum
    {
        /** This is the largest number of key/value pairs we
            will write during a bulk write.
        */
        // VFALCO TODO Make this a tunable parameter in the key value pairs
        bulkWriteBatchSize = 128

        /** Size of the fixed keys, in bytes.
        */
        ,keyBytes = 32 // 256 bit hash
    };

    /** Interface to inform callers of cetain activities.
    */
    class Hooks
    {
        virtual void onRetrieveBegin () { }
        virtual void onRetrieveEnd () { }
    };

    /** Back end used for the store.

        A Backend implements a persistent key/value storage system.
        Keys sizes are all fixed within the same database.
    */
    class Backend
    {
    public:
        /** Return codes from operations.
        */
        enum Status
        {
            ok,
            notFound,
            dataCorrupt,
            unknown
        };

        Backend ();

        virtual ~Backend () { }

        /** Provides storage for retrieved objects.
        */
        struct GetCallback
        {
            /** Get storage for an object.

                @param sizeInBytes The number of bytes needed to store the value.
                
                @return A pointer to a buffer large enough to hold all the bytes.
            */
            virtual void* getStorageForValue (size_t sizeInBytes) = 0;
        };

        /** Retrieve a single object.

            If the object is not found or an error is encountered, the
            result will indicate the condition.

            @param key A pointer to the key data.
            @param callback The callback used to obtain storage for the value.

            @return The result of the operation.
        */
        virtual Status get (void const* key, GetCallback* callback) { return notFound; }




        /** Store a single object.
        */
        // VFALCO TODO Why should the Backend know or care about NodeObject?
        //             It should just deal with a fixed key and raw data.
        //
        virtual bool store (NodeObject::ref);
        //virtual bool put (void const* key, void const* value, int valueBytes) { return false; }

        /** Retrieve an individual object.
        */
        virtual NodeObject::pointer retrieve (uint256 const &hash) = 0;

        // Visit every object in the database
        // This function will only be called during an import operation
        //
        // VFALCO TODO Replace FUNCTION_TYPE with a beast lift.
        //
        virtual void visitAll (FUNCTION_TYPE <void (NodeObject::pointer)>) = 0;

    private:
        friend class NodeStoreImp;

        // VFALCO TODO Put this bulk writing logic into a separate class.
        //        NOTE Why are these virtual?
        void bulkWrite (Job &);
        void waitWrite ();
        int getWriteLoad ();

    private:
        virtual std::string getDataBaseName() = 0;

        // Store a group of objects
        // This function will only be called from a single thread
        // VFALCO NOTE It looks like NodeStore throws this into the job queue?
        virtual bool bulkStore (const std::vector< NodeObject::pointer >&) = 0;

    protected:
        // VFALCO TODO Put this bulk writing logic into a separate class.
        boost::mutex mWriteMutex;
        boost::condition_variable mWriteCondition;
        int mWriteGeneration;
        int mWriteLoad;
        bool mWritePending;
        std::vector <boost::shared_ptr <NodeObject> > mWriteSet;
    };

public:
    // Helper functions for the backend
    class BackendHelper
    {
    public:
    };

public:
    /** Factory to produce backends.
    */
    class BackendFactory
    {
    public:
        virtual ~BackendFactory () { }

        /** Retrieve the name of this factory.
        */
        virtual String getName () const = 0;

        /** Create an instance of this factory's backend.
            
            @param keyBytes The fixed number of bytes per key.
            @param keyValues A set of key/value configuration pairs.

            @return A pointer to the Backend object.
        */
        virtual Backend* createInstance (size_t keyBytes, StringPairArray const& keyValues) = 0;
    };

public:
    /** Construct a node store.

        parameters has the format:

        <key>=<value>['|'<key>=<value>]

        The key "type" must exist, it defines the backend. For example
            "type=LevelDB|path=/mnt/ephemeral"
    */
    // VFALCO NOTE Is cacheSize in bytes? objects? KB?
    //             Is cacheAge in minutes? seconds?
    //             These should be in the parameters.
    //
    static NodeStore* New (String backendParameters,
                           String fastBackendParameters,
                           int cacheSize,
                           int cacheAge);

    /** Add the specified backend factory to the list of available factories.

        The names of available factories are compared against the "type"
        value in the parameter list on construction.
    */
    static void addBackendFactory (BackendFactory& factory);

    // VFALCO TODO Document this.
    virtual float getCacheHitRate () = 0;

    // VFALCO TODO Document this.
    virtual bool store (NodeObjectType type, uint32 index, Blob const& data,
                uint256 const& hash) = 0;

    // VFALCO TODO Document this.
    //        TODO Replace uint256 with void*
    //
    virtual NodeObject::pointer retrieve (uint256 const& hash) = 0;

    // VFALCO TODO Document this.
    //        TODO Document the parameter meanings.
    virtual void tune (int size, int age) = 0;

    // VFALCO TODO Document this.
    virtual void sweep () = 0;

    // VFALCO TODO Document this.
    //             What are the units of the return value?
    virtual int getWriteLoad () = 0;

    // VFALCO TODO Document this.
    //        NOTE What's the return value?
    virtual int import (String sourceBackendParameters) = 0;
};

#endif
