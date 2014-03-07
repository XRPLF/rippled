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

namespace ripple {
namespace PeerFinder {

/** Database persistence for PeerFinder using SQLite */
class StoreSqdb
    : public Store
    , public beast::LeakChecked <StoreSqdb>
{
private:
    beast::Journal m_journal;
    beast::sqdb::session m_session;

public:
    enum
    {
        // This determines the on-database format of the data
        currentSchemaVersion = 4
    };

    explicit StoreSqdb (beast::Journal journal = beast::Journal())
        : m_journal (journal)
    {
    }

    ~StoreSqdb ()
    {
    }

    beast::Error open (beast::File const& file)
    {
        beast::Error error (m_session.open (file.getFullPathName ()));

        m_journal.info << "Opening database at '" << file.getFullPathName() << "'";

        if (! error)
            error = init ();

        if (! error)
            error = update ();

        return error;
    }

    // Loads the bootstrap cache, calling the callback for each entry
    //
    std::size_t load (load_callback const& cb)
    {
        std::size_t n (0);
        beast::Error error;
        std::string s;
        int valence;
        beast::sqdb::statement st = (m_session.prepare <<
            "SELECT "
            " address, "
            " valence "
            "FROM PeerFinder_BootstrapCache "
            , beast::sqdb::into (s)
            , beast::sqdb::into (valence)
            );

        if (st.execute_and_fetch (error))
        {
            do
            {
                beast::IP::Endpoint const endpoint (
                    beast::IP::Endpoint::from_string (s));

                if (! is_unspecified (endpoint))
                {
                    cb (endpoint, valence);
                    ++n;
                }
                else
                {
                    m_journal.error <<
                        "Bad address string '" << s << "' in Bootcache table";
                }
            }
            while (st.fetch (error));
        }

        if (error)
            report (error, __FILE__, __LINE__);

        return n;
    }

    // Overwrites the stored bootstrap cache with the specified array.
    //
    void save (std::vector <Entry> const& v)
    {
        beast::Error error;
        beast::sqdb::transaction tr (m_session);
        m_session.once (error) <<
            "DELETE FROM PeerFinder_BootstrapCache";
        if (! error)
        {
            std::string s;
            int valence;

            beast::sqdb::statement st = (m_session.prepare <<
                "INSERT INTO PeerFinder_BootstrapCache ( "
                "  address, "
                "  valence "
                ") VALUES ( "
                "  ?, ? "
                ");"
                , beast::sqdb::use (s)
                , beast::sqdb::use (valence)
                );

            for (auto const& e : v)
            {
                s = to_string (e.endpoint);
                valence = e.valence;
                st.execute_and_fetch (error);
                if (error)
                    break;
            }
        }

        if (! error)
            error = tr.commit();

        if (error)
        {
            tr.rollback ();
            report (error, __FILE__, __LINE__);
        }
    }

    // Convert any existing entries from an older schema to the
    // current one, if appropriate.
    //
    beast::Error update ()
    {
        beast::Error error;

        beast::sqdb::transaction tr (m_session);

        // get version
        int version (0);
        if (! error)
        {
            m_session.once (error) <<
                "SELECT "
                "  version "
                "FROM SchemaVersion WHERE "
                "  name = 'PeerFinder'"
                ,beast::sqdb::into (version)
                ;

            if (! error)
            {
                if (!m_session.got_data())
                    version = 0;

                m_journal.info <<
                    "Opened version " << version << " database";
            }
        }

        if (!error)
        {
            if (version < currentSchemaVersion)
                m_journal.info <<
                    "Updating database to version " << currentSchemaVersion;
            else if (version > currentSchemaVersion)
                error.fail (__FILE__, __LINE__,
                    "The PeerFinder database version is higher than expected");
        }

        if (! error && version < 4)
        {
            //
            // Remove the "uptime" column from the bootstrap table
            //

            if (! error)
                m_session.once (error) <<
                    "CREATE TABLE IF NOT EXISTS PeerFinder_BootstrapCache_Next ( "
                    "  id       INTEGER PRIMARY KEY AUTOINCREMENT, "
                    "  address  TEXT UNIQUE NOT NULL, "
                    "  valence  INTEGER"
                    ");"
                    ;

            if (! error)
                m_session.once (error) <<
                    "CREATE INDEX IF NOT EXISTS "
                    "  PeerFinder_BootstrapCache_Next_Index ON "
                    "    PeerFinder_BootstrapCache_Next "
                    "  ( address ); "
                    ;

            std::size_t count;
            if (! error)
                m_session.once (error) <<
                    "SELECT COUNT(*) FROM PeerFinder_BootstrapCache "
                    ,beast::sqdb::into (count)
                    ;

            std::vector <Store::Entry> list;

            if (! error)
            {
                list.reserve (count);
                std::string s;
                int valence;
                beast::sqdb::statement st = (m_session.prepare <<
                    "SELECT "
                    " address, "
                    " valence "
                    "FROM PeerFinder_BootstrapCache "
                    , beast::sqdb::into (s)
                    , beast::sqdb::into (valence)
                    );

                if (st.execute_and_fetch (error))
                {
                    do
                    {
                        Store::Entry entry;
                        entry.endpoint = beast::IP::Endpoint::from_string (s);
                        if (! is_unspecified (entry.endpoint))
                        {
                            entry.valence = valence;
                            list.push_back (entry);
                        }
                        else
                        {
                            m_journal.error <<
                                "Bad address string '" << s << "' in Bootcache table";
                        }
                    }
                    while (st.fetch (error));
                }
            }

            if (! error)
            {
                std::string s;
                int valence;
                beast::sqdb::statement st = (m_session.prepare <<
                    "INSERT INTO PeerFinder_BootstrapCache_Next ( "
                    "  address, "
                    "  valence "
                    ") VALUES ( "
                    "  ?, ?"
                    ");"
                    , beast::sqdb::use (s)
                    , beast::sqdb::use (valence)
                    );

                for (auto iter (list.cbegin());
                    !error && iter != list.cend(); ++iter)
                {
                    s = to_string (iter->endpoint);
                    valence = iter->valence;
                    st.execute_and_fetch (error);
                }

            }

            if (! error)
                m_session.once (error) <<
                    "DROP TABLE IF EXISTS PeerFinder_BootstrapCache";

            if (! error)
                m_session.once (error) <<
                    "DROP INDEX IF EXISTS PeerFinder_BootstrapCache_Index";

            if (! error)
                m_session.once (error) <<
                    "ALTER TABLE PeerFinder_BootstrapCache_Next "
                    "  RENAME TO PeerFinder_BootstrapCache";

            if (! error)
                m_session.once (error) <<
                    "CREATE INDEX IF NOT EXISTS "
                    "  PeerFinder_BootstrapCache_Index ON PeerFinder_BootstrapCache "
                    "  (  "
                    "    address "
                    "  ); "
                    ;
        }

        if (! error && version < 3)
        {
            //
            // Remove legacy endpoints from the schema
            //

            if (! error)
                m_session.once (error) <<
                    "DROP TABLE IF EXISTS LegacyEndpoints";

            if (! error)
                m_session.once (error) <<
                    "DROP TABLE IF EXISTS PeerFinderLegacyEndpoints";

            if (! error)
                m_session.once (error) <<
                    "DROP TABLE IF EXISTS PeerFinder_LegacyEndpoints";

            if (! error)
                m_session.once (error) <<
                    "DROP TABLE IF EXISTS PeerFinder_LegacyEndpoints_Index";
        }

        if (! error)
        {
            int const version (currentSchemaVersion);
            m_session.once (error) <<
                "INSERT OR REPLACE INTO SchemaVersion ("
                "   name "
                "  ,version "
                ") VALUES ( "
                "  'PeerFinder', ? "
                ")"
                ,beast::sqdb::use(version);
        }

        if (! error)
            error = tr.commit();

        if (error)
        {
            tr.rollback();
            report (error, __FILE__, __LINE__);
        }

        return error;
    }

private:
    beast::Error init ()
    {
        beast::Error error;
        beast::sqdb::transaction tr (m_session);

        if (! error)
            m_session.once (error) <<
                "PRAGMA encoding=\"UTF-8\"";

        if (! error)
            m_session.once (error) <<
                "CREATE TABLE IF NOT EXISTS SchemaVersion ( "
                "  name             TEXT PRIMARY KEY, "
                "  version          INTEGER"
                ");"
                ;

        if (! error)
            m_session.once (error) <<
                "CREATE TABLE IF NOT EXISTS PeerFinder_BootstrapCache ( "
                "  id       INTEGER PRIMARY KEY AUTOINCREMENT, "
                "  address  TEXT UNIQUE NOT NULL, "
                "  valence  INTEGER"
                ");"
                ;

        if (! error)
            m_session.once (error) <<
                "CREATE INDEX IF NOT EXISTS "
                "  PeerFinder_BootstrapCache_Index ON PeerFinder_BootstrapCache "
                "  (  "
                "    address "
                "  ); "
                ;

        if (! error)
            error = tr.commit();

        if (error)
        {
            tr.rollback ();
            report (error, __FILE__, __LINE__);
        }

        return error;
    }

    void report (beast::Error const& error, char const* fileName, int lineNumber)
    {
        if (error)
        {
            m_journal.error <<
                "Failure: '"<< error.getReasonText() << "' " <<
                " at " << beast::Debug::getSourceLocation (fileName, lineNumber);
        }
    }
};

}
}

#endif
