//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_NODESTORELEVELDB_H_INCLUDED
#define RIPPLE_NODESTORELEVELDB_H_INCLUDED

/** Factory to produce LevelDB backends for the NodeStore.
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
    NodeStore::Backend* createInstance (HashMap <String, String> const& keyValueParameters);
};

#endif
