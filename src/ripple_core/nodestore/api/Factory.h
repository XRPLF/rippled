//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_NODESTORE_FACTORY_H_INCLUDED
#define RIPPLE_NODESTORE_FACTORY_H_INCLUDED

namespace NodeStore
{

/** Factory to produce backends. */
class Factory
{
public:
    virtual ~Factory () { }

    /** Retrieve the name of this factory. */
    virtual String getName () const = 0;

    /** Create an instance of this factory's backend.
            
        @param keyBytes The fixed number of bytes per key.
        @param keyValues A set of key/value configuration pairs.
        @param scheduler The scheduler to use for running tasks.

        @return A pointer to the Backend object.
    */
    virtual Backend* createInstance (size_t keyBytes,
                                     Parameters const& parameters,
                                     Scheduler& scheduler) = 0;
};

}

#endif
