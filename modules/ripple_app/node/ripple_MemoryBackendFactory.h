//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_MEMORYBACKENDFACTORY_H_INCLUDED
#define RIPPLE_MEMORYBACKENDFACTORY_H_INCLUDED

/** Factory to produce a RAM based backend for the NodeStore.

    @see NodeStore
*/
class MemoryBackendFactory : public NodeStore::BackendFactory
{
private:
    class Backend;

    MemoryBackendFactory ();
    ~MemoryBackendFactory ();

public:
    static MemoryBackendFactory& getInstance ();

    String getName () const;

    NodeStore::Backend* createInstance (size_t keyBytes,
                                        StringPairArray const& keyValues,
                                        NodeStore::Scheduler& scheduler);
};

#endif
