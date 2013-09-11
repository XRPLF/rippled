//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_CORE_NODE_HYPERLEVELDBBACKENDFACTORY_H_INCLUDED
#define RIPPLE_CORE_NODE_HYPERLEVELDBBACKENDFACTORY_H_INCLUDED

#if RIPPLE_HYPERLEVELDB_AVAILABLE

/** Factory to produce HyperLevelDB backends for the NodeStore.

    @see NodeStore
*/
class HyperLevelDBBackendFactory : public NodeStore::BackendFactory
{
private:
    class Backend;

    HyperLevelDBBackendFactory ();
    ~HyperLevelDBBackendFactory ();

public:
    static HyperLevelDBBackendFactory& getInstance ();

    String getName () const;

    NodeStore::Backend* createInstance (size_t keyBytes,
                                        StringPairArray const& keyValues,
                                        NodeStore::Scheduler& scheduler);
};

#endif

#endif
