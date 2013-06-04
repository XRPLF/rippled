#ifndef __HASHEDOBJECT__
#define __HASHEDOBJECT__

#include <vector>

#include <boost/thread/mutex.hpp>
#include <boost/thread/condition_variable.hpp>

// VFALCO: TODO, Move this to someplace sensible!!
// Adapter to furnish uptime information to KeyCache via UptimeTimer singleton
struct UptimeTimerAdapter
{
	inline static int getElapsedSeconds ()
	{
		return UptimeTimer::getInstance().getElapsedSeconds ();
	}
};



DEFINE_INSTANCE(HashedObject);

class Job;

enum HashedObjectType
{
	hotUNKNOWN = 0,
	hotLEDGER = 1,
	hotTRANSACTION = 2,
	hotACCOUNT_NODE = 3,
	hotTRANSACTION_NODE = 4
};

class HashedObject : private IS_INSTANCE(HashedObject)
{
public:
	typedef boost::shared_ptr<HashedObject> pointer;

	HashedObjectType			mType;
	uint256						mHash;
	uint32						mLedgerIndex;
	std::vector<unsigned char>	mData;

	HashedObject(HashedObjectType type, uint32 index, const std::vector<unsigned char>& data, const uint256& hash) :
		mType(type), mHash(hash), mLedgerIndex(index), mData(data) { ; }

	HashedObject(HashedObjectType type, uint32 index, const unsigned char *data, int dlen, const uint256& hash) :
		mType(type), mHash(hash), mLedgerIndex(index), mData(data, data + dlen) { ; }

	const std::vector<unsigned char>& getData() const	{ return mData; }
	const uint256& getHash() const						{ return mHash; }
	HashedObjectType getType() const					{ return mType; }
	uint32 getIndex() const								{ return mLedgerIndex; }
};

class HashedObjectStore
{
public:

	HashedObjectStore(int cacheSize, int cacheAge);

	bool isLevelDB()		{ return mLevelDB; }

	float getCacheHitRate()	{ return mCache.getHitRate(); }

	bool store(HashedObjectType type, uint32 index, const std::vector<unsigned char>& data,
		const uint256& hash)
	{
#ifdef USE_LEVELDB
		if (mLevelDB)
			return storeLevelDB(type, index, data, hash);
#endif
		return storeSQLite(type, index, data, hash);
	}

	HashedObject::pointer retrieve(const uint256& hash)
	{
#ifdef USE_LEVELDB
		if (mLevelDB)
			return retrieveLevelDB(hash);
#endif
		return retrieveSQLite(hash);
	}

	bool storeSQLite(HashedObjectType type, uint32 index, const std::vector<unsigned char>& data,
		const uint256& hash);
	HashedObject::pointer retrieveSQLite(const uint256& hash);
	void bulkWriteSQLite(Job&);

#ifdef USE_LEVELDB
	bool storeLevelDB(HashedObjectType type, uint32 index, const std::vector<unsigned char>& data,
		const uint256& hash);
	HashedObject::pointer retrieveLevelDB(const uint256& hash);
	void bulkWriteLevelDB(Job&);
#endif


	void waitWrite();
	void tune(int size, int age);
	void sweep() { mCache.sweep(); mNegativeCache.sweep(); }
	int getWriteLoad();

	int import(const std::string& fileName);

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
};

#endif
// vim:ts=4
