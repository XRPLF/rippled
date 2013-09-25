//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_NODESTORE_LEVELDBFACTORY_H_INCLUDED
#define RIPPLE_NODESTORE_LEVELDBFACTORY_H_INCLUDED

namespace NodeStore
{

/** Factory to produce LevelDBFactory backends for the NodeStore.

    @see Database
*/
class LevelDBFactory : public Factory
{
private:
    LevelDBFactory ();
    ~LevelDBFactory ();

public:
    class BackendImp;

    static LevelDBFactory* getInstance ();

    String getName () const;

    Backend* createInstance (size_t keyBytes,
                             Parameters const& keyValues,
                             Scheduler& scheduler);

private:
    void* m_lruCache;
};

}

#endif
