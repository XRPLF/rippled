
HSBELevelDB::HSBELevelDB(std::string const& path) : mName(path), mDB(NULL)
{
    leveldb::Options options;
    options.create_if_missing = true;
    options.block_cache = leveldb::NewLRUCache (theConfig.getSize (siHashNodeDBCache) * 1024 * 1024);

    if (theConfig.NODE_SIZE >= 2)
        options.filter_policy = leveldb::NewBloomFilterPolicy (10);

    leveldb::Status status = leveldb::DB::Open (options, path, &mDB);
    if (!status.ok () || mDB)
        throw (std::runtime_error (std::string("Unable to open/create leveldb: ") + status.ToString()));
}

HSBELevelDB::~HSBELevelDB()
{
    delete mDB;
}

std::string HSBELevelDB::getDataBaseName()
{
    return mName;
}

bool HSBELevelDB::store(HashedObject::ref obj)
{
    Blob blob (toBlob (obj));
    return mDB->Put (leveldb::WriteOptions (),
        leveldb::Slice (reinterpret_cast<char const*>(obj->getHash ().begin ()), 256 / 8),
        leveldb::Slice (reinterpret_cast<char const*>(&blob.front ()), blob.size ())).ok ();
}

bool HSBELevelDB::bulkStore(const std::vector< HashedObject::pointer >& objs)
{
    leveldb::WriteBatch batch;

    BOOST_FOREACH (HashedObject::ref obj, objs)
    {
        Blob blob (toBlob (obj));
        batch.Put (
            leveldb::Slice (reinterpret_cast<char const*>(obj->getHash ().begin ()), 256 / 8),
            leveldb::Slice (reinterpret_cast<char const*>(&blob.front ()), blob.size ()));
    }
    return mDB->Write (leveldb::WriteOptions (), &batch).ok ();
}

HashedObject::pointer HSBELevelDB::retrieve(uint256 const& hash)
{
    std::string sData;
    if (!mDB->Get (leveldb::ReadOptions (),
        leveldb::Slice (reinterpret_cast<char const*>(hash.begin ()), 256 / 8), &sData).ok ())
    {
        return HashedObject::pointer();
    }
    return fromBinary(hash, &sData[0], sData.size ());
}

void HSBELevelDB::visitAll(FUNCTION_TYPE<void (HashedObject::pointer)> func)
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

Blob HSBELevelDB::toBlob(HashedObject::ref obj)
{
    Blob rawData (9 + obj->getData ().size ());
    unsigned char* bufPtr = &rawData.front();

    *reinterpret_cast<uint32*> (bufPtr + 0) = ntohl (obj->getIndex ());
    *reinterpret_cast<uint32*> (bufPtr + 4) = ntohl (obj->getIndex ());
    * (bufPtr + 8) = static_cast<unsigned char> (obj->getType ());
    memcpy (bufPtr + 9, &obj->getData ().front (), obj->getData ().size ());

    return rawData;
}

HashedObject::pointer HSBELevelDB::fromBinary(uint256 const& hash,
    char const* data, int size)
{
    if (size < 9)
        throw std::runtime_error ("undersized object");

    uint32 index = htonl (*reinterpret_cast<const uint32*> (data));
    int htype = data[8];

    return boost::make_shared<HashedObject> (static_cast<HashedObjectType> (htype), index,
        data + 9, size - 9, hash);
}
