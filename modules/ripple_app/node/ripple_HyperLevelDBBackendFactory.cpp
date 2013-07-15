//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#if RIPPLE_HYPERLEVELDB_AVAILABLE

class HyperLevelDBBackendFactory::Backend : public NodeStore::Backend
{
public:
    Backend (StringPairArray const& keyValues)
        : mName(keyValues ["path"].toStdString ())
        , mDB(NULL)
    {
        if (mName.empty())
            throw std::runtime_error ("Missing path in LevelDB backend");

        hyperleveldb::Options options;
        options.create_if_missing = true;

        if (keyValues["cache_mb"].isEmpty())
            options.block_cache = hyperleveldb::NewLRUCache (theConfig.getSize (siHashNodeDBCache) * 1024 * 1024);
        else
            options.block_cache = hyperleveldb::NewLRUCache (keyValues["cache_mb"].getIntValue() * 1024L * 1024L);

        if (keyValues["filter_bits"].isEmpty())
        {
            if (theConfig.NODE_SIZE >= 2)
                options.filter_policy = hyperleveldb::NewBloomFilterPolicy (10);
        }
        else if (keyValues["filter_bits"].getIntValue() != 0)
            options.filter_policy = hyperleveldb::NewBloomFilterPolicy (keyValues["filter_bits"].getIntValue());

        if (!keyValues["open_files"].isEmpty())
            options.max_open_files = keyValues["open_files"].getIntValue();

        hyperleveldb::Status status = hyperleveldb::DB::Open (options, mName, &mDB);
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
        hyperleveldb::WriteBatch batch;

        BOOST_FOREACH (NodeObject::ref obj, objs)
        {
            Blob blob (toBlob (obj));
            batch.Put (
                hyperleveldb::Slice (reinterpret_cast<char const*>(obj->getHash ().begin ()), 256 / 8),
                hyperleveldb::Slice (reinterpret_cast<char const*>(&blob.front ()), blob.size ()));
        }
        return mDB->Write (hyperleveldb::WriteOptions (), &batch).ok ();
    }

    NodeObject::pointer retrieve (uint256 const& hash)
    {
        std::string sData;
        if (!mDB->Get (hyperleveldb::ReadOptions (),
            hyperleveldb::Slice (reinterpret_cast<char const*>(hash.begin ()), 256 / 8), &sData).ok ())
        {
            return NodeObject::pointer();
        }
        return fromBinary(hash, &sData[0], sData.size ());
    }

    void visitAll (FUNCTION_TYPE<void (NodeObject::pointer)> func)
    {
        hyperleveldb::Iterator* it = mDB->NewIterator (hyperleveldb::ReadOptions ());
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
    hyperleveldb::DB* mDB;
};

//------------------------------------------------------------------------------

HyperLevelDBBackendFactory::HyperLevelDBBackendFactory ()
{
}

HyperLevelDBBackendFactory::~HyperLevelDBBackendFactory ()
{
}

HyperLevelDBBackendFactory& HyperLevelDBBackendFactory::getInstance ()
{
    static HyperLevelDBBackendFactory instance;

    return instance;
}

String HyperLevelDBBackendFactory::getName () const
{
    return "HyperLevelDB";
}

NodeStore::Backend* HyperLevelDBBackendFactory::createInstance (StringPairArray const& keyValues)
{
    return new HyperLevelDBBackendFactory::Backend (keyValues);
}

//------------------------------------------------------------------------------

#endif
