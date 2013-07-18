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

class NodeStoreImp : public NodeStore
{
public:
    /** Size of a key.
    */
    enum
    {
        keyBytes = 32
    };

    /** Parsed key/value blob into NodeObject components.

        This will extract the information required to construct
        a NodeObject. It also does consistency checking and returns
        the result, so it is possible to determine if the data
        is corrupted without throwing an exception. Note all forms
        of corruption are detected so further analysis will be
        needed to eliminate false positives.

        This is the format in which a NodeObject is stored in the
        persistent storage layer.
    */
    struct DecodedBlob
    {
        /** Construct the decoded blob from raw data.

            The `success` member will indicate if the operation was succesful.
        */
        DecodedBlob (void const* keyParam, void const* value, int valueBytes)
        {
            /*  Data format:

                Bytes

                0...3       LedgerIndex     32-bit big endian integer
                4...7       Unused?         An unused copy of the LedgerIndex
                8           char            One of NodeObjectType
                9...end                     The body of the object data
            */

            success = false;
            key = keyParam;
            // VFALCO NOTE Ledger indexes should have started at 1
            ledgerIndex = LedgerIndex (-1);
            objectType = hotUNKNOWN;
            objectData = nullptr;
            dataBytes = bmax (0, valueBytes - 9);

            if (dataBytes > 4)
            {
                LedgerIndex const* index = static_cast <LedgerIndex const*> (value);
                ledgerIndex = ByteOrder::swapIfLittleEndian (*index);
            }

            // VFALCO NOTE What about bytes 4 through 7 inclusive?

            if (dataBytes > 8)
            {
                unsigned char const* byte = static_cast <unsigned char const*> (value);
                objectType = static_cast <NodeObjectType> (byte [8]);
            }

            if (dataBytes > 9)
            {
                objectData = static_cast <unsigned char const*> (value) + 9;

                switch (objectType)
                {
                case hotUNKNOWN:
                default:
                    break;

                case hotLEDGER:
                case hotTRANSACTION:
                case hotACCOUNT_NODE:
                case hotTRANSACTION_NODE:
                    success = true;
                    break;
                }
            }
        }

        /** Create a NodeObject from this data.
        */
        NodeObject::pointer createObject ()
        {
            NodeObject::pointer object;

            if (success)
            {
                // VFALCO NOTE I dislke these shared pointers from boost
                object = boost::make_shared <NodeObject> (
                    objectType, ledgerIndex, objectData, dataBytes, uint256 (key));
            }

            return object;
        }

        bool success;

        void const* key;
        LedgerIndex ledgerIndex;
        NodeObjectType objectType;
        unsigned char const* objectData;
        int dataBytes;
    };

    //--------------------------------------------------------------------------

    class EncodedBlob
    {
        HeapBlock <char> data;
    };

public:
    NodeStoreImp (String backendParameters,
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

    ~NodeStoreImp ()
    {
        // VFALCO NOTE This shouldn't be necessary, the backend can
        //             just handle it in the destructor.
        //
        m_backend->waitWrite ();

        if (m_fastBackend)
            m_fastBackend->waitWrite ();
    }

    float getCacheHitRate ()
    {
        return m_cache.getHitRate ();
    }

    void tune (int size, int age)
    {
        m_cache.setTargetSize (size);
        m_cache.setTargetAge (age);
    }

    void sweep ()
    {
        m_cache.sweep ();
        m_negativeCache.sweep ();
    }

    int getWriteLoad ()
    {
        return m_backend->getWriteLoad ();
    }

    bool store (NodeObjectType type,
                uint32 index,
                Blob const& data,
                uint256 const& hash)
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

            NodeObject::pointer object = boost::make_shared <NodeObject> (type, index, data, hash);

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

    //------------------------------------------------------------------------------

    NodeObject::pointer retrieve (uint256 const& hash)
    {
        // See if the object already exists in the cache
        //
        NodeObject::pointer obj = m_cache.fetch (hash);

        if (obj == nullptr)
        {
            // It's not in the cache, see if we can skip checking the db.
            //
            if (! m_negativeCache.isPresent (hash))
            {
                // There's still a chance it could be in one of the databases.

                bool foundInFastBackend = false;

                // Check the fast backend database if we have one
                //
                if (m_fastBackend != nullptr)
                {
                    obj = retrieveInternal (m_fastBackend, hash);

                    // If we found the object, avoid storing it again later.
                    if (obj != nullptr)
                        foundInFastBackend = true;
                }

                // Are we still without an object?
                //
                if (obj == nullptr)
                {
                    // Yes so at last we will try the main database.
                    //
                    {
                        // Monitor this operation's load since it is expensive.

                        // m_hooks->onRetrieveBegin ()

                        // VFALCO TODO Why is this an autoptr? Why can't it just be a plain old object?
                        //
                        LoadEvent::autoptr event (getApp().getJobQueue ().getLoadEventAP (jtHO_READ, "HOS::retrieve"));

                        obj = retrieveInternal (m_backend, hash);

                        // m_hooks->onRetrieveEnd ()
                    }

                    // If it's not in the main database, remember that so we
                    // can skip the lookup for the same object again later.
                    //
                    if (obj == nullptr)
                        m_negativeCache.add (hash);
                }

                // Did we finally get something?
                //
                if (obj != nullptr)
                {
                    // Yes it so canonicalize. This solves the problem where
                    // more than one thread has its own copy of the same object.
                    //
                    m_cache.canonicalize (hash, obj);

                    if (! foundInFastBackend)
                    {
                        // If we have a fast back end, store it there for later.
                        //
                        if (m_fastBackend != nullptr)
                            m_fastBackend->store (obj);

                        // Since this was a 'hard' fetch, we will log it.
                        //
                        WriteLog (lsTRACE, NodeObject) << "HOS: " << hash << " fetch: in db";
                    }
                }
            }
            else
            {
                // hash is known not to be in the database
            }
        }
        else
        {
            // found it!
        }

        return obj;
    }

    NodeObject::pointer retrieveInternal (Backend* backend, uint256 const& hash)
    {
        // VFALCO TODO Make this not allocate and free on each call
        //
        struct MyGetCallback : Backend::GetCallback
        {
            void* getStorageForValue (size_t sizeInBytes)
            {
                bytes = sizeInBytes;
                data.malloc (sizeInBytes);

                return &data [0];
            }

            size_t bytes;
            HeapBlock <char> data;
        };

        NodeObject::pointer object;

        MyGetCallback cb;
        Backend::Status const status = backend->get (hash.begin (), &cb);

        if (status == Backend::ok)
        {
            // Deserialize the payload into its components.
            //
            DecodedBlob decoded (hash.begin (), cb.data.getData (), cb.bytes);

            if (decoded.success)
            {
                object = decoded.createObject ();
            }
            else
            {
                // Houston, we've had a problem. Data is likely corrupt.

                // VFALCO TODO Deal with encountering corrupt data!

                WriteLog (lsFATAL, NodeObject) << "Corrupt NodeObject #" << hash;
            }
        }

        return object;
    }

    //------------------------------------------------------------------------------

    void importVisitor (
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

    int import (String sourceBackendParameters)
    {
        ScopedPointer <NodeStore::Backend> srcBackend (createBackend (sourceBackendParameters));

        WriteLog (lsWARNING, NodeObject) <<
            "Node import from '" << srcBackend->getDataBaseName() << "' to '"
                                 << m_backend->getDataBaseName() << "'.";

        std::vector <NodeObject::pointer> objects;

        objects.reserve (bulkWriteBatchSize);

        srcBackend->visitAll (BIND_TYPE (&NodeStoreImp::importVisitor, this, boost::ref (objects), P_1));

        if (!objects.empty ())
            m_backend->bulkStore (objects);

        return 0;
    }

    NodeStore::Backend* createBackend (String const& parameters)
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
                backend = factory->createInstance (keyBytes, keyValues);
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

    static void addBackendFactory (BackendFactory& factory)
    {
        s_factories.add (&factory);
    }

private:
    static Array <NodeStore::BackendFactory*> s_factories;

    RecycledObjectPool <EncodedBlob> m_blobPool;

    // Persistent key/value storage.
    ScopedPointer <Backend> m_backend;

    // Larger key/value storage, but not necessarily persistent.
    ScopedPointer <Backend> m_fastBackend;

    // VFALCO NOTE What are these things for? We need comments.
    TaggedCache <uint256, NodeObject, UptimeTimerAdapter> m_cache;
    KeyCache <uint256, UptimeTimerAdapter> m_negativeCache;
};

Array <NodeStore::BackendFactory*> NodeStoreImp::s_factories;

//------------------------------------------------------------------------------

void NodeStore::addBackendFactory (BackendFactory& factory)
{
    NodeStoreImp::addBackendFactory (factory);
}

NodeStore* NodeStore::New (String backendParameters,
                           String fastBackendParameters,
                           int cacheSize,
                           int cacheAge)
{
    return new NodeStoreImp (backendParameters,
                             fastBackendParameters,
                             cacheSize,
                             cacheAge);
}

//------------------------------------------------------------------------------

class NodeStoreTests : public UnitTest
{
public:
    enum
    {
        maxPayloadBytes = 1000,

        numObjects = 1000
    };

    NodeStoreTests () : UnitTest ("NodeStore")
    {
    }

    // Create a pseudo-random object
    static NodeObject* createNodeObject (int index, int64 seedValue, HeapBlock <char>& payloadBuffer)
    {
        Random r (seedValue + index);

        NodeObjectType type;
        switch (r.nextInt (4))
        {
        case 0: type = hotLEDGER; break;
        case 1: type = hotTRANSACTION; break;
        case 2: type = hotACCOUNT_NODE; break;
        case 3: type = hotTRANSACTION_NODE; break;
        default:
            type = hotUNKNOWN;
            break;
        };

        LedgerIndex ledgerIndex = 1 + r.nextInt (1024 * 1024);

        uint256 hash;
        r.nextBlob (hash.begin (), hash.size ());

        int payloadBytes = 1 + r.nextInt (maxPayloadBytes);
        r.nextBlob (payloadBuffer.getData (), payloadBytes);

        return new NodeObject (type, ledgerIndex, payloadBuffer.getData (), payloadBytes, hash);
    }

    void runTest ()
    {
        beginTest ("create");

        int64 const seedValue = 50;

        HeapBlock <char> payloadBuffer (maxPayloadBytes);

        for (int i = 0; i < numObjects; ++i)
        {
            ScopedPointer <NodeObject> object (createNodeObject (i, seedValue, payloadBuffer));
        }
    }
};

static NodeStoreTests nodeStoreTests;
