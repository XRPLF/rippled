#ifndef __ACCOUNTSTATE__
#define __ACCOUNTSTATE__

// An account's state
// Used to track information about a local account

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
	AccountState(const NewcoinAddress& AccountID);				// For new accounts
	AccountState(SerializedLedgerEntry::pointer ledgerEntry);	// For accounts in a ledger

	const NewcoinAddress& getAccountID() const { return mAccountID; }
	uint64 getBalance() const { return mLedgerEntry->getIFieldU64(sfBalance); }
	uint32 getSeq() const { return mLedgerEntry->getIFieldU32(sfSequence); }

	SerializedLedgerEntry::pointer getSLE() { return mLedgerEntry; }
	const SerializedLedgerEntry& peekSLE() const { return *mLedgerEntry; }
	SerializedLedgerEntry& peekSLE() { return *mLedgerEntry; }

	std::vector<unsigned char> getRaw() const;
	void addJson(Json::Value& value);
	void dump();
};

#endif
// vim:ts=4
