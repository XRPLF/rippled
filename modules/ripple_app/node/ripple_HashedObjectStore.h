//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_HASHEDOBJECTSTORE_H
#define RIPPLE_HASHEDOBJECTSTORE_H

/** Persistency layer for hashed objects.
*/
// VFALCO TODO Move all definitions to the .cpp
class HashedObjectStore : LeakChecked <HashedObjectStore>
{
public:
    HashedObjectStore (int cacheSize, int cacheAge);

    bool isLevelDB ()
    {
        return mLevelDB;
    }

    float getCacheHitRate ()
    {
        return mCache.getHitRate ();
    }

    bool store (HashedObjectType type, uint32 index, Blob const& data,
                uint256 const& hash)
    {
        if (mLevelDB)
            return storeLevelDB (type, index, data, hash);

        return storeSQLite (type, index, data, hash);
    }

    HashedObject::pointer retrieve (uint256 const& hash)
    {
        if (mLevelDB)
            return retrieveLevelDB (hash);

        return retrieveSQLite (hash);
    }

    bool storeSQLite (HashedObjectType type, uint32 index, Blob const& data,
                      uint256 const& hash);
    HashedObject::pointer retrieveSQLite (uint256 const& hash);
    void bulkWriteSQLite (Job&);

    bool storeLevelDB (HashedObjectType type, uint32 index, Blob const& data,
                       uint256 const& hash);
    HashedObject::pointer retrieveLevelDB (uint256 const& hash);
    void bulkWriteLevelDB (Job&);


    void waitWrite ();
    void tune (int size, int age);
    void sweep ()
    {
        mCache.sweep ();
        mNegativeCache.sweep ();
    }
    int getWriteLoad ();

    int import (const std::string& fileName);

private:
    static HashedObject::pointer LLRetrieve (uint256 const& hash, leveldb::DB* db);
    static void LLWrite (boost::shared_ptr<HashedObject> ptr, leveldb::DB* db);
    static void LLWrite (const std::vector< boost::shared_ptr<HashedObject> >& set, leveldb::DB* db);

private:
    TaggedCache<uint256, HashedObject, UptimeTimerAdapter>  mCache;
    KeyCache <uint256, UptimeTimerAdapter> mNegativeCache;

    boost::mutex                mWriteMutex;
    boost::condition_variable   mWriteCondition;
    int                         mWriteGeneration;
    int                         mWriteLoad;

    std::vector< boost::shared_ptr<HashedObject> > mWriteSet;
    bool mWritePending;
    bool mLevelDB;
    bool mEphemeralDB;
};

#endif
// vim:ts=4
