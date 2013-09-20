//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_NODESTORE_MDBFACTORY_H_INCLUDED
#define RIPPLE_NODESTORE_MDBFACTORY_H_INCLUDED

#if RIPPLE_MDB_AVAILABLE

namespace NodeStore
{

/** Factory to produce a backend using MDB.

    @note MDB is not currently available for Win32

    @see Database
*/
class MdbFactory : public Factory
{
private:
    MdbFactory ();
    ~MdbFactory ();

public:
    class BackendImp;

    static MdbFactory* getInstance ();

    String getName () const;

    Backend* createInstance (size_t keyBytes,
                             Parameters const& keyValues,
                             Scheduler& scheduler);
};

}

#endif

#endif
