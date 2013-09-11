//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_CORE_NODE_LEVELDBBACKENDFACTORY_H_INCLUDED
#define RIPPLE_CORE_NODE_LEVELDBBACKENDFACTORY_H_INCLUDED

/** Factory to produce LevelDB backends for the NodeStore.

    @see NodeStore
*/
class LevelDBBackendFactory : public NodeStore::BackendFactory
{
private:
    class Backend;

    LevelDBBackendFactory ();
    ~LevelDBBackendFactory ();

public:
    static LevelDBBackendFactory& getInstance ();

    String getName () const;

    NodeStore::Backend* createInstance (size_t keyBytes,
                                        StringPairArray const& keyValues,
                                        NodeStore::Scheduler& scheduler);
};

#endif
