#ifndef __HASHEDOBJECT__
#define __HASHEDOBJECT__

#include <vector>

#include <boost/thread/mutex.hpp>
#include <boost/thread/condition_variable.hpp>

#include "types.h"
#include "uint256.h"
#include "ScopedLock.h"
#include "TaggedCache.h"
#include "KeyCache.h"
#include "InstanceCounter.h"

DEFINE_INSTANCE(HashedObject);

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

	const std::vector<unsigned char>& getData() const	{ return mData; }
	const uint256& getHash() const						{ return mHash; }
	HashedObjectType getType() const					{ return mType; }
	uint32 getIndex() const								{ return mLedgerIndex; }
};

class HashedObjectStore
{
protected:
	TaggedCache<uint256, HashedObject>	mCache;
	KeyCache<uint256>					mNegativeCache;

	boost::mutex				mWriteMutex;
	boost::condition_variable	mWriteCondition;
	int							mWriteGeneration;

	std::vector< boost::shared_ptr<HashedObject> > mWriteSet;
	bool mWritePending;

public:

	HashedObjectStore(int cacheSize, int cacheAge);

	bool store(HashedObjectType type, uint32 index, const std::vector<unsigned char>& data,
		const uint256& hash);

	HashedObject::pointer retrieve(const uint256& hash);

	void bulkWrite();
	void waitWrite();
	void sweep() { mCache.sweep(); mNegativeCache.sweep(); }

	int import(const std::string&);
};

#endif
// vim:ts=4
