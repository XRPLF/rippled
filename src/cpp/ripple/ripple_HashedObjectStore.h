#ifndef RIPPLE_HASHEDOBJECTSTORE_H
#define RIPPLE_HASHEDOBJECTSTORE_H

/** Persistency layer for hashed objects.
*/
class HashedObjectStore
{
public:
	HashedObjectStore (int cacheSize, int cacheAge);

	bool isLevelDB()
    {
        return mLevelDB;
    }

	float getCacheHitRate ()
    {
        return mCache.getHitRate();
    }

	bool store (HashedObjectType type, uint32 index, const std::vector<unsigned char>& data,
		const uint256& hash)
	{
		if (mLevelDB)
			return storeLevelDB(type, index, data, hash);

        return storeSQLite(type, index, data, hash);
	}

	HashedObject::pointer retrieve(const uint256& hash)
	{
		if (mLevelDB)
			return retrieveLevelDB(hash);
		return retrieveSQLite(hash);
	}

	bool storeSQLite(HashedObjectType type, uint32 index, const std::vector<unsigned char>& data,
		const uint256& hash);
	HashedObject::pointer retrieveSQLite(const uint256& hash);
	void bulkWriteSQLite(Job&);

	bool storeLevelDB(HashedObjectType type, uint32 index, const std::vector<unsigned char>& data,
		const uint256& hash);
	HashedObject::pointer retrieveLevelDB(const uint256& hash);
	void bulkWriteLevelDB(Job&);


	void waitWrite();
	void tune(int size, int age);
	void sweep() { mCache.sweep(); mNegativeCache.sweep(); }
	int getWriteLoad();

	int import(const std::string& fileName);

private:
    static HashedObject::pointer LLRetrieve(const uint256& hash, leveldb::DB* db);
    static void LLWrite(boost::shared_ptr<HashedObject> ptr, leveldb::DB* db);
    static void LLWrite(const std::vector< boost::shared_ptr<HashedObject> >& set, leveldb::DB* db);

private:
	TaggedCache<uint256, HashedObject, UptimeTimerAdapter>	mCache;
	KeyCache <uint256, UptimeTimerAdapter> mNegativeCache;

	boost::mutex				mWriteMutex;
	boost::condition_variable	mWriteCondition;
	int							mWriteGeneration;
	int							mWriteLoad;

	std::vector< boost::shared_ptr<HashedObject> > mWriteSet;
	bool mWritePending;
	bool mLevelDB;
	bool mEphemeralDB;
};

#endif
// vim:ts=4
