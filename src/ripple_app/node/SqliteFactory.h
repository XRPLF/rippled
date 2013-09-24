//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_APP_SQLITEFACTORY_H_INCLUDED
#define RIPPLE_APP_SQLITEFACTORY_H_INCLUDED

/** Factory to produce SQLite backends for the NodeStore.

    @see Database
*/
class SqliteFactory : public NodeStore::Factory
{
private:
    SqliteFactory ();
    ~SqliteFactory ();

public:
    class BackendImp;

    static SqliteFactory* getInstance ();

    String getName () const;

    NodeStore::Backend* createInstance (size_t keyBytes,
                                        NodeStore::Parameters const& keyValues,
                                        NodeStore::Scheduler& scheduler);
};

#endif
