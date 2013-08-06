//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

Array <NodeStore::BackendFactory*> NodeStore::s_factories;

NodeStore::NodeStore (String backendParameters, String fastBackendParameters, int cacheSize, int cacheAge)
    : m_backend (createBackend (backendParameters))
    , mCache ("NodeStore", cacheSize, cacheAge)
    , mNegativeCache ("HashedObjectNegativeCache", 0, 120)
{
    if (fastBackendParameters.isNotEmpty ())
        m_fastBackend = createBackend (fastBackendParameters);
}

void NodeStore::addBackendFactory (BackendFactory& factory)
{
    s_factories.add (&factory);
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
    m_backend->waitWrite ();
    if (m_fastBackend)
        m_fastBackend->waitWrite ();
}

int NodeStore::getWriteLoad ()
{
    return m_backend->getWriteLoad ();
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
        m_backend->store (object);
        if (m_fastBackend)
            m_fastBackend->store (object);
    }

    mNegativeCache.del (hash);
    return true;
}

NodeObject::pointer NodeStore::retrieve (uint256 const& hash)
{
    NodeObject::pointer obj = mCache.fetch (hash);

    if (obj || mNegativeCache.isPresent (hash))
        return obj;

    if (m_fastBackend)
    {
        obj = m_fastBackend->retrieve (hash);

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

    if (m_fastBackend)
        m_fastBackend->store(obj);

    WriteLog (lsTRACE, NodeObject) << "HOS: " << hash << " fetch: in db";
    return obj;
}

void NodeStore::importVisitor (
    std::vector <NodeObject::pointer>& objects,
    NodeObject::pointer object)
{
    if (objects.size() >= 128)
    {
        m_backend->bulkStore (objects);

        objects.clear ();
        objects.reserve (128);
    }

    objects.push_back (object);
}

int NodeStore::import (String sourceBackendParameters)
{
    ScopedPointer <NodeStore::Backend> srcBackend (createBackend (sourceBackendParameters));

    WriteLog (lsWARNING, NodeObject) <<
        "Node import from '" << srcBackend->getDataBaseName() << "' to '"
                             << m_backend->getDataBaseName() << "'.";

    std::vector <NodeObject::pointer> objects;

    objects.reserve (128);

    srcBackend->visitAll (BIND_TYPE (&NodeStore::importVisitor, this, boost::ref (objects), P_1));

    if (!objects.empty ())
        m_backend->bulkStore (objects);

    return 0;
}

NodeStore::Backend* NodeStore::createBackend (String const& parameters)
{
    Backend* backend = nullptr;

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
            backend = factory->createInstance (keyValues);
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

    return backend;
}

bool NodeStore::Backend::store (NodeObject::ref object)
{
    boost::mutex::scoped_lock sl (mWriteMutex);
    mWriteSet.push_back (object);

    if (!mWritePending)
    {
        mWritePending = true;
        getApp().getJobQueue ().addJob (jtWRITE, "NodeObject::store",
				       BIND_TYPE (&NodeStore::Backend::bulkWrite, this, P_1));
    }
    return true;
}

void NodeStore::Backend::bulkWrite (Job &)
{
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

        bulkStore (set);
    }
}

void NodeStore::Backend::waitWrite ()
{
    boost::mutex::scoped_lock sl (mWriteMutex);
    int gen = mWriteGeneration;

    while (mWritePending && (mWriteGeneration == gen))
        mWriteCondition.wait (sl);
}

int NodeStore::Backend::getWriteLoad ()
{
    boost::mutex::scoped_lock sl (mWriteMutex);

    return std::max (mWriteLoad, static_cast<int> (mWriteSet.size ()));
}
