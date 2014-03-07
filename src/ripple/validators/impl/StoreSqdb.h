//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_VALIDATORS_STORESQDB_H_INCLUDED
#define RIPPLE_VALIDATORS_STORESQDB_H_INCLUDED

namespace ripple {
namespace Validators {

/** Database persistence for Validators using SQLite */
class StoreSqdb
    : public Store
    , public beast::LeakChecked <StoreSqdb>
{
public:
    enum
    {
        // This affects the format of the data!
        currentSchemaVersion = 2
    };

    explicit StoreSqdb (beast::Journal journal = beast::Journal());

    ~StoreSqdb ();

    beast::Error open (beast::File const& file);

    void insert (SourceDesc& desc);

    void update (SourceDesc& desc, bool updateFetchResults);

    void remove (RipplePublicKey const& publicKey);

private:
    void report (beast::Error const& error, char const* fileName, int lineNumber);

    bool select (SourceDesc& desc);
    void selectList (SourceDesc& desc);

    beast::Error update ();
    beast::Error init ();

    beast::Journal m_journal;
    beast::sqdb::session m_session;
};

}
}

#endif
