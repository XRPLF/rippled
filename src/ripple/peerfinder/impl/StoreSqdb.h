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

#ifndef RIPPLE_PEERFINDER_STORESQDB_H_INCLUDED
#define RIPPLE_PEERFINDER_STORESQDB_H_INCLUDED

#include <ripple/app/rdb/RelationalDBInterface_global.h>
#include <ripple/basics/contract.h>
#include <ripple/core/SociDB.h>
#include <ripple/peerfinder/impl/Store.h>
#include <boost/optional.hpp>

namespace ripple {
namespace PeerFinder {

/** Database persistence for PeerFinder using SQLite */
class StoreSqdb : public Store
{
private:
    beast::Journal m_journal;
    soci::session m_sqlDb;

public:
    enum {
        // This determines the on-database format of the data
        currentSchemaVersion = 4
    };

    explicit StoreSqdb(
        beast::Journal journal = beast::Journal{beast::Journal::getNullSink()})
        : m_journal(journal)
    {
    }

    ~StoreSqdb()
    {
    }

    void
    open(BasicConfig const& config)
    {
        init(config);
        update();
    }

    // Loads the bootstrap cache, calling the callback for each entry
    //
    std::size_t
    load(load_callback const& cb) override
    {
        std::size_t n(0);

        readPeerFinderDB(m_sqlDb, [&](std::string const& s, int valence) {
            beast::IP::Endpoint const endpoint(
                beast::IP::Endpoint::from_string(s));

            if (!is_unspecified(endpoint))
            {
                cb(endpoint, valence);
                ++n;
            }
            else
            {
                JLOG(m_journal.error())
                    << "Bad address string '" << s << "' in Bootcache table";
            }
        });

        return n;
    }

    // Overwrites the stored bootstrap cache with the specified array.
    //
    void
    save(std::vector<Entry> const& v) override
    {
        savePeerFinderDB(m_sqlDb, v);
    }

    // Convert any existing entries from an older schema to the
    // current one, if appropriate.
    void
    update()
    {
        updatePeerFinderDB(m_sqlDb, currentSchemaVersion, m_journal);
    }

private:
    void
    init(BasicConfig const& config)
    {
        initPeerFinderDB(m_sqlDb, config, m_journal);
    }
};

}  // namespace PeerFinder
}  // namespace ripple

#endif
