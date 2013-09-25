//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_NODESTORE_NULLFACTORY_H_INCLUDED
#define RIPPLE_NODESTORE_NULLFACTORY_H_INCLUDED

namespace NodeStore
{

/** Factory to produce a null backend.

    This is for standalone / testing mode.

    @see Database
*/
class NullFactory : public Factory
{
private:
    NullFactory ();
    ~NullFactory ();

public:
    class BackendImp;

    static NullFactory* getInstance ();

    String getName () const;

    Backend* createInstance (size_t keyBytes,
                             Parameters const& keyValues,
                             Scheduler& scheduler);
};

}

#endif

