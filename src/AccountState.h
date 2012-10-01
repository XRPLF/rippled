#ifndef __ACCOUNTSTATE__
#define __ACCOUNTSTATE__

//
// Provide abstract access to an account's state, such that access to the serialized format is hidden.
//

#include <vector>

#include <boost/shared_ptr.hpp>

#include "../json/value.h"

#include "types.h"
#include "NewcoinAddress.h"
#include "SerializedLedger.h"

class AccountState
{
public:
	typedef boost::shared_ptr<AccountState> pointer;

private:
	NewcoinAddress					mAccountID;
	NewcoinAddress					mAuthorizedKey;
	SerializedLedgerEntry::pointer	mLedgerEntry;

	bool							mValid;

public:
	AccountState(const NewcoinAddress& naAccountID);						// For new accounts
	AccountState(SLE::ref ledgerEntry,const NewcoinAddress& naAccountI);	// For accounts in a ledger

	bool	bHaveAuthorizedKey()
	{
		return mLedgerEntry->isFieldPresent(sfAuthorizedKey);
	}

	NewcoinAddress getAuthorizedKey()
	{
		return mLedgerEntry->getValueFieldAccount(sfAuthorizedKey);
	}

	STAmount getBalance() const { return mLedgerEntry->getValueFieldAmount(sfBalance); }
	uint32 getSeq() const { return mLedgerEntry->getValueFieldU32(sfSequence); }

	SerializedLedgerEntry::pointer getSLE() { return mLedgerEntry; }
	const SerializedLedgerEntry& peekSLE() const { return *mLedgerEntry; }
	SerializedLedgerEntry& peekSLE() { return *mLedgerEntry; }

	std::vector<unsigned char> getRaw() const;
	void addJson(Json::Value& value);
	void dump();

	static std::string createGravatarUrl(uint128 uEmailHash);
};

#endif
// vim:ts=4
