//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_NODESTORE_H_INCLUDED
#define RIPPLE_NODESTORE_H_INCLUDED

/** Persistency layer for NodeObject
*/
// VFALCO TODO Move all definitions to the .cpp
class NodeStore : LeakChecked <NodeStore>
{
public:
    /** Back end used for the store.
    */
    class Backend
    {
    public:
        // VFALCO TODO Remove this, the backend should not be a shared
        //             object it should be ScopedPointer owned.
        //
        typedef boost::shared_ptr <Backend> pointer;

        virtual ~Backend () { }

        virtual std::string getDataBaseName() = 0;

        // Store/retrieve a single object
        // These functions must be thread safe
        virtual bool store (NodeObject::ref) = 0;
        virtual NodeObject::pointer retrieve (uint256 const &hash) = 0;

        // Store a group of objects
        // This function will only be called from a single thread
        virtual bool bulkStore(const std::vector< NodeObject::pointer >&) = 0;

        // Visit every object in the database
        // This function will only be called during an import operation
        virtual void visitAll(FUNCTION_TYPE<void (NodeObject::pointer)>) = 0;
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
    NodeStore (String parameters, int cacheSize, int cacheAge);

    /** Add the specified backend factory to the list of available factories.

        The names of available factories are compared against the "type"
        value in the parameter list on construction.
    */
    static void addBackendFactory (BackendFactory& factory);

    bool isLevelDB ();

    float getCacheHitRate ();

    bool store (NodeObjectType type, uint32 index, Blob const& data,
                uint256 const& hash);

    NodeObject::pointer retrieve (uint256 const& hash);

    void bulkWrite (Job&);
    void waitWrite ();
    void tune (int size, int age);
    void sweep ();
    int getWriteLoad ();

    int import (const std::string& fileName);

private:
    static NodeObject::pointer LLRetrieve (uint256 const& hash, leveldb::DB* db);
    static void LLWrite (boost::shared_ptr<NodeObject> ptr, leveldb::DB* db);
    static void LLWrite (const std::vector< boost::shared_ptr<NodeObject> >& set, leveldb::DB* db);

private:
    static Array <BackendFactory*> s_factories;

private:
    ScopedPointer <Backend> m_backend;
    ScopedPointer <Backend> m_backendFast;

    TaggedCache<uint256, NodeObject, UptimeTimerAdapter>  mCache;
    KeyCache <uint256, UptimeTimerAdapter> mNegativeCache;

    boost::mutex                mWriteMutex;
    boost::condition_variable   mWriteCondition;
    int                         mWriteGeneration;
    int                         mWriteLoad;

    std::vector< boost::shared_ptr<NodeObject> > mWriteSet;
    bool mWritePending;
    bool mLevelDB;
    bool mEphemeralDB;
};

#endif
