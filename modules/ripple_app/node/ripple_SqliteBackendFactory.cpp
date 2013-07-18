//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

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

static int s_nodeStoreDBCount = NUMBER (s_nodeStoreDBInit);

class SqliteBackendFactory::Backend : public NodeStore::Backend
{
public:
    Backend (size_t keyBytes, std::string const& path)
        : m_keyBytes (keyBytes)
        , m_name (path)
        , m_db (new DatabaseCon(path, s_nodeStoreDBInit, s_nodeStoreDBCount))
    {
        String s;

        // VFALCO TODO Remove this dependency on theConfig
        //
        s << "PRAGMA cache_size=-" << String (theConfig.getSize(siHashNodeDBCache) * 1024);
        m_db->getDB()->executeSQL (s.toStdString ().c_str ());

        //m_db->getDB()->executeSQL (boost::str (boost::format ("PRAGMA cache_size=-%d;") %
        //    (theConfig.getSize(siHashNodeDBCache) * 1024)));
    }

    ~Backend()
    {
        delete m_db;
    }

    std::string getDataBaseName()
    {
        return m_name;
    }

    bool bulkStore (const std::vector< NodeObject::pointer >& objects)
    {
        ScopedLock sl(m_db->getDBLock());
        static SqliteStatement pStB(m_db->getDB()->getSqliteDB(), "BEGIN TRANSACTION;");
        static SqliteStatement pStE(m_db->getDB()->getSqliteDB(), "END TRANSACTION;");
        static SqliteStatement pSt(m_db->getDB()->getSqliteDB(),
            "INSERT OR IGNORE INTO CommittedObjects "
                "(Hash,ObjType,LedgerIndex,Object) VALUES (?, ?, ?, ?);");

        pStB.step();
        pStB.reset();

        BOOST_FOREACH(NodeObject::ref object, objects)
        {
            bind(pSt, object);
            pSt.step();
            pSt.reset();
        }

        pStE.step();
        pStE.reset();

        return true;

    }

    NodeObject::pointer retrieve(uint256 const& hash)
    {
        NodeObject::pointer ret;

        {
            ScopedLock sl(m_db->getDBLock());
            static SqliteStatement pSt(m_db->getDB()->getSqliteDB(),
                "SELECT ObjType,LedgerIndex,Object FROM CommittedObjects WHERE Hash = ?;");

            pSt.bind(1, hash.GetHex());

            if (pSt.isRow(pSt.step()))
                ret = boost::make_shared<NodeObject>(getType(pSt.peekString(0)), pSt.getUInt32(1), pSt.getBlob(2), hash);

            pSt.reset();
        }

        return ret;
    }

    void visitAll(FUNCTION_TYPE<void (NodeObject::pointer)> func)
    {
        uint256 hash;

        static SqliteStatement pSt(m_db->getDB()->getSqliteDB(),
            "SELECT ObjType,LedgerIndex,Object,Hash FROM CommittedObjects;");

        while (pSt.isRow(pSt.step()))
        {
            hash.SetHexExact(pSt.getString(3));
            func(boost::make_shared<NodeObject>(getType(pSt.peekString(0)), pSt.getUInt32(1), pSt.getBlob(2), hash));
        }

        pSt.reset();
    }

    void bind(SqliteStatement& statement, NodeObject::ref object)
    {
        char const* type;
        switch (object->getType())
        {
            case hotLEDGER:                type = "L"; break;
            case hotTRANSACTION:        type = "T"; break;
            case hotACCOUNT_NODE:        type = "A"; break;
            case hotTRANSACTION_NODE:    type = "N"; break;
            default:                    type = "U";
        }

        statement.bind(1, object->getHash().GetHex());
        statement.bind(2, type);
        statement.bind(3, object->getIndex());
        statement.bindStatic(4, object->getData());
    }

    NodeObjectType getType(std::string const& type)
    {
        NodeObjectType htype = hotUNKNOWN;
        if (!type.empty())
        {
            switch (type[0])
            {
                case 'L': htype = hotLEDGER; break;
                case 'T': htype = hotTRANSACTION; break;
                case 'A': htype = hotACCOUNT_NODE; break;
                case 'N': htype = hotTRANSACTION_NODE; break;
            }
        }
        return htype;
    }

private:
    size_t const m_keyBytes;
    std::string const m_name;
    ScopedPointer <DatabaseCon> m_db;
};

//------------------------------------------------------------------------------

SqliteBackendFactory::SqliteBackendFactory ()
{
}

SqliteBackendFactory::~SqliteBackendFactory ()
{
}

SqliteBackendFactory& SqliteBackendFactory::getInstance ()
{
    static SqliteBackendFactory instance;

    return instance;
}

String SqliteBackendFactory::getName () const
{
    return "Sqlite";
}

NodeStore::Backend* SqliteBackendFactory::createInstance (size_t keyBytes, StringPairArray const& keyValues)
{
    return new Backend (keyBytes, keyValues ["path"].toStdString ());
}
