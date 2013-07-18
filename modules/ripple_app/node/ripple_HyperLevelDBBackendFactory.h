//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_HYPERLEVELDBBACKENDFACTORY_H_INCLUDED
#define RIPPLE_HYPERLEVELDBBACKENDFACTORY_H_INCLUDED

#if RIPPLE_HYPERLEVELDB_AVAILABLE

/** Factory to produce HyperLevelDB backends for the NodeStore.
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
    NodeStore::Backend* createInstance (size_t keyBytes, StringPairArray const& keyValues);
};

#endif

#endif
