//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_CORE_SOPHIABACKENDFACTORY_H_INCLUDED
#define RIPPLE_CORE_SOPHIABACKENDFACTORY_H_INCLUDED

/** Factory to produce Sophia backends for the NodeStore.

    @see NodeStore
*/
class SophiaBackendFactory : public NodeStore::BackendFactory
{
private:
    class Backend;

    SophiaBackendFactory ();
    ~SophiaBackendFactory ();

public:
    static SophiaBackendFactory& getInstance ();

    String getName () const;

    NodeStore::Backend* createInstance (size_t keyBytes,
                                        StringPairArray const& keyValues,
                                        NodeStore::Scheduler& scheduler);
};

#endif
