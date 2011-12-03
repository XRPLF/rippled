#ifndef __HASHEDOBJECT__
#define __HASHEDOBJECT__

#include <vector>

#include <boost/shared_ptr.hpp>

#include "types.h"
#include "uint256.h"

enum HashedObjectType
{
	UNKNOWN=0,
	LEDGER=1,
	TRANSACTION=2,
	ACCOUNT_NODE=3,
	TRANSACTION_NODE=4
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

	bool store() const;
	static HashedObject::pointer retrieve(const uint256& hash);
};

#endif
