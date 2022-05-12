//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2021 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <ripple/app/rdb/PeerFinder.h>

namespace ripple {

void
initPeerFinderDB(
    soci::session& session,
    BasicConfig const& config,
    beast::Journal j)
{
    DBConfig m_sociConfig(config, "peerfinder");
    m_sociConfig.open(session);

    JLOG(j.info()) << "Opening database at '" << m_sociConfig.connectionString()
                   << "'";

    soci::transaction tr(session);
    session << "PRAGMA encoding=\"UTF-8\";";

    session << "CREATE TABLE IF NOT EXISTS SchemaVersion ( "
               "  name             TEXT PRIMARY KEY, "
               "  version          INTEGER"
               ");";

    session << "CREATE TABLE IF NOT EXISTS PeerFinder_BootstrapCache ( "
               "  id       INTEGER PRIMARY KEY AUTOINCREMENT, "
               "  address  TEXT UNIQUE NOT NULL, "
               "  valence  INTEGER"
               ");";

    session << "CREATE INDEX IF NOT EXISTS "
               "  PeerFinder_BootstrapCache_Index ON "
               "PeerFinder_BootstrapCache "
               "  (  "
               "    address "
               "  ); ";

    tr.commit();
}

void
updatePeerFinderDB(
    soci::session& session,
    int currentSchemaVersion,
    beast::Journal j)
{
    soci::transaction tr(session);
    // get version
    int version(0);
    {
        // SOCI requires a boost::optional (not std::optional) parameter.
        boost::optional<int> vO;
        session << "SELECT "
                   "  version "
                   "FROM SchemaVersion WHERE "
                   "  name = 'PeerFinder';",
            soci::into(vO);

        version = vO.value_or(0);

        JLOG(j.info()) << "Opened version " << version << " database";
    }

    {
        if (version < currentSchemaVersion)
        {
            JLOG(j.info()) << "Updating database to version "
                           << currentSchemaVersion;
        }
        else if (version > currentSchemaVersion)
        {
            Throw<std::runtime_error>(
                "The PeerFinder database version is higher than expected");
        }
    }

    if (version < 4)
    {
        //
        // Remove the "uptime" column from the bootstrap table
        //

        session << "CREATE TABLE IF NOT EXISTS "
                   "PeerFinder_BootstrapCache_Next ( "
                   "  id       INTEGER PRIMARY KEY AUTOINCREMENT, "
                   "  address  TEXT UNIQUE NOT NULL, "
                   "  valence  INTEGER"
                   ");";

        session << "CREATE INDEX IF NOT EXISTS "
                   "  PeerFinder_BootstrapCache_Next_Index ON "
                   "    PeerFinder_BootstrapCache_Next "
                   "  ( address ); ";

        std::size_t count;
        session << "SELECT COUNT(*) FROM PeerFinder_BootstrapCache;",
            soci::into(count);

        std::vector<PeerFinder::Store::Entry> list;

        {
            list.reserve(count);
            std::string s;
            int valence;
            soci::statement st =
                (session.prepare << "SELECT "
                                    " address, "
                                    " valence "
                                    "FROM PeerFinder_BootstrapCache;",
                 soci::into(s),
                 soci::into(valence));

            st.execute();
            while (st.fetch())
            {
                PeerFinder::Store::Entry entry;
                entry.endpoint = beast::IP::Endpoint::from_string(s);
                if (!is_unspecified(entry.endpoint))
                {
                    entry.valence = valence;
                    list.push_back(entry);
                }
                else
                {
                    JLOG(j.error()) << "Bad address string '" << s
                                    << "' in Bootcache table";
                }
            }
        }

        if (!list.empty())
        {
            std::vector<std::string> s;
            std::vector<int> valence;
            s.reserve(list.size());
            valence.reserve(list.size());

            for (auto iter(list.cbegin()); iter != list.cend(); ++iter)
            {
                s.emplace_back(to_string(iter->endpoint));
                valence.emplace_back(iter->valence);
            }

            session << "INSERT INTO PeerFinder_BootstrapCache_Next ( "
                       "  address, "
                       "  valence "
                       ") VALUES ( "
                       "  :s, :valence"
                       ");",
                soci::use(s), soci::use(valence);
        }

        session << "DROP TABLE IF EXISTS PeerFinder_BootstrapCache;";

        session << "DROP INDEX IF EXISTS PeerFinder_BootstrapCache_Index;";

        session << "ALTER TABLE PeerFinder_BootstrapCache_Next "
                   "  RENAME TO PeerFinder_BootstrapCache;";

        session << "CREATE INDEX IF NOT EXISTS "
                   "  PeerFinder_BootstrapCache_Index ON "
                   "PeerFinder_BootstrapCache "
                   "  (  "
                   "    address "
                   "  ); ";
    }

    if (version < 3)
    {
        //
        // Remove legacy endpoints from the schema
        //

        session << "DROP TABLE IF EXISTS LegacyEndpoints;";

        session << "DROP TABLE IF EXISTS PeerFinderLegacyEndpoints;";

        session << "DROP TABLE IF EXISTS PeerFinder_LegacyEndpoints;";

        session << "DROP TABLE IF EXISTS PeerFinder_LegacyEndpoints_Index;";
    }

    {
        int const v(currentSchemaVersion);
        session << "INSERT OR REPLACE INTO SchemaVersion ("
                   "   name "
                   "  ,version "
                   ") VALUES ( "
                   "  'PeerFinder', :version "
                   ");",
            soci::use(v);
    }

    tr.commit();
}

void
readPeerFinderDB(
    soci::session& session,
    std::function<void(std::string const&, int)> const& func)
{
    std::string s;
    int valence;
    soci::statement st =
        (session.prepare << "SELECT "
                            " address, "
                            " valence "
                            "FROM PeerFinder_BootstrapCache;",
         soci::into(s),
         soci::into(valence));

    st.execute();
    while (st.fetch())
    {
        func(s, valence);
    }
}

void
savePeerFinderDB(
    soci::session& session,
    std::vector<PeerFinder::Store::Entry> const& v)
{
    soci::transaction tr(session);
    session << "DELETE FROM PeerFinder_BootstrapCache;";

    if (!v.empty())
    {
        std::vector<std::string> s;
        std::vector<int> valence;
        s.reserve(v.size());
        valence.reserve(v.size());

        for (auto const& e : v)
        {
            s.emplace_back(to_string(e.endpoint));
            valence.emplace_back(e.valence);
        }

        session << "INSERT INTO PeerFinder_BootstrapCache ( "
                   "  address, "
                   "  valence "
                   ") VALUES ( "
                   "  :s, :valence "
                   ");",
            soci::use(s), soci::use(valence);
    }

    tr.commit();
}

}  // namespace ripple
