#ifndef __ACCOUNT_ITEMS__
#define __ACCOUNT_ITEMS__

#include "Ledger.h"
#include "SerializedLedger.h"

/*
Way to fetch ledger entries from an account's owner dir
*/
class AccountItem
{
protected:
	SerializedLedgerEntry::pointer	mLedgerEntry;
public:
	typedef boost::shared_ptr<AccountItem> pointer;
	AccountItem(){ }
	AccountItem(SerializedLedgerEntry::pointer ledger);
	virtual AccountItem::pointer makeItem(uint160& accountID, SerializedLedgerEntry::pointer ledgerEntry)=0;
	virtual LedgerEntryType getType()=0;

	SerializedLedgerEntry::pointer getSLE() { return mLedgerEntry; }
	const SerializedLedgerEntry& peekSLE() const { return *mLedgerEntry; }
	SerializedLedgerEntry& peekSLE() { return *mLedgerEntry; }

	std::vector<unsigned char> getRaw() const;
};

class AccountItems
{
	AccountItem::pointer mOfType;

	std::vector<AccountItem::pointer> mItems;
	void fillItems(uint160& accountID, Ledger::ref ledger);
public:

	AccountItems(uint160& accountID, Ledger::ref ledger, AccountItem::pointer ofType);
	AccountItems(uint160& accountID, AccountItem::pointer ofType ); // looks in the current ledger

	std::vector<AccountItem::pointer>& getItems() { return(mItems); }
};

#endif
