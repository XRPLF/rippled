#ifndef __ACCOUNTSTATE__
#define __ACCOUNTSTATE__

// An account's state in one or more accepted ledgers

#include <vector>

#include <boost/shared_ptr.hpp>

#include "../json/value.h"

#include "types.h"
#include "uint256.h"
#include "NewcoinAddress.h"

class AccountState
{
public:
    typedef boost::shared_ptr<AccountState> pointer;

private:
    NewcoinAddress mAccountID;
    uint64 mBalance;
    uint32 mAccountSeq;
    bool mValid;

public:
	AccountState(const NewcoinAddress& mAccountID);			// new account
	AccountState(const std::vector<unsigned char>&);	// raw form

	const NewcoinAddress& getAccountID() const { return mAccountID; }
	uint64 getBalance() const { return mBalance; }
	uint32 getSeq() const { return mAccountSeq; }

	void credit(const uint64& a)
	{
		mBalance+=a;
		if(!mAccountSeq) mAccountSeq=1; // an account with non-0 balance cannot have 0 sequence
	}
	void charge(const uint64& a)
	{
		assert(mBalance>=a);
		mBalance-=a;
	}
	void incSeq()
	{
		mAccountSeq++;
	}
	void decSeq()
	{
		assert(mAccountSeq!=0);
		mAccountSeq--;
	}

	std::vector<unsigned char> getRaw() const;
	void addJson(Json::Value& value);
};

#endif
// vim:ts=4
