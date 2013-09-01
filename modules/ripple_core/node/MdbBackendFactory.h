//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_CORE_NODE_MDBBACKENDFACTORY_H_INCLUDED
#define RIPPLE_CORE_NODE_MDBBACKENDFACTORY_H_INCLUDED

#if RIPPLE_MDB_AVAILABLE

/** Factory to produce a backend using MDB.

    @note MDB is not currently available for Win32

    @see NodeStore
*/
class MdbBackendFactory : public NodeStore::BackendFactory
{
private:
    class Backend;

    MdbBackendFactory ();
    ~MdbBackendFactory ();

public:
    static MdbBackendFactory& getInstance ();

    String getName () const;

    NodeStore::Backend* createInstance (size_t keyBytes,
                                        StringPairArray const& keyValues,
                                        NodeStore::Scheduler& scheduler);
};

#endif

#endif
