//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

class LevelDBBackendFactory::Backend : public NodeStore::Backend
{
public:
    Backend (StringPairArray const& keyValues)
        : mName(keyValues ["path"].toStdString ())
        , mDB(NULL)
    {
        if (mName.empty())
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

        leveldb::Status status = leveldb::DB::Open (options, mName, &mDB);
        if (!status.ok () || !mDB)
            throw (std::runtime_error (std::string("Unable to open/create leveldb: ") + status.ToString()));
    }

    ~Backend ()
    {
        delete mDB;
    }

    std::string getDataBaseName()
    {
        return mName;
    }

    bool bulkStore (const std::vector< NodeObject::pointer >& objs)
    {
        leveldb::WriteBatch batch;

        BOOST_FOREACH (NodeObject::ref obj, objs)
        {
            Blob blob (toBlob (obj));
            batch.Put (
                leveldb::Slice (reinterpret_cast<char const*>(obj->getHash ().begin ()), 256 / 8),
                leveldb::Slice (reinterpret_cast<char const*>(&blob.front ()), blob.size ()));
        }
        return mDB->Write (leveldb::WriteOptions (), &batch).ok ();
    }

    NodeObject::pointer retrieve (uint256 const& hash)
    {
        std::string sData;
        if (!mDB->Get (leveldb::ReadOptions (),
            leveldb::Slice (reinterpret_cast<char const*>(hash.begin ()), 256 / 8), &sData).ok ())
        {
            return NodeObject::pointer();
        }
        return fromBinary(hash, &sData[0], sData.size ());
    }

    void visitAll (FUNCTION_TYPE<void (NodeObject::pointer)> func)
    {
        leveldb::Iterator* it = mDB->NewIterator (leveldb::ReadOptions ());
        for (it->SeekToFirst (); it->Valid (); it->Next ())
        {
            if (it->key ().size () == 256 / 8)
            {
                uint256 hash;
                memcpy(hash.begin(), it->key ().data(), 256 / 8);
                func (fromBinary (hash, it->value ().data (), it->value ().size ()));
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
    std::string mName;
    leveldb::DB* mDB;
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

NodeStore::Backend* LevelDBBackendFactory::createInstance (StringPairArray const& keyValues)
{
    return new LevelDBBackendFactory::Backend (keyValues);
}

//------------------------------------------------------------------------------

