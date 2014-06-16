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

#include <ripple/module/app/node/SqliteFactory.h>

namespace ripple {

static const char* s_nodeStoreDBInit [] =
{
    "PRAGMA synchronous=NORMAL;",
    "PRAGMA journal_mode=WAL;",
    "PRAGMA journal_size_limit=1582080;",

#if (ULONG_MAX > UINT_MAX) && !defined (NO_SQLITE_MMAP)
    "PRAGMA mmap_size=171798691840;",
#endif

    "BEGIN TRANSACTION;",

    "CREATE TABLE CommittedObjects (				\
        Hash		CHARACTER(64) PRIMARY KEY,		\
        ObjType		CHAR(1)	NOT	NULL,				\
        LedgerIndex	BIGINT UNSIGNED,				\
        Object		BLOB							\
    );",

    "END TRANSACTION;"
};

static int s_nodeStoreDBCount = RIPPLE_ARRAYSIZE (s_nodeStoreDBInit);

//------------------------------------------------------------------------------

class SqliteBackend : public NodeStore::Backend
{
public:
    explicit SqliteBackend (std::string const& path)
        : m_name (path)
        , m_db (new DatabaseCon(path, s_nodeStoreDBInit, s_nodeStoreDBCount))
    {
        beast::String s;

        // VFALCO TODO Remove this dependency on theConfig
        //
        s << "PRAGMA cache_size=-" << beast::String (getConfig ().getSize(siHashNodeDBCache) * 1024);
        m_db->getDB()->executeSQL (s.toStdString ().c_str ());
    }

    ~SqliteBackend()
    {
    }

    std::string getName()
    {
        return m_name;
    }

    //--------------------------------------------------------------------------

    NodeStore::Status fetch (void const* key, NodeObject::Ptr* pObject)
    {
        NodeStore::Status result = NodeStore::ok;

        pObject->reset ();

        {
            DeprecatedScopedLock sl (m_db->getDBLock());

            uint256 const hash (uint256::fromVoid (key));

            static SqliteStatement pSt (m_db->getDB()->getSqliteDB(),
                "SELECT ObjType,LedgerIndex,Object FROM CommittedObjects WHERE Hash = ?;");

            pSt.bind (1, to_string (hash));

            if (pSt.isRow (pSt.step()))
            {
                // VFALCO NOTE This is unfortunately needed,
                //             the DatabaseCon creates the blob?
                Blob data (pSt.getBlob (2));
                *pObject = NodeObject::createObject (
                    getTypeFromString (pSt.peekString (0)),
                    pSt.getUInt32 (1),
                    std::move(data),
                    hash);
            }
            else
            {
                result = NodeStore::notFound;
            }

            pSt.reset();
        }

        return result;
    }

    void store (NodeObject::ref object)
    {
        NodeStore::Batch batch;

        batch.push_back (object);

        storeBatch (batch);
    }

    void storeBatch (NodeStore::Batch const& batch)
    {
        // VFALCO TODO Rewrite this to use Beast::db

        DeprecatedScopedLock sl (m_db->getDBLock());

        static SqliteStatement pStB (m_db->getDB()->getSqliteDB(), "BEGIN TRANSACTION;");
        static SqliteStatement pStE (m_db->getDB()->getSqliteDB(), "END TRANSACTION;");
        static SqliteStatement pSt (m_db->getDB()->getSqliteDB(),
            "INSERT OR IGNORE INTO CommittedObjects "
                "(Hash,ObjType,LedgerIndex,Object) VALUES (?, ?, ?, ?);");

        pStB.step();
        pStB.reset();

        BOOST_FOREACH (NodeObject::Ptr const& object, batch)
        {
            doBind (pSt, object);

            pSt.step();
            pSt.reset();
        }

        pStE.step();
        pStE.reset();
    }

    void for_each (std::function <void(NodeObject::Ptr)> f)
    {
        // No lock needed as per the for_each() API

        uint256 hash;

        static SqliteStatement pSt(m_db->getDB()->getSqliteDB(),
            "SELECT ObjType,LedgerIndex,Object,Hash FROM CommittedObjects;");

        while (pSt.isRow (pSt.step()))
        {
            hash.SetHexExact(pSt.getString(3));

            // VFALCO NOTE This is unfortunately needed,
            //             the DatabaseCon creates the blob?
            Blob data (pSt.getBlob (2));
            NodeObject::Ptr const object (NodeObject::createObject (
                getTypeFromString (pSt.peekString (0)),
                pSt.getUInt32 (1),
                std::move(data),
                hash));

            f (object);
        }

        pSt.reset ();
    }

    int getWriteLoad ()
    {
        return 0;
    }

    //--------------------------------------------------------------------------

    void doBind (SqliteStatement& statement, NodeObject::ref object)
    {
        char const* type;
        switch (object->getType())
        {
            case hotLEDGER:             type = "L"; break;
            case hotTRANSACTION:        type = "T"; break;
            case hotACCOUNT_NODE:       type = "A"; break;
            case hotTRANSACTION_NODE:   type = "N"; break;
            default:                    type = "U";
        }

        statement.bind(1, to_string (object->getHash()));
        statement.bind(2, type);
        statement.bind(3, object->getLedgerIndex());
        statement.bindStatic(4, object->getData());
    }

    NodeObjectType getTypeFromString (std::string const& s)
    {
        NodeObjectType type = hotUNKNOWN;

        if (!s.empty ())
        {
            switch (s [0])
            {
                case 'L': type = hotLEDGER; break;
                case 'T': type = hotTRANSACTION; break;
                case 'A': type = hotACCOUNT_NODE; break;
                case 'N': type = hotTRANSACTION_NODE; break;
            }
        }
        return type;
    }

private:
    std::string const m_name;
    std::unique_ptr <DatabaseCon> m_db;
};

//------------------------------------------------------------------------------

class SqliteFactory : public NodeStore::Factory
{
public:
    beast::String getName () const
    {
        return "Sqlite";
    }

    std::unique_ptr <NodeStore::Backend> createInstance (
        size_t, NodeStore::Parameters const& keyValues,
            NodeStore::Scheduler&, beast::Journal)
    {
        return std::make_unique <SqliteBackend> (keyValues ["path"].toStdString ());
    }
};

//------------------------------------------------------------------------------

std::unique_ptr <NodeStore::Factory> make_SqliteFactory ()
{
    return std::make_unique <SqliteFactory> ();
}

}
