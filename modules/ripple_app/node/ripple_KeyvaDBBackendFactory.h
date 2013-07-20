//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_KEYVABACKENDFACTORY_H_INCLUDED
#define RIPPLE_KEYVABACKENDFACTORY_H_INCLUDED

/** Factory to produce KeyvaDB backends for the NodeStore.
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
