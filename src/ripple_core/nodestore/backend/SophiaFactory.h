//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_NODESTORE_SOPHIAFACTORY_H_INCLUDED
#define RIPPLE_NODESTORE_SOPHIAFACTORY_H_INCLUDED

namespace NodeStore
{

/** Factory to produce Sophia backends for the NodeStore.

    @see Database
*/
class SophiaFactory : public Factory
{
private:
    SophiaFactory ();
    ~SophiaFactory ();

public:
    class BackendImp;

    static SophiaFactory* getInstance ();

    String getName () const;

    NodeStore::Backend* createInstance (size_t keyBytes,
                                        Parameters const& keyValues,
                                        Scheduler& scheduler);
};

}

#endif
