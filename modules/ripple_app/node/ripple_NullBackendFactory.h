//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_NULLBACKENDFACTORY_H_INCLUDED
#define RIPPLE_NULLBACKENDFACTORY_H_INCLUDED

/** Factory to produce a null backend.

    This is for standalone / testing mode.
*/
class NullBackendFactory : public NodeStore::BackendFactory
{
private:
    class Backend;

    NullBackendFactory ();
    ~NullBackendFactory ();

public:
    static NullBackendFactory& getInstance ();

    String getName () const;

    NodeStore::Backend* createInstance (size_t keyBytes,
                                        StringPairArray const& keyValues,
                                        NodeStore::Scheduler& scheduler);
};

#endif
