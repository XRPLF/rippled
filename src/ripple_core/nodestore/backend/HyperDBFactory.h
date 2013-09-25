//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_NODESTORE_HYPERDBFACTORY_H_INCLUDED
#define RIPPLE_NODESTORE_HYPERDBFACTORY_H_INCLUDED

#if RIPPLE_HYPERLEVELDB_AVAILABLE

namespace NodeStore
{

/** Factory to produce HyperLevelDB backends for the NodeStore.

    @see Database
*/
class HyperDBFactory : public NodeStore::Factory
{
private:
    HyperDBFactory ();
    ~HyperDBFactory ();

public:
    class BackendImp;

    static HyperDBFactory* getInstance ();

    String getName () const;

    NodeStore::Backend* createInstance (size_t keyBytes,
                                        Parameters const& keyValues,
                                        NodeStore::Scheduler& scheduler);
};

}

#endif

#endif
