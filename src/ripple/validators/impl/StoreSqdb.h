//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_VALIDATORS_STORESQDB_H_INCLUDED
#define RIPPLE_VALIDATORS_STORESQDB_H_INCLUDED

namespace Validators
{

/** Database persistence for Validators using SQLite */
class StoreSqdb : public Store
{
public:
    explicit StoreSqdb (Journal journal = Journal());

    ~StoreSqdb ();

    Error open (File const& file);

    void insert (SourceDesc& desc);

    void update (SourceDesc& desc, bool updateFetchResults);

private:
    void report (Error const& error, char const* fileName, int lineNumber);

    bool select (SourceDesc& desc);
    void selectList (SourceDesc& desc);

    Error init ();

    Journal m_journal;
    sqdb::session m_session;
};

}

#endif
