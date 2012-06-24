#ifndef __HASHEDOBJECT__
#define __HASHEDOBJECT__

#include <vector>

#include "types.h"
#include "uint256.h"
#include "ScopedLock.h"
#include "TaggedCache.h"

enum HashedObjectType
{
	UNKNOWN = 0,
	LEDGER = 1,
	TRANSACTION = 2,
	ACCOUNT_NODE = 3,
	TRANSACTION_NODE = 4
};

class HashedObject
{
public:
	typedef boost::shared_ptr<HashedObject> pointer;

	HashedObjectType 			mType;
	uint256						mHash;
	uint32						mLedgerIndex;
	std::vector<unsigned char>	mData;

	HashedObject(HashedObjectType type, uint32 index, const std::vector<unsigned char>& data) :
		mType(type), mLedgerIndex(index), mData(data) { ; }

	bool checkHash() const;
	bool checkFixHash();
	void setHash();

	const std::vector<unsigned char>& getData()		{ return mData; }
	const uint256& getHash() 						{ return mHash; }
	HashedObjectType getType()						{ return mType; }
	uint32 getIndex()								{ return mLedgerIndex; }
};

class HashedObjectStore
{
protected:
	TaggedCache<uint256, HashedObject> mCache;

	boost::recursive_mutex mWriteMutex;
	std::vector< boost::shared_ptr<HashedObject> > mWriteSet;
	bool mWritePending;

public:

	HashedObjectStore(int cacheSize, int cacheAge);

	bool store(HashedObjectType type, uint32 index, const std::vector<unsigned char>& data,
		const uint256& hash);

	HashedObject::pointer retrieve(const uint256& hash);

	void bulkWrite();
};

#endif
