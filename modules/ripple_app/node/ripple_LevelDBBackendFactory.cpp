//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

class LevelDBBackendFactory::Backend : public NodeStore::Backend
{
public:
    Backend (int keyBytes, StringPairArray const& keyValues)
        : m_keyBytes (keyBytes)
        , m_name(keyValues ["path"].toStdString ())
        , m_db(NULL)
    {
        if (m_name.empty())
            throw std::runtime_error ("Missing path in LevelDB backend");

        leveldb::Options options;
        options.create_if_missing = true;

        if (keyValues["cache_mb"].isEmpty())
            options.block_cache = leveldb::NewLRUCache (theConfig.getSize (siHashNodeDBCache) * 1024 * 1024);
        else
            options.block_cache = leveldb::NewLRUCache (keyValues["cache_mb"].getIntValue() * 1024L * 1024L);

        if (keyValues["filter_bits"].isEmpty())
        {
            if (theConfig.NODE_SIZE >= 2)
                options.filter_policy = leveldb::NewBloomFilterPolicy (10);
        }
        else if (keyValues["filter_bits"].getIntValue() != 0)
            options.filter_policy = leveldb::NewBloomFilterPolicy (keyValues["filter_bits"].getIntValue());

        if (!keyValues["open_files"].isEmpty())
            options.max_open_files = keyValues["open_files"].getIntValue();

        leveldb::Status status = leveldb::DB::Open (options, m_name, &m_db);
        if (!status.ok () || !m_db)
            throw (std::runtime_error (std::string("Unable to open/create leveldb: ") + status.ToString()));
    }

    ~Backend ()
    {
        delete m_db;
    }

    std::string getDataBaseName()
    {
        return m_name;
    }

    //--------------------------------------------------------------------------

    struct StdString
    {
        std::string blob;
    };

    typedef RecycledObjectPool <StdString> StdStringPool;

    //--------------------------------------------------------------------------

    Status get (void const* key, GetCallback* callback)
    {
        Status status (ok);

        leveldb::ReadOptions const options;
        leveldb::Slice const slice (static_cast <char const*> (key), m_keyBytes);

        {
            // These are reused std::string objects,
            // required for leveldb's funky interface.
            //
            StdStringPool::ScopedItem item (m_stringPool);
            std::string& blob = item.getObject ().blob;

            leveldb::Status getStatus = m_db->Get (options, slice, &blob);

            if (getStatus.ok ())
            {
                void* const buffer = callback->getStorageForValue (blob.size ());

                if (buffer != nullptr)
                {
                    memcpy (buffer, blob.data (), blob.size ());
                }
                else
                {
                    Throw (std::bad_alloc ());
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

    //--------------------------------------------------------------------------

    bool bulkStore (const std::vector< NodeObject::pointer >& objs)
    {
        leveldb::WriteBatch batch;

        BOOST_FOREACH (NodeObject::ref obj, objs)
        {
            Blob blob (toBlob (obj));
            batch.Put (
                leveldb::Slice (reinterpret_cast<char const*>(obj->getHash ().begin ()), m_keyBytes),
                leveldb::Slice (reinterpret_cast<char const*>(&blob.front ()), blob.size ()));
        }
        return m_db->Write (leveldb::WriteOptions (), &batch).ok ();
    }

    NodeObject::pointer retrieve (uint256 const& hash)
    {
        std::string sData;
        if (!m_db->Get (leveldb::ReadOptions (),
            leveldb::Slice (reinterpret_cast<char const*>(hash.begin ()), m_keyBytes), &sData).ok ())
        {
            return NodeObject::pointer();
        }
        return fromBinary(hash, &sData[0], sData.size ());
    }

    void visitAll (FUNCTION_TYPE<void (NodeObject::pointer)> func)
    {
        leveldb::Iterator* it = m_db->NewIterator (leveldb::ReadOptions ());
        for (it->SeekToFirst (); it->Valid (); it->Next ())
        {
            if (it->key ().size () == m_keyBytes)
            {
                uint256 hash;
                memcpy(hash.begin(), it->key ().data(), m_keyBytes);
                func (fromBinary (hash, it->value ().data (), it->value ().size ()));
            }
            else
            {
                // VFALCO NOTE What does it mean to find an
                //             incorrectly sized key? Corruption?
            }
        }
    }

    Blob toBlob(NodeObject::ref obj)
    {
        Blob rawData (9 + obj->getData ().size ());
        unsigned char* bufPtr = &rawData.front();

        *reinterpret_cast<uint32*> (bufPtr + 0) = ntohl (obj->getIndex ());
        *reinterpret_cast<uint32*> (bufPtr + 4) = ntohl (obj->getIndex ());
        * (bufPtr + 8) = static_cast<unsigned char> (obj->getType ());
        memcpy (bufPtr + 9, &obj->getData ().front (), obj->getData ().size ());

        return rawData;
    }

    NodeObject::pointer fromBinary(uint256 const& hash,
        char const* data, int size)
    {
        if (size < 9)
            throw std::runtime_error ("undersized object");

        uint32 index = htonl (*reinterpret_cast<const uint32*> (data));
        int htype = data[8];

        return boost::make_shared<NodeObject> (static_cast<NodeObjectType> (htype), index,
            data + 9, size - 9, hash);
    }

private:
    size_t const m_keyBytes;
    StdStringPool m_stringPool;
    std::string m_name;
    leveldb::DB* m_db;
};

//------------------------------------------------------------------------------

LevelDBBackendFactory::LevelDBBackendFactory ()
{
}

LevelDBBackendFactory::~LevelDBBackendFactory ()
{
}

LevelDBBackendFactory& LevelDBBackendFactory::getInstance ()
{
    static LevelDBBackendFactory instance;

    return instance;
}

String LevelDBBackendFactory::getName () const
{
    return "LevelDB";
}

NodeStore::Backend* LevelDBBackendFactory::createInstance (size_t keyBytes, StringPairArray const& keyValues)
{
    return new LevelDBBackendFactory::Backend (keyBytes, keyValues);
}

//------------------------------------------------------------------------------

