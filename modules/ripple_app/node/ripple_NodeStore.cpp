//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

NodeStore::DecodedBlob::DecodedBlob (void const* key, void const* value, int valueBytes)
{
    /*  Data format:

        Bytes

        0...3       LedgerIndex     32-bit big endian integer
        4...7       Unused?         An unused copy of the LedgerIndex
        8           char            One of NodeObjectType
        9...end                     The body of the object data
    */

    m_success = false;
    m_key = key;
    // VFALCO NOTE Ledger indexes should have started at 1
    m_ledgerIndex = LedgerIndex (-1);
    m_objectType = hotUNKNOWN;
    m_objectData = nullptr;
    m_dataBytes = bmax (0, valueBytes - 9);

    if (valueBytes > 4)
    {
        LedgerIndex const* index = static_cast <LedgerIndex const*> (value);
        m_ledgerIndex = ByteOrder::swapIfLittleEndian (*index);
    }

    // VFALCO NOTE What about bytes 4 through 7 inclusive?

    if (valueBytes > 8)
    {
        unsigned char const* byte = static_cast <unsigned char const*> (value);
        m_objectType = static_cast <NodeObjectType> (byte [8]);
    }

    if (valueBytes > 9)
    {
        m_objectData = static_cast <unsigned char const*> (value) + 9;

        switch (m_objectType)
        {
        case hotUNKNOWN:
        default:
            break;

        case hotLEDGER:
        case hotTRANSACTION:
        case hotACCOUNT_NODE:
        case hotTRANSACTION_NODE:
            m_success = true;
            break;
        }
    }
}

NodeObject::Ptr NodeStore::DecodedBlob::createObject ()
{
    bassert (m_success);

    NodeObject::Ptr object;

    if (m_success)
    {
        Blob data (m_dataBytes);

        memcpy (data.data (), m_objectData, m_dataBytes);

        object = NodeObject::createObject (
            m_objectType, m_ledgerIndex, data, uint256::fromVoid (m_key));
    }

    return object;
}

//------------------------------------------------------------------------------

void NodeStore::EncodedBlob::prepare (NodeObject::Ptr const& object)
{
    m_key = object->getHash ().begin ();

    // This is how many bytes we need in the flat data
    m_size = object->getData ().size () + 9;

    m_data.ensureSize (m_size);

    // These sizes must be the same!
    static_bassert (sizeof (uint32) == sizeof (object->getIndex ()));

    {
        uint32* buf = static_cast <uint32*> (m_data.getData ());

        buf [0] = ByteOrder::swapIfLittleEndian (object->getIndex ());
        buf [1] = ByteOrder::swapIfLittleEndian (object->getIndex ());
    }

    {
        unsigned char* buf = static_cast <unsigned char*> (m_data.getData ());

        buf [8] = static_cast <unsigned char> (object->getType ());

        memcpy (&buf [9], object->getData ().data (), object->getData ().size ());
    }
}

//==============================================================================

NodeStore::BatchWriter::BatchWriter (Callback& callback, Scheduler& scheduler)
    : m_callback (callback)
    , m_scheduler (scheduler)
    , mWriteGeneration (0)
    , mWriteLoad (0)
    , mWritePending (false)
{
    mWriteSet.reserve (batchWritePreallocationSize);
}

NodeStore::BatchWriter::~BatchWriter ()
{
    waitForWriting ();
}

void NodeStore::BatchWriter::store (NodeObject::ref object)
{
    LockType::scoped_lock sl (mWriteMutex);

    mWriteSet.push_back (object);

    if (! mWritePending)
    {
        mWritePending = true;

        m_scheduler.scheduleTask (this);
    }
}

int NodeStore::BatchWriter::getWriteLoad ()
{
    LockType::scoped_lock sl (mWriteMutex);

    return std::max (mWriteLoad, static_cast<int> (mWriteSet.size ()));
}

void NodeStore::BatchWriter::performScheduledTask ()
{
    writeBatch ();
}

void NodeStore::BatchWriter::writeBatch ()
{
    int setSize = 0;

    for (;;)
    {
        std::vector< boost::shared_ptr<NodeObject> > set;

        set.reserve (batchWritePreallocationSize);

        {
            LockType::scoped_lock sl (mWriteMutex);

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

            int newSetSize = mWriteSet.size();
            mWriteLoad = std::max (setSize, newSetSize);
            newSetSize = set.size ();
        }

        m_callback.writeBatch (set);
    }
}

void NodeStore::BatchWriter::waitForWriting ()
{
    LockType::scoped_lock sl (mWriteMutex);
    int gen = mWriteGeneration;

    while (mWritePending && (mWriteGeneration == gen))
        mWriteCondition.wait (sl);
}

//==============================================================================

class NodeStoreImp
    : public NodeStore
    , LeakChecked <NodeStoreImp>
{
public:
    NodeStoreImp (Parameters const& backendParameters,
                  Parameters const& fastBackendParameters,
                  Scheduler& scheduler)
        : m_scheduler (scheduler)
        , m_backend (createBackend (backendParameters, scheduler))
        , m_fastBackend ((fastBackendParameters.size () > 0)
            ? createBackend (fastBackendParameters, scheduler) : nullptr)
        , m_cache ("NodeStore", 16384, 300)
        , m_negativeCache ("NoteStoreNegativeCache", 0, 120)
    {
    }

    ~NodeStoreImp ()
    {
    }

    String getName () const
    {
        return m_backend->getName ();
    }

    //------------------------------------------------------------------------------

    NodeObject::Ptr fetch (uint256 const& hash)
    {
        // See if the object already exists in the cache
        //
        NodeObject::Ptr obj = m_cache.fetch (hash);

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
                    obj = fetchInternal (m_fastBackend, hash);

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
                        //
                        // VFALCO TODO Why is this an autoptr? Why can't it just be a plain old object?
                        //
                        // VFALCO NOTE Commented this out because it breaks the unit test!
                        //
                        //LoadEvent::autoptr event (getApp().getJobQueue ().getLoadEventAP (jtHO_READ, "HOS::retrieve"));

                        obj = fetchInternal (m_backend, hash);
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

    NodeObject::Ptr fetchInternal (Backend* backend, uint256 const& hash)
    {
        NodeObject::Ptr object;

        Backend::Status const status = backend->fetch (hash.begin (), &object);

        switch (status)
        {
        case Backend::ok:
        case Backend::notFound:
            break;

        case Backend::dataCorrupt:
            // VFALCO TODO Deal with encountering corrupt data!
            //
            WriteLog (lsFATAL, NodeObject) << "Corrupt NodeObject #" << hash;
            break;

        default:
            WriteLog (lsWARNING, NodeObject) << "Unknown status=" << status;
            break;
        }

        return object;
    }

    //------------------------------------------------------------------------------

    void store (NodeObjectType type,
                uint32 index,
                Blob& data,
                uint256 const& hash)
    {
        bool const keyFoundAndObjectCached = m_cache.refreshIfPresent (hash);

        // VFALCO NOTE What happens if the key is found, but the object
        //             fell out of the cache? We will end up passing it
        //             to the backend anyway.
        //
        if (! keyFoundAndObjectCached)
        {
        #if RIPPLE_VERIFY_NODEOBJECT_KEYS
            assert (hash == Serializer::getSHA512Half (data));
        #endif

            NodeObject::Ptr object = NodeObject::createObject (
                type, index, data, hash);

            if (!m_cache.canonicalize (hash, object))
            {
                m_backend->store (object);

                if (m_fastBackend)
                    m_fastBackend->store (object);
            }

            m_negativeCache.del (hash);
        }
    }

    //------------------------------------------------------------------------------

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

    //------------------------------------------------------------------------------

    void visitAll (Backend::VisitCallback& callback)
    {
        m_backend->visitAll (callback);
    }

    void import (NodeStore& sourceDatabase)
    {
        class ImportVisitCallback : public Backend::VisitCallback
        {
        public:
            explicit ImportVisitCallback (Backend& backend)
                : m_backend (backend)
            {
                m_objects.reserve (batchWritePreallocationSize);
            }

            ~ImportVisitCallback ()
            {
                if (! m_objects.empty ())
                    m_backend.storeBatch (m_objects);
            }

            void visitObject (NodeObject::Ptr const& object)
            {
                if (m_objects.size () >= batchWritePreallocationSize)
                {
                    m_backend.storeBatch (m_objects);

                    m_objects.clear ();
                    m_objects.reserve (batchWritePreallocationSize);
                }

                m_objects.push_back (object);
            }

        private:
            Backend& m_backend;
            Batch m_objects;
        };

        //--------------------------------------------------------------------------

        ImportVisitCallback callback (*m_backend);

        sourceDatabase.visitAll (callback);
    }

    //------------------------------------------------------------------------------

    static NodeStore::Backend* createBackend (
        Parameters const& parameters, Scheduler& scheduler = getSynchronousScheduler ())
    {
        Backend* backend = nullptr;

        String const& type = parameters ["type"];

        if (type.isNotEmpty ())
        {
            BackendFactory* factory = nullptr;

            for (int i = 0; i < s_factories.size (); ++i)
            {
                if (s_factories [i]->getName ().compareIgnoreCase (type) == 0)
                {
                    factory = s_factories [i];
                    break;
                }
            }

            if (factory != nullptr)
            {
                backend = factory->createInstance (NodeObject::keyBytes, parameters, scheduler);
            }
            else
            {
                Throw (std::runtime_error ("unknown backend type"));
            }
        }
        else
        {
            Throw (std::runtime_error ("missing backend type"));
        }

        return backend;
    }

    static void addBackendFactory (BackendFactory& factory)
    {
        s_factories.add (&factory);
    }

    //------------------------------------------------------------------------------

private:
    static Array <NodeStore::BackendFactory*> s_factories;

    Scheduler& m_scheduler;

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

NodeStore::Scheduler& NodeStore::getSynchronousScheduler ()
{
    // Simple scheduler that performs the task immediately
    struct SynchronousScheduler : Scheduler
    {
        void scheduleTask (Task* task)
        {
            task->performScheduledTask ();
        }
    };

    static SynchronousScheduler scheduler;

    return scheduler;
}

NodeStore* NodeStore::New (Parameters const& backendParameters,
                           Parameters fastBackendParameters,
                           Scheduler& scheduler)
{
    return new NodeStoreImp (backendParameters,
                             fastBackendParameters,
                             scheduler);
}

//==============================================================================

// Some common code for the unit tests
//
class NodeStoreUnitTest : public UnitTest
{
public:
    // Tunable parameters
    //
    enum
    {
        maxPayloadBytes = 1000,
        numObjectsToTest = 1000
    };

    // Shorthand type names
    //
    typedef NodeStore::Backend Backend;
    typedef NodeStore::Batch Batch;

    // Creates predictable objects
    class PredictableObjectFactory
    {
    public:
        explicit PredictableObjectFactory (int64 seedValue)
            : m_seedValue (seedValue)
        {
        }

        NodeObject::Ptr createObject (int index)
        {
            Random r (m_seedValue + index);

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

            int const payloadBytes = 1 + r.nextInt (maxPayloadBytes);

            Blob data (payloadBytes);

            r.nextBlob (data.data (), payloadBytes);

            return NodeObject::createObject (type, ledgerIndex, data, hash);
        }

    private:
        int64 const m_seedValue;
    };

public:
    NodeStoreUnitTest (String name, UnitTest::When when = UnitTest::runAlways)
        : UnitTest (name, "ripple", when)
    {
    }

    // Create a predictable batch of objects
    static void createPredictableBatch (Batch& batch, int startingIndex, int numObjects, int64 seedValue)
    {
        batch.reserve (numObjects);

        PredictableObjectFactory factory (seedValue);

        for (int i = 0; i < numObjects; ++i)
            batch.push_back (factory.createObject (startingIndex + i));
    }

    // Compare two batches for equality
    static bool areBatchesEqual (Batch const& lhs, Batch const& rhs)
    {
        bool result = true;

        if (lhs.size () == rhs.size ())
        {
            for (int i = 0; i < lhs.size (); ++i)
            {
                if (! lhs [i]->isCloneOf (rhs [i]))
                {
                    result = false;
                    break;
                }
            }
        }
        else
        {
            result = false;
        }

        return result;
    }

    // Store a batch in a backend
    void storeBatch (Backend& backend, Batch const& batch)
    {
        for (int i = 0; i < batch.size (); ++i)
        {
            backend.store (batch [i]);
        }
    }

    // Get a copy of a batch in a backend
    void fetchCopyOfBatch (Backend& backend, Batch* pCopy, Batch const& batch)
    {
        pCopy->clear ();
        pCopy->reserve (batch.size ());

        for (int i = 0; i < batch.size (); ++i)
        {
            NodeObject::Ptr object;

            Backend::Status const status = backend.fetch (
                batch [i]->getHash ().cbegin (), &object);

            expect (status == Backend::ok, "Should be ok");

            if (status == Backend::ok)
            {
                expect (object != nullptr, "Should not be null");

                pCopy->push_back (object);
            }
        }
    }

    // Store all objects in a batch
    static void storeBatch (NodeStore& db, NodeStore::Batch const& batch)
    {
        for (int i = 0; i < batch.size (); ++i)
        {
            NodeObject::Ptr const object (batch [i]);

            Blob data (object->getData ());

            db.store (object->getType (),
                      object->getIndex (),
                      data,
                      object->getHash ());
        }
    }

    // Fetch all the hashes in one batch, into another batch.
    static void fetchCopyOfBatch (NodeStore& db,
                                  NodeStore::Batch* pCopy,
                                  NodeStore::Batch const& batch)
    {
        pCopy->clear ();
        pCopy->reserve (batch.size ());

        for (int i = 0; i < batch.size (); ++i)
        {
            NodeObject::Ptr object = db.fetch (batch [i]->getHash ());

            if (object != nullptr)
                pCopy->push_back (object);
        }
    }
};

//------------------------------------------------------------------------------

// Tests predictable batches, and NodeObject blob encoding
//
class NodeStoreBasicsTests : public NodeStoreUnitTest
{
public:
    typedef NodeStore::EncodedBlob EncodedBlob;
    typedef NodeStore::DecodedBlob DecodedBlob;

    NodeStoreBasicsTests () : NodeStoreUnitTest ("NodeStoreBasics")
    {
    }

    // Make sure predictable object generation works!
    void testBatches (int64 const seedValue)
    {
        beginTest ("batch");

        Batch batch1;
        createPredictableBatch (batch1, 0, numObjectsToTest, seedValue);

        Batch batch2;
        createPredictableBatch (batch2, 0, numObjectsToTest, seedValue);

        expect (areBatchesEqual (batch1, batch2), "Should be equal");

        Batch batch3;
        createPredictableBatch (batch3, 1, numObjectsToTest, seedValue);

        expect (! areBatchesEqual (batch1, batch3), "Should not be equal");
    }

    // Checks encoding/decoding blobs
    void testBlobs (int64 const seedValue)
    {
        beginTest ("encoding");

        Batch batch;
        createPredictableBatch (batch, 0, numObjectsToTest, seedValue);

        EncodedBlob encoded;
        for (int i = 0; i < batch.size (); ++i)
        {
            encoded.prepare (batch [i]);

            DecodedBlob decoded (encoded.getKey (), encoded.getData (), encoded.getSize ());

            expect (decoded.wasOk (), "Should be ok");

            if (decoded.wasOk ())
            {
                NodeObject::Ptr const object (decoded.createObject ());

                expect (batch [i]->isCloneOf (object), "Should be clones");
            }
        }
    }

    void runTest ()
    {
        int64 const seedValue = 50;

        testBatches (seedValue);

        testBlobs (seedValue);
    }
};

static NodeStoreBasicsTests nodeStoreBasicsTests;

//------------------------------------------------------------------------------

// Tests the NodeStore::Backend interface
//
class NodeStoreBackendTests : public NodeStoreUnitTest
{
public:
    NodeStoreBackendTests () : NodeStoreUnitTest ("NodeStoreBackend")
    {
    }

    //--------------------------------------------------------------------------

    void testBackend (String type, int64 const seedValue)
    {
        beginTest (String ("NodeStore::Backend type=") + type);

        StringPairArray params;
        File const path (File::createTempFile ("node_db"));
        params.set ("type", type);
        params.set ("path", path.getFullPathName ());

        // Create a batch
        NodeStore::Batch batch;
        createPredictableBatch (batch, 0, numObjectsToTest, seedValue);

        {
            // Open the backend
            ScopedPointer <Backend> backend (NodeStoreImp::createBackend (params));

            // Write the batch
            storeBatch (*backend, batch);

            {
                // Read it back in
                NodeStore::Batch copy;
                fetchCopyOfBatch (*backend, &copy, batch);
                expect (areBatchesEqual (batch, copy), "Should be equal");
            }

            {
                // Reorder and read the copy again
                NodeStore::Batch copy;
                UnitTestUtilities::repeatableShuffle (batch.size (), batch, seedValue);
                fetchCopyOfBatch (*backend, &copy, batch);
                expect (areBatchesEqual (batch, copy), "Should be equal");
            }
        }

        {
            // Re-open the backend
            ScopedPointer <Backend> backend (NodeStoreImp::createBackend (params));

            // Read it back in
            NodeStore::Batch copy;
            fetchCopyOfBatch (*backend, &copy, batch);
            // Canonicalize the source and destination batches
            std::sort (batch.begin (), batch.end (), NodeObject::LessThan ());
            std::sort (copy.begin (), copy.end (), NodeObject::LessThan ());
            expect (areBatchesEqual (batch, copy), "Should be equal");
        }
    }

    //--------------------------------------------------------------------------

    void runTest ()
    {
        int const seedValue = 50;

        testBackend ("keyvadb", seedValue);

        testBackend ("leveldb", seedValue);

        testBackend ("sqlite", seedValue);

        #if RIPPLE_HYPERLEVELDB_AVAILABLE
        testBackend ("hyperleveldb", seedValue);
        #endif

        #if RIPPLE_MDB_AVAILABLE
        testBackend ("mdb", seedValue);
        #endif
    }
};

static NodeStoreBackendTests nodeStoreBackendTests;

//------------------------------------------------------------------------------

class NodeStoreTimingTests : public NodeStoreUnitTest
{
public:
    enum
    {
        numObjectsToTest     = 20000
    };

    NodeStoreTimingTests ()
        : NodeStoreUnitTest ("NodeStoreTiming", UnitTest::runManual)
    {
    }

    class Stopwatch
    {
    public:
        Stopwatch ()
        {
        }

        void start ()
        {
            m_startTime = Time::getHighResolutionTicks ();
        }

        double getElapsed ()
        {
            int64 const now = Time::getHighResolutionTicks();

            return Time::highResolutionTicksToSeconds (now - m_startTime);
        }

    private:
        int64 m_startTime;
    };

    //--------------------------------------------------------------------------

    void testBackend (String type, int64 const seedValue)
    {
        String s;
        s << "Testing backend '" << type << "' performance";
        beginTest (s);

        StringPairArray params;
        File const path (File::createTempFile ("node_db"));
        params.set ("type", type);
        params.set ("path", path.getFullPathName ());

        // Create batches
        NodeStore::Batch batch1;
        createPredictableBatch (batch1, 0, numObjectsToTest, seedValue);
        NodeStore::Batch batch2;
        createPredictableBatch (batch2, 0, numObjectsToTest, seedValue);

        // Open the backend
        ScopedPointer <Backend> backend (NodeStoreImp::createBackend (params));

        Stopwatch t;

        // Individual write batch test
        t.start ();
        storeBatch (*backend, batch1);
        s = "";
        s << "  Single write: " << String (t.getElapsed (), 2) << " seconds";
        logMessage (s);

        // Bulk write batch test
        t.start ();
        backend->storeBatch (batch2);
        s = "";
        s << "  Batch write:  " << String (t.getElapsed (), 2) << " seconds";
        logMessage (s);

        // Read test
        Batch copy;
        t.start ();
        fetchCopyOfBatch (*backend, &copy, batch1);
        fetchCopyOfBatch (*backend, &copy, batch2);
        s = "";
        s << "  Batch read:   " << String (t.getElapsed (), 2) << " seconds";
        logMessage (s);
    }

    //--------------------------------------------------------------------------

    void runTest ()
    {
        int const seedValue = 50;

        testBackend ("keyvadb", seedValue);

        testBackend ("leveldb", seedValue);

    #if RIPPLE_HYPERLEVELDB_AVAILABLE
        testBackend ("hyperleveldb", seedValue);
    #endif

    #if RIPPLE_MDB_AVAILABLE
        testBackend ("mdb", seedValue);
    #endif

        testBackend ("sqlite", seedValue);
    }
};

static NodeStoreTimingTests nodeStoreTimingTests;

//------------------------------------------------------------------------------

class NodeStoreTests : public NodeStoreUnitTest
{
public:
    NodeStoreTests () : NodeStoreUnitTest ("NodeStore")
    {
    }

    void testImport (String destBackendType, String srcBackendType, int64 seedValue)
    {
        File const node_db (File::createTempFile ("node_db"));
        StringPairArray srcParams;
        srcParams.set ("type", srcBackendType);
        srcParams.set ("path", node_db.getFullPathName ());

        // Create a batch
        NodeStore::Batch batch;
        createPredictableBatch (batch, 0, numObjectsToTest, seedValue);

        // Write to source db
        {
            ScopedPointer <NodeStore> src (NodeStore::New (srcParams));

            storeBatch (*src, batch);
        }

        NodeStore::Batch copy;

        {
            // Re-open the db
            ScopedPointer <NodeStore> src (NodeStore::New (srcParams));

            // Set up the destination database
            File const dest_db (File::createTempFile ("dest_db"));
            StringPairArray destParams;
            destParams.set ("type", destBackendType);
            destParams.set ("path", dest_db.getFullPathName ());

            ScopedPointer <NodeStore> dest (NodeStore::New (destParams));

            beginTest (String ("import into '") + destBackendType + "' from '" + srcBackendType + "'");

            // Do the import
            dest->import (*src);

            // Get the results of the import
            fetchCopyOfBatch (*dest, &copy, batch);
        }

        // Canonicalize the source and destination batches
        std::sort (batch.begin (), batch.end (), NodeObject::LessThan ());
        std::sort (copy.begin (), copy.end (), NodeObject::LessThan ());
        expect (areBatchesEqual (batch, copy), "Should be equal");

    }

    //--------------------------------------------------------------------------

    void testNodeStore (String type,
                        bool const useEphemeralDatabase,
                        bool const testPersistence,
                        int64 const seedValue)
    {
        String s;
        s << String ("NodeStore backend '") + type + "'";
        if (useEphemeralDatabase)
            s << " (with ephemeral database)";

        beginTest (s);

        File const node_db (File::createTempFile ("node_db"));
        StringPairArray nodeParams;
        nodeParams.set ("type", type);
        nodeParams.set ("path", node_db.getFullPathName ());

        File const temp_db  (File::createTempFile ("temp_db"));
        StringPairArray tempParams;
        if (useEphemeralDatabase)
        {
            tempParams.set ("type", type);
            tempParams.set ("path", temp_db.getFullPathName ());
        }

        // Create a batch
        NodeStore::Batch batch;
        createPredictableBatch (batch, 0, numObjectsToTest, seedValue);

        {
            // Open the database
            ScopedPointer <NodeStore> db (NodeStore::New (nodeParams, tempParams));

            // Write the batch
            storeBatch (*db, batch);

            {
                // Read it back in
                NodeStore::Batch copy;
                fetchCopyOfBatch (*db, &copy, batch);
                expect (areBatchesEqual (batch, copy), "Should be equal");
            }

            {
                // Reorder and read the copy again
                NodeStore::Batch copy;
                UnitTestUtilities::repeatableShuffle (batch.size (), batch, seedValue);
                fetchCopyOfBatch (*db, &copy, batch);
                expect (areBatchesEqual (batch, copy), "Should be equal");
            }
        }

        if (testPersistence)
        {
            {
                // Re-open the database without the ephemeral DB
                ScopedPointer <NodeStore> db (NodeStore::New (nodeParams));

                // Read it back in
                NodeStore::Batch copy;
                fetchCopyOfBatch (*db, &copy, batch);

                // Canonicalize the source and destination batches
                std::sort (batch.begin (), batch.end (), NodeObject::LessThan ());
                std::sort (copy.begin (), copy.end (), NodeObject::LessThan ());
                expect (areBatchesEqual (batch, copy), "Should be equal");
            }

            if (useEphemeralDatabase)
            {
                // Verify the ephemeral db
                ScopedPointer <NodeStore> db (NodeStore::New (tempParams, StringPairArray ()));

                // Read it back in
                NodeStore::Batch copy;
                fetchCopyOfBatch (*db, &copy, batch);

                // Canonicalize the source and destination batches
                std::sort (batch.begin (), batch.end (), NodeObject::LessThan ());
                std::sort (copy.begin (), copy.end (), NodeObject::LessThan ());
                expect (areBatchesEqual (batch, copy), "Should be equal");
            }
        }
    }

    //--------------------------------------------------------------------------

    void runBackendTests (bool useEphemeralDatabase, int64 const seedValue)
    {
        testNodeStore ("keyvadb", useEphemeralDatabase, true, seedValue);

        testNodeStore ("leveldb", useEphemeralDatabase, true, seedValue);

        testNodeStore ("sqlite", useEphemeralDatabase, true, seedValue);

    #if RIPPLE_HYPERLEVELDB_AVAILABLE
        testNodeStore ("hyperleveldb", useEphemeralDatabase, true, seedValue);
    #endif

    #if RIPPLE_MDB_AVAILABLE
        testNodeStore ("mdb", useEphemeralDatabase, true, seedValue);
    #endif
    }

    //--------------------------------------------------------------------------

    void runImportTests (int64 const seedValue)
    {
        //testImport ("keyvadb", "keyvadb", seedValue);

        testImport ("leveldb", "leveldb", seedValue);

    #if RIPPLE_HYPERLEVELDB_AVAILABLE
        testImport ("hyperleveldb", "hyperleveldb", seedValue);
    #endif

    #if RIPPLE_MDB_AVAILABLE
        //testImport ("mdb", "mdb", seedValue);
    #endif

        testImport ("sqlite", "sqlite", seedValue);
    }

    //--------------------------------------------------------------------------

    void runTest ()
    {
        int64 const seedValue = 50;

        testNodeStore ("memory", false, false, seedValue);

        runBackendTests (false, seedValue);

        runBackendTests (true, seedValue);

        runImportTests (seedValue);
    }
};

static NodeStoreTests nodeStoreTests;
