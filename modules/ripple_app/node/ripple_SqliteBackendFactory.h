//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_SQLITEBACKENDFACTORY_H_INCLUDED
#define RIPPLE_SQLITEBACKENDFACTORY_H_INCLUDED

/** Factory to produce SQLite backends for the NodeStore.
*/
class SqliteBackendFactory : public NodeStore::BackendFactory
{
private:
    class Backend;

    SqliteBackendFactory ();
    ~SqliteBackendFactory ();

public:
    static SqliteBackendFactory& getInstance ();

    String getName () const;

    NodeStore::Backend* createInstance (size_t keyBytes,
                                        StringPairArray const& keyValues,
                                        NodeStore::Scheduler& scheduler);
};

#endif
