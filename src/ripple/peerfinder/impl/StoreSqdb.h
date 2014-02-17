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
    , public LeakChecked <StoreSqdb>
{
private:
    Journal m_journal;
    sqdb::session m_session;

public:
    enum
    {
        // This determines the on-database format of the data
        currentSchemaVersion = 3
    };

    explicit StoreSqdb (Journal journal = Journal())
        : m_journal (journal)
    {
    }

    ~StoreSqdb ()
    {
    }

    Error open (File const& file)
    {
        Error error (m_session.open (file.getFullPathName ()));

        m_journal.info << "Opening database at '" << file.getFullPathName() << "'";

        if (!error)
            error = init ();

        if (!error)
            error = update ();

        return error;
    }

    // Loads the entire stored bootstrap cache and returns it as an array.
    //
    std::vector <SavedBootstrapAddress> loadBootstrapCache ()
    {
        std::vector <SavedBootstrapAddress> list;

        Error error;

        // Get the count
        std::size_t count;
        if (! error)
        {
            m_session.once (error) <<
                "SELECT COUNT(*) FROM PeerFinder_BootstrapCache "
                ,sqdb::into (count)
                ;
        }

        if (error)
        {
            report (error, __FILE__, __LINE__);
            return list;
        }

        list.reserve (count);

        {
            std::string s;
            std::chrono::seconds::rep uptimeSeconds;
            int connectionValence;

            sqdb::statement st = (m_session.prepare <<
                "SELECT "
                " address, "
                " uptime, "
                " valence "
                "FROM PeerFinder_BootstrapCache "
                , sqdb::into (s)
                , sqdb::into (uptimeSeconds)
                , sqdb::into (connectionValence)
                );

            if (st.execute_and_fetch (error))
            {
                do
                {
                    SavedBootstrapAddress entry;

                    entry.address = IP::Endpoint::from_string (s);

                    if (! is_unspecified (entry.address))
                    {
                        entry.cumulativeUptime = std::chrono::seconds (uptimeSeconds);
                        entry.connectionValence = connectionValence;

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

        if (error)
        {
            report (error, __FILE__, __LINE__);
        }

        return list;
    }

    // Overwrites the stored bootstrap cache with the specified array.
    //
    void updateBootstrapCache (
        std::vector <SavedBootstrapAddress> const& list)
    {
        Error error;

        sqdb::transaction tr (m_session);

        m_session.once (error) <<
            "DELETE FROM PeerFinder_BootstrapCache";

        if (! error)
        {
            std::string s;
            std::chrono::seconds::rep uptimeSeconds;
            int connectionValence;

            sqdb::statement st = (m_session.prepare <<
                "INSERT INTO PeerFinder_BootstrapCache ( "
                "  address, "
                "  uptime, "
                "  valence "
                ") VALUES ( "
                "  ?, ?, ? "
                ");"
                , sqdb::use (s)
                , sqdb::use (uptimeSeconds)
                , sqdb::use (connectionValence)
                );

            for (std::vector <SavedBootstrapAddress>::const_iterator iter (
                list.begin()); !error && iter != list.end(); ++iter)
            {
                s = to_string (iter->address);
                uptimeSeconds = iter->cumulativeUptime.count ();
                connectionValence = iter->connectionValence;

                st.execute_and_fetch (error);
            }
        }

        if (! error)
        {
            error = tr.commit();
        }

        if (error)
        {
            tr.rollback ();
            report (error, __FILE__, __LINE__);
        }
    }

    // Convert any existing entries from an older schema to the
    // current one, if appropriate.
    //
    Error update ()
    {
        Error error;

        sqdb::transaction tr (m_session);

        // get version
        int version (0);
        if (!error)
        {
            m_session.once (error) <<
                "SELECT "
                "  version "
                "FROM SchemaVersion WHERE "
                "  name = 'PeerFinder'"
                ,sqdb::into (version)
                ;

            if (! error)
            {
                if (!m_session.got_data())
                    version = 0;

                m_journal.info <<
                    "Opened version " << version << " database";
            }
        }

        if (!error && version != currentSchemaVersion)
        {
            m_journal.info <<
                "Updating database to version " << currentSchemaVersion;
        }

        if (!error)
        {
            if (version < 3)
            {
                if (!error)
                    m_session.once (error) <<
                        "DROP TABLE IF EXISTS LegacyEndpoints";

                if (!error)
                    m_session.once (error) <<
                        "DROP TABLE IF EXISTS PeerFinderLegacyEndpoints";

                if (!error)
                    m_session.once (error) <<
                        "DROP TABLE IF EXISTS PeerFinder_LegacyEndpoints";

                if (!error)
                    m_session.once (error) <<
                        "DROP TABLE IF EXISTS PeerFinder_LegacyEndpoints_Index";
            }
        }

        if (!error)
        {
            int const version (currentSchemaVersion);
            m_session.once (error) <<
                "INSERT OR REPLACE INTO SchemaVersion ("
                "   name "
                "  ,version "
                ") VALUES ( "
                "  'PeerFinder', ? "
                ")"
                ,sqdb::use(version);
        }

        if (!error)
            error = tr.commit();

        if (error)
        {
            tr.rollback();
            report (error, __FILE__, __LINE__);
        }

        return error;
    }

private:
    Error init ()
    {
        Error error;
        sqdb::transaction tr (m_session);

        if (! error)
        {
            m_session.once (error) <<
                "PRAGMA encoding=\"UTF-8\"";
        }

        if (! error)
        {
            m_session.once (error) <<
                "CREATE TABLE IF NOT EXISTS SchemaVersion ( "
                "  name             TEXT PRIMARY KEY, "
                "  version          INTEGER"
                ");"
                ;
        }

        if (! error)
        {
            m_session.once (error) <<
                "CREATE TABLE IF NOT EXISTS PeerFinder_BootstrapCache ( "
                "  id       INTEGER PRIMARY KEY AUTOINCREMENT, "
                "  address  TEXT UNIQUE NOT NULL, "
                "  uptime   INTEGER,"
                "  valence  INTEGER"
                ");"
                ;
        }

        if (! error)
        {
            m_session.once (error) <<
                "CREATE INDEX IF NOT EXISTS "
                "  PeerFinder_BootstrapCache_Index ON PeerFinder_BootstrapCache "
                "  (  "
                "    address "
                "  ); "
                ;
        }

        if (! error)
        {
            error = tr.commit();
        }

        if (error)
        {
            tr.rollback ();
            report (error, __FILE__, __LINE__);
        }

        return error;
    }

    void report (Error const& error, char const* fileName, int lineNumber)
    {
        if (error)
        {
            m_journal.error <<
                "Failure: '"<< error.getReasonText() << "' " <<
                " at " << Debug::getSourceLocation (fileName, lineNumber);
        }
    }
};

}
}

#endif
