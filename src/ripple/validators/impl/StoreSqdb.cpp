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

namespace ripple {
namespace Validators {

StoreSqdb::StoreSqdb (Journal journal)
    : m_journal (journal)
{
}

StoreSqdb::~StoreSqdb ()
{
}

Error StoreSqdb::open (File const& file)
{
    Error error (m_session.open (file.getFullPathName ()));

    if (!error)
        error = init ();

    return error;
}

//--------------------------------------------------------------------------

void StoreSqdb::insert (SourceDesc& desc)
{
    sqdb::transaction tr (m_session);

    bool const found (select (desc));

    if (found)
    {
        selectList (desc);
    }
    else
    {
        Error error;

        String const sourceID (desc.source->uniqueID().toStdString());
        String const createParam (desc.source->createParam().toStdString());
        String const lastFetchTime (Utilities::timeToString (desc.lastFetchTime));
        String const expirationTime (Utilities::timeToString (desc.expirationTime));

        sqdb::statement st = (m_session.prepare <<
            "INSERT INTO ValidatorsSource ( "
            "  sourceID, "
            "  createParam, "
            "  lastFetchTime, "
            "  expirationTime "
            ") VALUES ( "
            "  ?, ?, ?, ? "
            "); "
            ,sqdb::use (sourceID)
            ,sqdb::use (createParam)
            ,sqdb::use (lastFetchTime)
            ,sqdb::use (expirationTime)
            );

        st.execute_and_fetch (error);

        if (! error)
        {
            error = tr.commit ();
        }

        if (error)
        {
            tr.rollback ();
            report (error, __FILE__, __LINE__);
        }
    }
}

//--------------------------------------------------------------------------

void StoreSqdb::update (SourceDesc& desc, bool updateFetchResults)
{
    Error error;

    String const sourceID (desc.source->uniqueID());
    String const lastFetchTime (Utilities::timeToString (desc.lastFetchTime));
    String const expirationTime (Utilities::timeToString (desc.expirationTime));

    sqdb::transaction tr (m_session);

    m_session.once (error) <<
        "UPDATE ValidatorsSource SET "
        "  lastFetchTime = ?, "
        "  expirationTime = ? "
        "WHERE "
        "  sourceID = ? "
        ,sqdb::use (lastFetchTime)
        ,sqdb::use (expirationTime)
        ,sqdb::use (sourceID)
        ;

    if (! error && updateFetchResults)
    {
        // Delete the previous data set
        {
            sqdb::statement st = (m_session.prepare <<
                "DELETE FROM ValidatorsSourceInfo WHERE "
                "  sourceID = ?; "
                ,sqdb::use (sourceID)
                );

            st.execute_and_fetch (error);
        }

        // Insert the new data set
        if (! error)
        {
            std::string publicKey;
            String label;

            sqdb::statement st = (m_session.prepare <<
                "INSERT INTO ValidatorsSourceInfo ( "
                "  sourceID, "
                "  publicKey, "
                "  label "
                ") VALUES ( "
                "  ?, ?, ? "
                ");"
                ,sqdb::use (sourceID)
                ,sqdb::use (publicKey)
                ,sqdb::use (label)
                );

            Array <Source::Info>& list (desc.result.list);
            for (std::size_t i = 0; ! error && i < list.size(); ++i)
            {
                Source::Info& info (list.getReference(i));
                publicKey = Utilities::publicKeyToString (info.publicKey);
                label = list[i].label;
                st.execute_and_fetch (error);
            }
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

//--------------------------------------------------------------------------

void StoreSqdb::report (Error const& error, char const* fileName, int lineNumber)
{
    if (error)
    {
        m_journal.error <<
            "Failure: '"<< error.getReasonText() << "' " <<
            " at " << Debug::getSourceLocation (fileName, lineNumber);
    }
}

//--------------------------------------------------------------------------

/** Reads the fixed information into the SourceDesc if it exists.
    Returns `true` if the record was found.
*/
bool StoreSqdb::select (SourceDesc& desc)
{
    bool found (false);

    Error error;

    String const sourceID (desc.source->uniqueID());
    String lastFetchTime;
    String expirationTime;
    sqdb::statement st = (m_session.prepare <<
        "SELECT "
        "  lastFetchTime, "
        "  expirationTime "
        "FROM ValidatorsSource WHERE "
        "  sourceID = ? "
        ,sqdb::into (lastFetchTime)
        ,sqdb::into (expirationTime)
        ,sqdb::use (sourceID)
        );

    if (st.execute_and_fetch (error))
    {
        found = true;
        desc.lastFetchTime = Utilities::stringToTime (lastFetchTime);
        desc.expirationTime = Utilities::stringToTime (expirationTime);
    }

    if (error)
    {
        report (error, __FILE__, __LINE__);
    }

    return found;
}

//--------------------------------------------------------------------------

/** Reads the variable information into the SourceDesc.
    This should only be called when the sourceID was already found.
*/
void StoreSqdb::selectList (SourceDesc& desc)
{
    Error error;
       
    String const sourceID (desc.source->uniqueID());

    // Get the count
    std::size_t count;
    if (! error)
    {
        m_session.once (error) <<
            "SELECT "
            "  COUNT(*) "
            "FROM ValidatorsSourceInfo WHERE "
            "  sourceID = ? "
            ,sqdb::into (count)
            ,sqdb::use (sourceID)
            ;
    }

    if (error)
    {
        report (error, __FILE__, __LINE__);
        return;
    }

    // Precondition: the list must be empty.
    bassert (desc.result.list.size() == 0);

    // Pre-allocate some storage
    desc.result.list.ensureStorageAllocated (count);

    // Prepare the select
    {
        std::string publicKey;
        std::string label;
        sqdb::statement st = (m_session.prepare <<
            "SELECT "
            "  publicKey, "
            "  label "
            "FROM ValidatorsSourceInfo WHERE "
            "  sourceID = ? "
            ,sqdb::into (publicKey)
            ,sqdb::into (label)
            ,sqdb::use (sourceID)
            );

        // Add all the records to the list
        if (st.execute_and_fetch (error))
        {
            do
            {
                Source::Info info;
                info.publicKey = Utilities::stringToPublicKey (publicKey);
                info.label = label;
                desc.result.list.add (info);
            }
            while (st.fetch (error));
        }
    }

    if (error)
    {
        report (error, __FILE__, __LINE__);
    }
}

//--------------------------------------------------------------------------

Error StoreSqdb::init ()
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
            "CREATE TABLE IF NOT EXISTS ValidatorsSource ( "
            "  id               INTEGER PRIMARY KEY AUTOINCREMENT, "
            "  sourceID         TEXT UNIQUE,   "
            "  createParam      TEXT NOT NULL, "
            "  lastFetchTime    TEXT NOT NULL, "
            "  expirationTime   TEXT NOT NULL "
            ");"
            ;
    }

    if (! error)
    {
        m_session.once (error) <<
            "CREATE TABLE IF NOT EXISTS ValidatorsSourceInfo ( "
            "  id               INTEGER PRIMARY KEY AUTOINCREMENT, "
            "  sourceID         TEXT NOT NULL, "
            "  publicKey        TEXT NOT NULL, "
            "  label            TEXT NOT NULL  "
            ");"
            ;
    }

    if (! error)
    {
        m_session.once (error) <<
            "CREATE INDEX IF NOT EXISTS "
            "  ValidatorsSourceInfoIndex ON ValidatorsSourceInfo "
            "  (  "
            "    sourceID "
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

}
}
