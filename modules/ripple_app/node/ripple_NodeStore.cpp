//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

//
// NodeStore::Backend
//

NodeStore::Backend::Backend ()
    : mWriteGeneration(0)
    , mWriteLoad(0)
    , mWritePending(false)
{
    mWriteSet.reserve (bulkWriteBatchSize);
}

bool NodeStore::Backend::store (NodeObject::ref object)
{
    boost::mutex::scoped_lock sl (mWriteMutex);
    mWriteSet.push_back (object);

    if (!mWritePending)
    {
        mWritePending = true;

        // VFALCO TODO Eliminate this dependency on the Application object.
        getApp().getJobQueue ().addJob (
            jtWRITE,
            "NodeObject::store",
            BIND_TYPE (&NodeStore::Backend::bulkWrite, this, P_1));
    }
    return true;
}

void NodeStore::Backend::bulkWrite (Job &)
{
    int setSize = 0;

    // VFALCO NOTE Use the canonical for(;;) instead.
    //             Or better, provide a proper terminating condition.
    while (1)
    {
        std::vector< boost::shared_ptr<NodeObject> > set;
        set.reserve (bulkWriteBatchSize);

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

                // VFALCO NOTE Fix this function to not return from the middle
                return;
            }

            mWriteLoad = std::max (setSize, static_cast<int> (mWriteSet.size ()));
            setSize = set.size ();
        }

        bulkStore (set);
    }
}

// VFALCO TODO This function should not be needed. Instead, the
//             destructor should handle flushing of the bulk write buffer.
//
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

//------------------------------------------------------------------------------

//
// NodeStore
//

Array <NodeStore::BackendFactory*> NodeStore::s_factories;

NodeStore::NodeStore (String backendParameters,
                      String fastBackendParameters,
                      int cacheSize,
                      int cacheAge)
    : m_backend (createBackend (backendParameters))
    , m_fastBackend (fastBackendParameters.isNotEmpty () ? createBackend (fastBackendParameters)
                                                         : nullptr)
    , m_cache ("NodeStore", cacheSize, cacheAge)
    , m_negativeCache ("NoteStoreNegativeCache", 0, 120)
{
}

void NodeStore::addBackendFactory (BackendFactory& factory)
{
    s_factories.add (&factory);
}

float NodeStore::getCacheHitRate ()
{
    return m_cache.getHitRate ();
}

void NodeStore::tune (int size, int age)
{
    m_cache.setTargetSize (size);
    m_cache.setTargetAge (age);
}

void NodeStore::sweep ()
{
    m_cache.sweep ();
    m_negativeCache.sweep ();
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
    bool wasStored = false;

    bool const keyFoundAndObjectCached = m_cache.refreshIfPresent (hash);

    // VFALCO NOTE What happens if the key is found, but the object
    //             fell out of the cache? We will end up passing it
    //             to the backend anyway.
    //      
    if (! keyFoundAndObjectCached)
    {

// VFALCO TODO Rename this to RIPPLE_NODESTORE_VERIFY_HASHES and make
//             it be 1 or 0 instead of merely defined or undefined.
//
#ifdef PARANOID
        assert (hash == Serializer::getSHA512Half (data));
#endif

        NodeObject::pointer object = boost::make_shared<NodeObject> (type, index, data, hash);

        // VFALCO NOTE What does it mean to canonicalize an object?
        //
        if (!m_cache.canonicalize (hash, object))
        {
            m_backend->store (object);

            if (m_fastBackend)
                m_fastBackend->store (object);
        }

        m_negativeCache.del (hash);

        wasStored = true;
    }

    return wasStored;
}

NodeObject::pointer NodeStore::retrieve (uint256 const& hash)
{
    NodeObject::pointer obj = m_cache.fetch (hash);

    if (obj || m_negativeCache.isPresent (hash))
        return obj;

    if (m_fastBackend)
    {
        obj = m_fastBackend->retrieve (hash);

        if (obj)
        {
            m_cache.canonicalize (hash, obj);
            return obj;
        }
    }

    {
        LoadEvent::autoptr event (getApp().getJobQueue ().getLoadEventAP (jtHO_READ, "HOS::retrieve"));
        obj = m_backend->retrieve(hash);

        if (!obj)
        {
            m_negativeCache.add (hash);
            return obj;
        }
    }

    m_cache.canonicalize (hash, obj);

    if (m_fastBackend)
        m_fastBackend->store(obj);

    WriteLog (lsTRACE, NodeObject) << "HOS: " << hash << " fetch: in db";

    return obj;
}

void NodeStore::importVisitor (
    std::vector <NodeObject::pointer>& objects,
    NodeObject::pointer object)
{
    if (objects.size() >= bulkWriteBatchSize)
    {
        m_backend->bulkStore (objects);

        objects.clear ();
        objects.reserve (bulkWriteBatchSize);
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

    objects.reserve (bulkWriteBatchSize);

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
