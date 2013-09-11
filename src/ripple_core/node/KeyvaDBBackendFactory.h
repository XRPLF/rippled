//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_CORE_NODE_KEYVABACKENDFACTORY_H_INCLUDED
#define RIPPLE_CORE_NODE_KEYVABACKENDFACTORY_H_INCLUDED

/** Factory to produce KeyvaDB backends for the NodeStore.

    @see NodeStore
*/
class KeyvaDBBackendFactory : public NodeStore::BackendFactory
{
private:
    class Backend;

    KeyvaDBBackendFactory ();
    ~KeyvaDBBackendFactory ();

public:
    static KeyvaDBBackendFactory& getInstance ();

    String getName () const;

    NodeStore::Backend* createInstance (size_t keyBytes,
                                        StringPairArray const& keyValues,
                                        NodeStore::Scheduler& scheduler);
};

#endif
