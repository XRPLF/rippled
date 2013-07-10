//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_NODESTORESQLITE_H_INCLUDED
#define RIPPLE_NODESTORESQLITE_H_INCLUDED

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
    NodeStore::Backend* createInstance (HashMap <String, String> const& keyValueParameters);
};

#endif
