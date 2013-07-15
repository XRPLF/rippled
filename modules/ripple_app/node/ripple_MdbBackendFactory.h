//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_MDBBACKENDFACTORY_H_INCLUDED
#define RIPPLE_MDBBACKENDFACTORY_H_INCLUDED

#if RIPPLE_MDB_AVAILABLE

/** Factory to produce a backend using MDB.

    @note MDB is not currently available for Win32
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
    NodeStore::Backend* createInstance (StringPairArray const& keyValues);
};

#endif

#endif
