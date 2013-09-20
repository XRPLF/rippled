//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_NODESTORE_KEYVADBFACTORY_H_INCLUDED
#define RIPPLE_NODESTORE_KEYVADBFACTORY_H_INCLUDED

namespace NodeStore
{

/** Factory to produce KeyvaDB backends for the NodeStore.

    @see Database
*/
class KeyvaDBFactory : public Factory
{
private:
    KeyvaDBFactory ();
    ~KeyvaDBFactory ();

public:
    class BackendImp;

    static KeyvaDBFactory* getInstance ();

    String getName () const;

    Backend* createInstance (size_t keyBytes,
                             Parameters const& keyValues,
                             Scheduler& scheduler);
};

}

#endif
