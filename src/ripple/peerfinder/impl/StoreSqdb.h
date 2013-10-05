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
class StoreSqdb : public Store
{
private:
    Journal m_journal;
    sqdb::session m_session;

public:
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

        if (!error)
            error = init ();

        return error;
    }

    void loadLegacyEndpoints (
        std::vector <IPEndpoint>& list)
    {
        list.clear ();

        Error error;

        // Get the count
        std::size_t count;
        if (! error)
        {
            m_session.once (error) <<
                "SELECT COUNT(*) FROM LegacyEndpoints "
                ,sqdb::into (count)
                ;
        }

        if (error)
        {
            report (error, __FILE__, __LINE__);
            return;
        }

        list.reserve (count);

        {
            std::string s;
            sqdb::statement st = (m_session.prepare <<
                "SELECT ipv4 FROM LegacyEndpoints "
                ,sqdb::into (s)
                );

            if (st.execute_and_fetch (error))
            {
                do
                {
                    IPEndpoint ep (IPEndpoint::from_string (s));
                    if (! ep.empty())
                        list.push_back (ep);
                }
                while (st.fetch (error));
            }
        }

        if (error)
        {
            report (error, __FILE__, __LINE__);
        }
    }

    void updateLegacyEndpoints (
        std::vector <LegacyEndpoint const*> const& list)
    {
        typedef std::vector <LegacyEndpoint const*> List;

        Error error;

        sqdb::transaction tr (m_session);

        m_session.once (error) <<
            "DELETE FROM LegacyEndpoints";

        if (! error)
        {
            std::string s;
            sqdb::statement st = (m_session.prepare <<
                "INSERT INTO LegacyEndpoints ( "
                "  ipv4 "
                ") VALUES ( "
                "  ? "
                ");"
                ,sqdb::use (s)
                );

            for (List::const_iterator iter (list.begin());
                !error && iter != list.end(); ++iter)
            {
                IPEndpoint const& ep ((*iter)->address);
                s = ep.to_string();
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
                "CREATE TABLE IF NOT EXISTS LegacyEndpoints ( "
                "  id    INTEGER PRIMARY KEY AUTOINCREMENT, "
                "  ipv4  TEXT UNIQUE NOT NULL   "
                ");"
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
