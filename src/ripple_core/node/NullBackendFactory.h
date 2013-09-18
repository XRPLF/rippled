//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_CORE_NODE_NULLBACKENDFACTORY_H_INCLUDED
#define RIPPLE_CORE_NODE_NULLBACKENDFACTORY_H_INCLUDED

/** Factory to produce a null backend.

    This is for standalone / testing mode.

    @see NodeStore
*/
class NullBackendFactory : public NodeStore::BackendFactory
{
private:
    class Backend;

    NullBackendFactory ();
    ~NullBackendFactory ();

public:
    static NullBackendFactory* getInstance ();

    String getName () const;

    NodeStore::Backend* createInstance (size_t keyBytes,
                                        StringPairArray const& keyValues,
                                        NodeStore::Scheduler& scheduler);
};

#endif
