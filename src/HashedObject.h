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

	const std::vector<unsigned char>& getData() { return mData; }
	const uint256& getHash() { return mHash; }
};

class HashedObjectStore
{
protected:
	TaggedCache<uint256, HashedObject> mCache;

public:

	HashedObjectStore(int cacheSize, int cacheAge) : mCache(cacheSize, cacheAge) { ; }

	bool store(HashedObjectType type, uint32 index, const std::vector<unsigned char>& data,
		const uint256& hash);

	HashedObject::pointer retrieve(const uint256& hash);

	ScopedLock beginBulk();
	void endBulk();
};

class HashedObjectBulkWriter
{
protected:
	HashedObjectStore& mStore;
	ScopedLock sl;

public:
	HashedObjectBulkWriter(HashedObjectStore& ostore) : mStore(ostore), sl(mStore.beginBulk()) { ; }
	~HashedObjectBulkWriter() { mStore.endBulk(); }

	bool store(HashedObjectType type, uint32 index, const std::vector<unsigned char>& data,
		const uint256& hash) { return mStore.store(type, index, data, hash); }

	HashedObject::pointer retrieve(const uint256& hash) { return mStore.retrieve(hash); }
};

#endif
