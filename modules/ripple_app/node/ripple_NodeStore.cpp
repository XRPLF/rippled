//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

Array <NodeStore::BackendFactory*> NodeStore::s_factories;

NodeStore::NodeStore (String parameters, int cacheSize, int cacheAge)
    : mCache ("NodeStore", cacheSize, cacheAge)
    , mNegativeCache ("HashedObjectNegativeCache", 0, 120)
    , mWriteGeneration (0)
    , mWriteLoad (0)
    , mWritePending (false)
    , mLevelDB (false)
    , mEphemeralDB (false)
{
    StringPairArray keyValues = parseKeyValueParameters (parameters, '|');

    String const& type = keyValues ["type"];

    if (type.isNotEmpty ())
    {
        BackendFactory* factory = nullptr;

        for (int i = 0; i < s_factories.size (); ++i)
        {
            if (s_factories [i]->getName () == type)
            {
                factory = s_factories [i];
                break;
            }
        }

        if (factory != nullptr)
        {
            m_backend = factory->createInstance (keyValues);
        }
        else
        {
            throw std::runtime_error ("unkown backend type");
        }
    }
    else
    {
        throw std::runtime_error ("missing backend type");
    }

    mWriteSet.reserve (128);

    // VFALCO TODO Eliminate usage of theConfig
    //             This can be done by passing required parameters through
    //             the backendParameters string.
    //
    if (theConfig.NODE_DB == "leveldb" || theConfig.NODE_DB == "LevelDB")
    {
        mLevelDB = true;
    }
    else if (theConfig.NODE_DB == "SQLite" || theConfig.NODE_DB == "sqlite")
    {
        mLevelDB = false;
    }
    else
    {
        WriteLog (lsFATAL, NodeObject) << "Incorrect database selection";
        assert (false);
    }

    if (!theConfig.LDB_EPHEMERAL.empty ())
    {
        // VFALCO NOTE This is cryptic
        mEphemeralDB = true;
    }
}

void NodeStore::addBackendFactory (BackendFactory& factory)
{
    s_factories.add (&factory);
}

// DEPRECATED
bool NodeStore::isLevelDB ()
{
    return mLevelDB;
}

float NodeStore::getCacheHitRate ()
{
    return mCache.getHitRate ();
}

bool NodeStore::store (NodeObjectType type, uint32 index, Blob const& data,
            uint256 const& hash)
{
    if (mLevelDB)
        return storeLevelDB (type, index, data, hash);

    return storeSQLite (type, index, data, hash);
}

NodeObject::pointer NodeStore::retrieve (uint256 const& hash)
{
    if (mLevelDB)
        return retrieveLevelDB (hash);

    return retrieveSQLite (hash);
}

void NodeStore::tune (int size, int age)
{
    mCache.setTargetSize (size);
    mCache.setTargetAge (age);
}

void NodeStore::sweep ()
{
    mCache.sweep ();
    mNegativeCache.sweep ();
}

void NodeStore::waitWrite ()
{
    boost::mutex::scoped_lock sl (mWriteMutex);
    int gen = mWriteGeneration;

    while (mWritePending && (mWriteGeneration == gen))
        mWriteCondition.wait (sl);
}

int NodeStore::getWriteLoad ()
{
    boost::mutex::scoped_lock sl (mWriteMutex);
    return std::max (mWriteLoad, static_cast<int> (mWriteSet.size ()));
}

// low-level retrieve
NodeObject::pointer NodeStore::LLRetrieve (uint256 const& hash, leveldb::DB* db)
{
    std::string sData;

    leveldb::Status st = db->Get (leveldb::ReadOptions (),
                                  leveldb::Slice (reinterpret_cast<const char*> (hash.begin ()), hash.size ()), &sData);

    if (!st.ok ())
    {
        assert (st.IsNotFound ());
        return NodeObject::pointer ();
    }

    const unsigned char* bufPtr = reinterpret_cast<const unsigned char*> (&sData[0]);
    uint32 index = htonl (*reinterpret_cast<const uint32*> (bufPtr));
    int htype = bufPtr[8];

    return boost::make_shared<NodeObject> (static_cast<NodeObjectType> (htype), index,
            bufPtr + 9, sData.size () - 9, hash);
}

// low-level write single
void NodeStore::LLWrite (boost::shared_ptr<NodeObject> ptr, leveldb::DB* db)
{
    NodeObject& obj = *ptr;
    Blob rawData (9 + obj.getData ().size ());
    unsigned char* bufPtr = &rawData.front ();

    *reinterpret_cast<uint32*> (bufPtr + 0) = ntohl (obj.getIndex ());
    *reinterpret_cast<uint32*> (bufPtr + 4) = ntohl (obj.getIndex ());
    * (bufPtr + 8) = static_cast<unsigned char> (obj.getType ());
    memcpy (bufPtr + 9, &obj.getData ().front (), obj.getData ().size ());

    leveldb::Status st = db->Put (leveldb::WriteOptions (),
                                  leveldb::Slice (reinterpret_cast<const char*> (obj.getHash ().begin ()), obj.getHash ().size ()),
                                  leveldb::Slice (reinterpret_cast<const char*> (bufPtr), rawData.size ()));

    if (!st.ok ())
    {
        WriteLog (lsFATAL, NodeObject) << "Failed to store hash node";
        assert (false);
    }
}

// low-level write set
void NodeStore::LLWrite (const std::vector< boost::shared_ptr<NodeObject> >& set, leveldb::DB* db)
{
    leveldb::WriteBatch batch;

    BOOST_FOREACH (const boost::shared_ptr<NodeObject>& it, set)
    {
        const NodeObject& obj = *it;
        Blob rawData (9 + obj.getData ().size ());
        unsigned char* bufPtr = &rawData.front ();

        *reinterpret_cast<uint32*> (bufPtr + 0) = ntohl (obj.getIndex ());
        *reinterpret_cast<uint32*> (bufPtr + 4) = ntohl (obj.getIndex ());
        * (bufPtr + 8) = static_cast<unsigned char> (obj.getType ());
        memcpy (bufPtr + 9, &obj.getData ().front (), obj.getData ().size ());

        batch.Put (leveldb::Slice (reinterpret_cast<const char*> (obj.getHash ().begin ()), obj.getHash ().size ()),
                   leveldb::Slice (reinterpret_cast<const char*> (bufPtr), rawData.size ()));
    }

    leveldb::Status st = db->Write (leveldb::WriteOptions (), &batch);

    if (!st.ok ())
    {
        WriteLog (lsFATAL, NodeObject) << "Failed to store hash node";
        assert (false);
    }
}

bool NodeStore::storeLevelDB (NodeObjectType type, uint32 index,
                                      Blob const& data, uint256 const& hash)
{
    // return: false = already in cache, true = added to cache
    if (!getApp().getHashNodeLDB ())
        return true;

    if (mCache.touch (hash))
        return false;

#ifdef PARANOID
    assert (hash == Serializer::getSHA512Half (data));
#endif

    NodeObject::pointer object = boost::make_shared<NodeObject> (type, index, data, hash);

    if (!mCache.canonicalize (hash, object))
    {
        boost::mutex::scoped_lock sl (mWriteMutex);
        mWriteSet.push_back (object);

        if (!mWritePending)
        {
            mWritePending = true;
            getApp().getJobQueue ().addJob (jtWRITE, "NodeObject::store",
                                           BIND_TYPE (&NodeStore::bulkWriteLevelDB, this, P_1));
        }
    }

    mNegativeCache.del (hash);
    return true;
}

void NodeStore::bulkWriteLevelDB (Job&)
{
    assert (mLevelDB);
    int setSize = 0;

    while (1)
    {
        std::vector< boost::shared_ptr<NodeObject> > set;
        set.reserve (128);

        {
            boost::mutex::scoped_lock sl (mWriteMutex);

            mWriteSet.swap (set);
            assert (mWriteSet.empty ());
            ++mWriteGeneration;
            mWriteCondition.notify_all ();

            if (set.empty ())
            {
                mWritePending = false;
                mWriteLoad = 0;
                return;
            }

            mWriteLoad = std::max (setSize, static_cast<int> (mWriteSet.size ()));
            setSize = set.size ();
        }

        LLWrite (set, getApp().getHashNodeLDB ());

        if (mEphemeralDB)
            LLWrite (set, getApp().getEphemeralLDB ());
    }
}

NodeObject::pointer NodeStore::retrieveLevelDB (uint256 const& hash)
{
    NodeObject::pointer obj = mCache.fetch (hash);

    if (obj || mNegativeCache.isPresent (hash) || !getApp().getHashNodeLDB ())
        return obj;

    if (mEphemeralDB)
    {
        obj = LLRetrieve (hash, getApp().getEphemeralLDB ());

        if (obj)
        {
            mCache.canonicalize (hash, obj);
            return obj;
        }
    }

    {
        LoadEvent::autoptr event (getApp().getJobQueue ().getLoadEventAP (jtHO_READ, "HOS::retrieve"));
        obj = LLRetrieve (hash, getApp().getHashNodeLDB ());

        if (!obj)
        {
            mNegativeCache.add (hash);
            return obj;
        }
    }

    mCache.canonicalize (hash, obj);

    if (mEphemeralDB)
        LLWrite (obj, getApp().getEphemeralLDB ());

    WriteLog (lsTRACE, NodeObject) << "HOS: " << hash << " fetch: in db";
    return obj;
}

bool NodeStore::storeSQLite (NodeObjectType type, uint32 index,
                                     Blob const& data, uint256 const& hash)
{
    // return: false = already in cache, true = added to cache
    if (!getApp().getHashNodeDB ())
    {
        WriteLog (lsTRACE, NodeObject) << "HOS: no db";
        return true;
    }

    if (mCache.touch (hash))
    {
        WriteLog (lsTRACE, NodeObject) << "HOS: " << hash << " store: incache";
        return false;
    }

    assert (hash == Serializer::getSHA512Half (data));

    NodeObject::pointer object = boost::make_shared<NodeObject> (type, index, data, hash);

    if (!mCache.canonicalize (hash, object))
    {
        //      WriteLog (lsTRACE, NodeObject) << "Queuing write for " << hash;
        boost::mutex::scoped_lock sl (mWriteMutex);
        mWriteSet.push_back (object);

        if (!mWritePending)
        {
            mWritePending = true;
            getApp().getJobQueue ().addJob (jtWRITE, "NodeObject::store",
                                           BIND_TYPE (&NodeStore::bulkWriteSQLite, this, P_1));
        }
    }

    //  else
    //      WriteLog (lsTRACE, NodeObject) << "HOS: already had " << hash;
    mNegativeCache.del (hash);

    return true;
}

void NodeStore::bulkWriteSQLite (Job&)
{
    assert (!mLevelDB);
    int setSize = 0;

    while (1)
    {
        std::vector< boost::shared_ptr<NodeObject> > set;
        set.reserve (128);

        {
            boost::mutex::scoped_lock sl (mWriteMutex);
            mWriteSet.swap (set);
            assert (mWriteSet.empty ());
            ++mWriteGeneration;
            mWriteCondition.notify_all ();

            if (set.empty ())
            {
                mWritePending = false;
                mWriteLoad = 0;
                return;
            }

            mWriteLoad = std::max (setSize, static_cast<int> (mWriteSet.size ()));
            setSize = set.size ();
        }
        //      WriteLog (lsTRACE, NodeObject) << "HOS: writing " << set.size();

#ifndef NO_SQLITE3_PREPARE

        if (mEphemeralDB)
            LLWrite (set, getApp().getEphemeralLDB ());

        {
            Database* db = getApp().getHashNodeDB ()->getDB ();


            // VFALCO TODO Get rid of the last parameter "aux", which is set to !theConfig.RUN_STANDALONE
            //
            static SqliteStatement pStB (db->getSqliteDB (), "BEGIN TRANSACTION;", !theConfig.RUN_STANDALONE);
            static SqliteStatement pStE (db->getSqliteDB (), "END TRANSACTION;", !theConfig.RUN_STANDALONE);
            static SqliteStatement pSt (db->getSqliteDB (),
                                        "INSERT OR IGNORE INTO CommittedObjects "
                                        "(Hash,ObjType,LedgerIndex,Object) VALUES (?, ?, ?, ?);", !theConfig.RUN_STANDALONE);

            pStB.step ();
            pStB.reset ();

            BOOST_FOREACH (const boost::shared_ptr<NodeObject>& it, set)
            {
                const char* type;

                switch (it->getType ())
                {
                case hotLEDGER:
                    type = "L";
                    break;

                case hotTRANSACTION:
                    type = "T";
                    break;

                case hotACCOUNT_NODE:
                    type = "A";
                    break;

                case hotTRANSACTION_NODE:
                    type = "N";
                    break;

                default:
                    type = "U";
                }

                pSt.bind (1, it->getHash ().GetHex ());
                pSt.bind (2, type);
                pSt.bind (3, it->getIndex ());
                pSt.bindStatic (4, it->getData ());
                int ret = pSt.step ();

                if (!pSt.isDone (ret))
                {
                    WriteLog (lsFATAL, NodeObject) << "Error saving hashed object " << ret;
                    assert (false);
                }

                pSt.reset ();
            }

            pStE.step ();
            pStE.reset ();
        }

#else

        static boost::format
        fAdd ("INSERT OR IGNORE INTO CommittedObjects "
              "(Hash,ObjType,LedgerIndex,Object) VALUES ('%s','%c','%u',%s);");

        Database* db = getApp().getHashNodeDB ()->getDB ();
        {
            ScopedLock sl (getApp().getHashNodeDB ()->getDBLock ());

            db->executeSQL ("BEGIN TRANSACTION;");

            BOOST_FOREACH (const boost::shared_ptr<NodeObject>& it, set)
            {
                char type;

                switch (it->getType ())
                {
                case hotLEDGER:
                    type = 'L';
                    break;

                case hotTRANSACTION:
                    type = 'T';
                    break;

                case hotACCOUNT_NODE:
                    type = 'A';
                    break;

                case hotTRANSACTION_NODE:
                    type = 'N';
                    break;

                default:
                    type = 'U';
                }

                db->executeSQL (boost::str (boost::format (fAdd)
                                            % it->getHash ().GetHex () % type % it->getIndex () % sqlEscape (it->getData ())));
            }

            db->executeSQL ("END TRANSACTION;");
        }
#endif

    }
}

NodeObject::pointer NodeStore::retrieveSQLite (uint256 const& hash)
{
    NodeObject::pointer obj = mCache.fetch (hash);

    if (obj)
        return obj;

    if (mNegativeCache.isPresent (hash))
        return obj;

    if (mEphemeralDB)
    {
        obj = LLRetrieve (hash, getApp().getEphemeralLDB ());

        if (obj)
        {
            mCache.canonicalize (hash, obj);
            return obj;
        }
    }

    if (!getApp().getHashNodeDB ())
        return obj;

    Blob data;
    std::string type;
    uint32 index;

#ifndef NO_SQLITE3_PREPARE
    {
        ScopedLock sl (getApp().getHashNodeDB ()->getDBLock ());
        static SqliteStatement pSt (getApp().getHashNodeDB ()->getDB ()->getSqliteDB (),
                                    "SELECT ObjType,LedgerIndex,Object FROM CommittedObjects WHERE Hash = ?;");
        LoadEvent::autoptr event (getApp().getJobQueue ().getLoadEventAP (jtDISK, "HOS::retrieve"));

        pSt.bind (1, hash.GetHex ());
        int ret = pSt.step ();

        if (pSt.isDone (ret))
        {
            pSt.reset ();
            mNegativeCache.add (hash);
            WriteLog (lsTRACE, NodeObject) << "HOS: " << hash << " fetch: not in db";
            return obj;
        }

        type = pSt.peekString (0);
        index = pSt.getUInt32 (1);
        pSt.getBlob (2).swap (data);
        pSt.reset ();
    }

#else

    std::string sql = "SELECT * FROM CommittedObjects WHERE Hash='";
    sql.append (hash.GetHex ());
    sql.append ("';");


    {
        ScopedLock sl (getApp().getHashNodeDB ()->getDBLock ());
        Database* db = getApp().getHashNodeDB ()->getDB ();

        if (!db->executeSQL (sql) || !db->startIterRows ())
        {
            sl.unlock ();
            mNegativeCache.add (hash);
            return obj;
        }

        db->getStr ("ObjType", type);
        index = db->getBigInt ("LedgerIndex");

        int size = db->getBinary ("Object", NULL, 0);
        data.resize (size);
        db->getBinary ("Object", & (data.front ()), size);
        db->endIterRows ();
    }
#endif

#ifdef PARANOID
    assert (Serializer::getSHA512Half (data) == hash);
#endif

    NodeObjectType htype = hotUNKNOWN;

    switch (type[0])
    {
    case 'L':
        htype = hotLEDGER;
        break;

    case 'T':
        htype = hotTRANSACTION;
        break;

    case 'A':
        htype = hotACCOUNT_NODE;
        break;

    case 'N':
        htype = hotTRANSACTION_NODE;
        break;

    default:
        assert (false);
        WriteLog (lsERROR, NodeObject) << "Invalid hashed object";
        mNegativeCache.add (hash);
        return obj;
    }

    obj = boost::make_shared<NodeObject> (htype, index, data, hash);
    mCache.canonicalize (hash, obj);

    if (mEphemeralDB)
        LLWrite (obj, getApp().getEphemeralLDB ());

    WriteLog (lsTRACE, NodeObject) << "HOS: " << hash << " fetch: in db";
    return obj;
}

int NodeStore::import (const std::string& file)
{
    WriteLog (lsWARNING, NodeObject) << "Hashed object import from \"" << file << "\".";
    UPTR_T<Database> importDB (new SqliteDatabase (file.c_str ()));
    importDB->connect ();

    leveldb::DB* db = getApp().getHashNodeLDB ();
    leveldb::WriteOptions wo;

    int count = 0;

    SQL_FOREACH (importDB, "SELECT * FROM CommittedObjects;")
    {
        uint256 hash;
        std::string hashStr;
        importDB->getStr ("Hash", hashStr);
        hash.SetHexExact (hashStr);

        if (hash.isZero ())
        {
            WriteLog (lsWARNING, NodeObject) << "zero hash found in import table";
        }
        else
        {
            Blob rawData;
            int size = importDB->getBinary ("Object", NULL, 0);
            rawData.resize (9 + size);
            unsigned char* bufPtr = &rawData.front ();

            importDB->getBinary ("Object", bufPtr + 9, size);

            uint32 index = importDB->getBigInt ("LedgerIndex");
            *reinterpret_cast<uint32*> (bufPtr + 0) = ntohl (index);
            *reinterpret_cast<uint32*> (bufPtr + 4) = ntohl (index);

            std::string type;
            importDB->getStr ("ObjType", type);
            NodeObjectType htype = hotUNKNOWN;

            switch (type[0])
            {
            case 'L':
                htype = hotLEDGER;
                break;

            case 'T':
                htype = hotTRANSACTION;
                break;

            case 'A':
                htype = hotACCOUNT_NODE;
                break;

            case 'N':
                htype = hotTRANSACTION_NODE;
                break;

            default:
                assert (false);
                WriteLog (lsERROR, NodeObject) << "Invalid hashed object";
            }

            * (bufPtr + 8) = static_cast<unsigned char> (htype);

            leveldb::Status st = db->Put (wo,
                                          leveldb::Slice (reinterpret_cast<const char*> (hash.begin ()), hash.size ()),
                                          leveldb::Slice (reinterpret_cast<const char*> (bufPtr), rawData.size ()));

            if (!st.ok ())
            {
                WriteLog (lsFATAL, NodeObject) << "Failed to store hash node";
                assert (false);
            }

            ++count;
        }

        if ((count % 10000) == 0)
        {
            WriteLog (lsINFO, NodeObject) << "Import in progress: " << count;
        }
    }

    WriteLog (lsWARNING, NodeObject) << "Imported " << count << " nodes";
    return count;
}
