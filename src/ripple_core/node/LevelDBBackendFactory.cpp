//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

class LevelDBBackendFactory::Backend
    : public NodeStore::Backend
    , public NodeStore::BatchWriter::Callback
    , LeakChecked <LevelDBBackendFactory::Backend>
{
public:
    typedef RecycledObjectPool <std::string> StringPool;

    //--------------------------------------------------------------------------

    Backend (int keyBytes,
             StringPairArray const& keyValues,
             NodeStore::Scheduler& scheduler)
        : m_keyBytes (keyBytes)
        , m_scheduler (scheduler)
        , m_batch (*this, scheduler)
        , m_name (keyValues ["path"].toStdString ())
    {
        if (m_name.empty())
            Throw (std::runtime_error ("Missing path in LevelDB backend"));

        leveldb::Options options;
        options.create_if_missing = true;

        if (keyValues["cache_mb"].isEmpty())
        {
            options.block_cache = leveldb::NewLRUCache (getConfig ().getSize (siHashNodeDBCache) * 1024 * 1024);
        }
        else
        {
            options.block_cache = leveldb::NewLRUCache (keyValues["cache_mb"].getIntValue() * 1024L * 1024L);
        }

        if (keyValues["filter_bits"].isEmpty())
        {
            if (getConfig ().NODE_SIZE >= 2)
                options.filter_policy = leveldb::NewBloomFilterPolicy (10);
        }
        else if (keyValues["filter_bits"].getIntValue() != 0)
        {
            options.filter_policy = leveldb::NewBloomFilterPolicy (keyValues["filter_bits"].getIntValue());
        }

        if (! keyValues["open_files"].isEmpty())
        {
            options.max_open_files = keyValues["open_files"].getIntValue();
        }

        leveldb::DB* db = nullptr;
        leveldb::Status status = leveldb::DB::Open (options, m_name, &db);
        if (!status.ok () || !db)
            Throw (std::runtime_error (std::string("Unable to open/create leveldb: ") + status.ToString()));

        m_db = db;
    }

    ~Backend ()
    {
    }

    std::string getName()
    {
        return m_name;
    }

    //--------------------------------------------------------------------------

    Status fetch (void const* key, NodeObject::Ptr* pObject)
    {
        pObject->reset ();

        Status status (ok);

        leveldb::ReadOptions const options;
        leveldb::Slice const slice (static_cast <char const*> (key), m_keyBytes);

        {
            // These are reused std::string objects,
            // required for leveldb's funky interface.
            //
            StringPool::ScopedItem item (m_stringPool);
            std::string& string = item.getObject ();

            leveldb::Status getStatus = m_db->Get (options, slice, &string);

            if (getStatus.ok ())
            {
                NodeStore::DecodedBlob decoded (key, string.data (), string.size ());

                if (decoded.wasOk ())
                {
                    *pObject = decoded.createObject ();
                }
                else
                {
                    // Decoding failed, probably corrupted!
                    //
                    status = dataCorrupt;
                }
            }
            else
            {
                if (getStatus.IsCorruption ())
                {
                    status = dataCorrupt;
                }
                else if (getStatus.IsNotFound ())
                {
                    status = notFound;
                }
                else
                {
                    status = unknown;
                }
            }
        }

        return status;
    }

    void store (NodeObject::ref object)
    {
        m_batch.store (object);
    }

    void storeBatch (NodeStore::Batch const& batch)
    {
        leveldb::WriteBatch wb;

        {
            NodeStore::EncodedBlob::Pool::ScopedItem item (m_blobPool);

            BOOST_FOREACH (NodeObject::ref object, batch)
            {
                item.getObject ().prepare (object);

                wb.Put (
                    leveldb::Slice (reinterpret_cast <char const*> (item.getObject ().getKey ()),
                                                                    m_keyBytes),
                    leveldb::Slice (reinterpret_cast <char const*> (item.getObject ().getData ()),
                                                                    item.getObject ().getSize ()));
            }
        }

        leveldb::WriteOptions const options;

        m_db->Write (options, &wb).ok ();
    }

    void visitAll (VisitCallback& callback)
    {
        leveldb::ReadOptions const options;

        ScopedPointer <leveldb::Iterator> it (m_db->NewIterator (options));

        for (it->SeekToFirst (); it->Valid (); it->Next ())
        {
            if (it->key ().size () == m_keyBytes)
            {
                NodeStore::DecodedBlob decoded (it->key ().data (),
                                                it->value ().data (),
                                                it->value ().size ());

                if (decoded.wasOk ())
                {
                    NodeObject::Ptr object (decoded.createObject ());

                    callback.visitObject (object);
                }
                else
                {
                    // Uh oh, corrupted data!
                    WriteLog (lsFATAL, NodeObject) << "Corrupt NodeObject #" << uint256 (it->key ().data ());
                }
            }
            else
            {
                // VFALCO NOTE What does it mean to find an
                //             incorrectly sized key? Corruption?
                WriteLog (lsFATAL, NodeObject) << "Bad key size = " << it->key ().size ();
            }
        }
    }

    int getWriteLoad ()
    {
        return m_batch.getWriteLoad ();
    }

    void stopAsync ()
    {
        m_batch.stopAsync ();
    }

    //--------------------------------------------------------------------------

    void writeBatch (NodeStore::Batch const& batch)
    {
        storeBatch (batch);
    }

    void writeStopped ()
    {
        m_scheduler.scheduledTasksStopped ();
    }

private:
    size_t const m_keyBytes;
    NodeStore::Scheduler& m_scheduler;
    NodeStore::BatchWriter m_batch;
    StringPool m_stringPool;
    NodeStore::EncodedBlob::Pool m_blobPool;
    std::string m_name;
    ScopedPointer <leveldb::DB> m_db;
};

//------------------------------------------------------------------------------

LevelDBBackendFactory::LevelDBBackendFactory ()
    : m_lruCache (nullptr)
{
    leveldb::Options options;
    options.create_if_missing = true;
    options.block_cache = leveldb::NewLRUCache (
        getConfig ().getSize (siHashNodeDBCache) * 1024 * 1024);

    m_lruCache = options.block_cache;
}

LevelDBBackendFactory::~LevelDBBackendFactory ()
{
    leveldb::Cache* cache (reinterpret_cast <leveldb::Cache*> (m_lruCache));
    delete cache;
}

LevelDBBackendFactory* LevelDBBackendFactory::getInstance ()
{
    return new LevelDBBackendFactory;
}

String LevelDBBackendFactory::getName () const
{
    return "LevelDB";
}

NodeStore::Backend* LevelDBBackendFactory::createInstance (
    size_t keyBytes,
    StringPairArray const& keyValues,
    NodeStore::Scheduler& scheduler)
{
    return new LevelDBBackendFactory::Backend (keyBytes, keyValues, scheduler);
}

//------------------------------------------------------------------------------

