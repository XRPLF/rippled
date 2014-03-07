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

StoreSqdb::StoreSqdb (beast::Journal journal)
    : m_journal (journal)
{
}

StoreSqdb::~StoreSqdb ()
{
}

beast::Error StoreSqdb::open (beast::File const& file)
{
    beast::Error error (m_session.open (file.getFullPathName ()));

    m_journal.info <<
        "Opening " << file.getFullPathName();

    if (!error)
        error = init ();

    if (!error)
        error = update ();

    if (error)
        m_journal.error <<
            "Failed opening database: " << error.what();

    return error;
}

//--------------------------------------------------------------------------

void StoreSqdb::insert (SourceDesc& desc)
{
    beast::sqdb::transaction tr (m_session);

    bool const found (select (desc));

    if (found)
    {
        selectList (desc);
    }
    else
    {
        beast::Error error;

        beast::String const sourceID (desc.source->uniqueID().toStdString());
        beast::String const createParam (desc.source->createParam().toStdString());
        beast::String const lastFetchTime (Utilities::timeToString (desc.lastFetchTime));
        beast::String const expirationTime (Utilities::timeToString (desc.expirationTime));

        beast::sqdb::statement st = (m_session.prepare <<
            "INSERT INTO Validators_Source ( "
            "  sourceID, "
            "  createParam, "
            "  lastFetchTime, "
            "  expirationTime "
            ") VALUES ( "
            "  ?, ?, ?, ? "
            "); "
            ,beast::sqdb::use (sourceID)
            ,beast::sqdb::use (createParam)
            ,beast::sqdb::use (lastFetchTime)
            ,beast::sqdb::use (expirationTime)
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
    beast::Error error;

    beast::String const sourceID (desc.source->uniqueID());
    beast::String const lastFetchTime (Utilities::timeToString (desc.lastFetchTime));
    beast::String const expirationTime (Utilities::timeToString (desc.expirationTime));

    beast::sqdb::transaction tr (m_session);

    m_session.once (error) <<
        "UPDATE Validators_Source SET "
        "  lastFetchTime = ?, "
        "  expirationTime = ? "
        "WHERE "
        "  sourceID = ? "
        ,beast::sqdb::use (lastFetchTime)
        ,beast::sqdb::use (expirationTime)
        ,beast::sqdb::use (sourceID)
        ;

    if (! error && updateFetchResults)
    {
        // Delete the previous data set
        m_session.once (error) <<
            "DELETE FROM Validators_SourceItem WHERE "
            "  sourceID = ?; "
            ,beast::sqdb::use (sourceID)
            ;

        // Insert the new data set
        if (! error)
        {
            std::string publicKeyString;
            beast::String label;

            beast::sqdb::statement st = (m_session.prepare <<
                "INSERT INTO Validators_SourceItem ( "
                "  sourceID, "
                "  publicKey, "
                "  label "
                ") VALUES ( "
                "  ?, ?, ? "
                ");"
                ,beast::sqdb::use (sourceID)
                ,beast::sqdb::use (publicKeyString)
                ,beast::sqdb::use (label)
                );

            std::vector <Source::Item>& list (desc.results.list);
            for (std::size_t i = 0; ! error && i < list.size(); ++i)
            {
                Source::Item& item (list [i]);
                publicKeyString = item.publicKey.to_string ();
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

void StoreSqdb::report (beast::Error const& error, char const* fileName, int lineNumber)
{
    if (error)
    {
        m_journal.error <<
            "Failure: '"<< error.getReasonText() << "' " <<
            " at " << beast::Debug::getSourceLocation (fileName, lineNumber);
    }
}

//--------------------------------------------------------------------------

/** Reads the fixed information into the SourceDesc if it exists.
    Returns `true` if the record was found.
*/
bool StoreSqdb::select (SourceDesc& desc)
{
    bool found (false);

    beast::Error error;

    beast::String const sourceID (desc.source->uniqueID());
    beast::String lastFetchTime;
    beast::String expirationTime;
    beast::sqdb::statement st = (m_session.prepare <<
        "SELECT "
        "  lastFetchTime, "
        "  expirationTime "
        "FROM Validators_Source WHERE "
        "  sourceID = ? "
        ,beast::sqdb::into (lastFetchTime)
        ,beast::sqdb::into (expirationTime)
        ,beast::sqdb::use (sourceID)
        );

    if (st.execute_and_fetch (error))
    {
        m_journal.debug <<
            "Found record for " << *desc.source;
        
        found = true;
        desc.lastFetchTime = Utilities::stringToTime (lastFetchTime);
        desc.expirationTime = Utilities::stringToTime (expirationTime);
    }
    else if (! error)
    {
        m_journal.info <<
            "No previous record for " << *desc.source;
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
    beast::Error error;
       
    beast::String const sourceID (desc.source->uniqueID());

    // Get the count
    std::size_t count;
    if (! error)
    {
        m_session.once (error) <<
            "SELECT "
            "  COUNT(*) "
            "FROM Validators_SourceItem WHERE "
            "  sourceID = ? "
            ,beast::sqdb::into (count)
            ,beast::sqdb::use (sourceID)
            ;
    }

    if (error)
    {
        report (error, __FILE__, __LINE__);
        return;
    }

    // Precondition: the list must be empty.
    bassert (desc.results.list.size() == 0);

    // Pre-allocate some storage
    desc.results.list.reserve (count);

    // Prepare the select
    {
        std::string publicKeyString;
        std::string label;
        beast::sqdb::statement st = (m_session.prepare <<
            "SELECT "
            "  publicKey, "
            "  label "
            "FROM Validators_SourceItem WHERE "
            "  sourceID = ? "
            ,beast::sqdb::into (publicKeyString)
            ,beast::sqdb::into (label)
            ,beast::sqdb::use (sourceID)
            );

        // Add all the records to the list
        if (st.execute_and_fetch (error))
        {
            do
            {
                Source::Item info;
                std::pair <RipplePublicKey, bool> result (
                    RipplePublicKey::from_string (publicKeyString));
                if (result.second)
                {
                    bassert (result.first.to_string() == publicKeyString);
                    info.publicKey = result.first;
                    info.label = label;
                    desc.results.list.push_back (info);
                }
                else
                {
                    m_journal.error <<
                        "Invalid public key '" << publicKeyString <<
                        "' found in database";
                }
            }
            while (st.fetch (error));

            if (! error)
            {
                m_journal.info <<
                    "Loaded " << desc.results.list.size() <<
                    " trusted validators for " << *desc.source;
            }
        }
    }

    if (error)
    {
        report (error, __FILE__, __LINE__);
    }
}

//--------------------------------------------------------------------------

// Update the database for the current schema
beast::Error StoreSqdb::update ()
{
    beast::Error error;

    beast::sqdb::transaction tr (m_session);

    // Get the version from the database
    int version (0);
    if (! error)
    {
        m_session.once (error) <<
            "SELECT "
            "  version "
            "FROM SchemaVersion WHERE "
            "  name = 'Validators' "
            ,beast::sqdb::into (version)
            ;

        if (! m_session.got_data ())
        {
            // pre-dates the "SchemaVersion" table
            version = 0;
        }
    }

    if (! error && version != currentSchemaVersion)
    {
        m_journal.info <<
            "Update database to version " << currentSchemaVersion <<
            " from version " << version;
    }

    // Update database based on version
    if (! error && version < 2)
    {
        if (! error)
            m_session.once (error) <<
                "DROP TABLE IF EXISTS ValidatorsSource";

        if (! error)
            m_session.once (error) <<
                "DROP TABLE IF EXISTS ValidatorsSourceInfo";

        if (! error)
            m_session.once (error) <<
                "DROP INDEX IF EXISTS ValidatorsSourceInfoIndex";
    }

    // Update the version to the current version
    if (! error)
    {
        int const version (currentSchemaVersion);

        m_session.once (error) <<
            "INSERT OR REPLACE INTO SchemaVersion ( "
            "  name, "
            "  version "
            ") VALUES ( "
            "  'Validators', ? "
            "); "
            ,beast::sqdb::use (version)
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

//--------------------------------------------------------------------------

beast::Error StoreSqdb::init ()
{
    beast::Error error;

    beast::sqdb::transaction tr (m_session);

    if (! error)
    {
        m_session.once (error) <<
            "PRAGMA encoding=\"UTF-8\"";
    }

    if (! error)
    {
        // This table maps component names like "Validators" to their
        // corresponding schema version number. This method allows us
        // to keep all logic data in one database, or each in its own
        // database, or in any grouping of databases, while still being
        // able to let an individual component know what version of its
        // schema it is opening.
        //
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
            "CREATE TABLE IF NOT EXISTS Validators_Source ( "
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
            "CREATE TABLE IF NOT EXISTS Validators_SourceItem ( "
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
            "  Validators_SourceItem_Index ON Validators_SourceItem "
            "  (  "
            "    sourceID "
            "  ); "
            ;
    }

#if 0
    if (! error)
    {
        m_session.once (error) <<
            "CREATE TABLE IF NOT EXISTS ValidatorsValidator ( "
            "  id               INTEGER PRIMARY KEY AUTOINCREMENT, "
            "  publicKey        TEXT UNIQUE "
            ");"
            ;
    }

    if (! error)
    {
        m_session.once (error) <<
            "CREATE TABLE IF NOT EXISTS ValidatorsValidatorStats ( "
            "  id               INTEGER PRIMARY KEY AUTOINCREMENT, "
            "  publicKey        TEXT UNIQUE "
            ");"
            ;
    }
#endif

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
