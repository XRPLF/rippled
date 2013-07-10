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

bool NodeStore::store (NodeObjectType type, uint32 index,
                                      Blob const& data, uint256 const& hash)
{
    // return: false = already in cache, true = added to cache
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
                                           BIND_TYPE (&NodeStore::bulkWrite, this, P_1));
        }
    }

    mNegativeCache.del (hash);
    return true;
}

void NodeStore::bulkWrite (Job&)
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

        m_backend->bulkStore (set);

        if (m_backendFast)
            m_backendFast->bulkStore (set);
    }
}

NodeObject::pointer NodeStore::retrieve (uint256 const& hash)
{
    NodeObject::pointer obj = mCache.fetch (hash);

    if (obj || mNegativeCache.isPresent (hash) || !getApp().getHashNodeLDB ())
        return obj;

    if (m_backendFast)
    {
        obj = m_backendFast->retrieve (hash);

        if (obj)
        {
            mCache.canonicalize (hash, obj);
            return obj;
        }
    }

    {
        LoadEvent::autoptr event (getApp().getJobQueue ().getLoadEventAP (jtHO_READ, "HOS::retrieve"));
        obj = m_backend->retrieve(hash);

        if (!obj)
        {
            mNegativeCache.add (hash);
            return obj;
        }
    }

    mCache.canonicalize (hash, obj);

    if (m_backendFast)
        m_backendFast->store(obj);

    WriteLog (lsTRACE, NodeObject) << "HOS: " << hash << " fetch: in db";
    return obj;
}
