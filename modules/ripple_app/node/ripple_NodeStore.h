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
    };

    /** Interface to inform callers of cetain activities.
    */
    class Hooks
    {
        virtual void on
    };

    /** Back end used for the store.
    */
    class Backend
    {
    public:
        Backend ();

        virtual ~Backend () { }

        /** Store a single object.
        */
        // VFALCO TODO Why should the Backend know or care about NodeObject?
        //             It should just deal with a fixed key and raw data.
        //
        virtual bool store (NodeObject::ref);

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
        friend class NodeStore;

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
        */
        virtual Backend* createInstance (StringPairArray const& keyValues) = 0;
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
    NodeStore (String backendParameters,
               String fastBackendParameters,
               int cacheSize,
               int cacheAge);

    /** Add the specified backend factory to the list of available factories.

        The names of available factories are compared against the "type"
        value in the parameter list on construction.
    */
    static void addBackendFactory (BackendFactory& factory);

    // VFALCO TODO Document this.
    float getCacheHitRate ();

    // VFALCO TODO Document this.
    bool store (NodeObjectType type, uint32 index, Blob const& data,
                uint256 const& hash);

    // VFALCO TODO Document this.
    NodeObject::pointer retrieve (uint256 const& hash);

    // VFALCO TODO Document this.
    void waitWrite ();

    // VFALCO TODO Document this.
    //        TODO Document the parameter meanings.
    void tune (int size, int age);

    // VFALCO TODO Document this.
    void sweep ();

    // VFALCO TODO Document this.
    //             What are the units of the return value?
    int getWriteLoad ();

    // VFALCO TODO Document this.
    //        NOTE What's the return value?
    int import (String sourceBackendParameters);

private:
    void importVisitor (std::vector <NodeObject::pointer>& objects, NodeObject::pointer object);
    
    static Backend* createBackend (String const& parameters);

    static Array <BackendFactory*> s_factories;

private:
    // Persistent key/value storage.
    ScopedPointer <Backend> m_backend;

    // Larger key/value storage, but not necessarily persistent.
    ScopedPointer <Backend> m_fastBackend;

    // VFALCO NOTE What are these things for? We need comments.
    TaggedCache <uint256, NodeObject, UptimeTimerAdapter> m_cache;
    KeyCache <uint256, UptimeTimerAdapter> m_negativeCache;
};

#endif
