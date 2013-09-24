//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_NODESTORE_MEMORYFACTORY_H_INCLUDED
#define RIPPLE_NODESTORE_MEMORYFACTORY_H_INCLUDED

namespace NodeStore
{

/** Factory to produce a RAM based backend for the NodeStore.

    @see Database
*/
class MemoryFactory : public Factory
{
private:
    MemoryFactory ();
    ~MemoryFactory ();

public:
    class BackendImp;

    static MemoryFactory* getInstance ();

    String getName () const;

    Backend* createInstance (size_t keyBytes,
                             Parameters const& keyValues,
                             Scheduler& scheduler);
};

}

#endif
