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
	typedef const pointer& ref;

	AccountItem(){ }
	AccountItem(SerializedLedgerEntry::ref ledger);
	virtual AccountItem::pointer makeItem(const uint160& accountID, SerializedLedgerEntry::ref ledgerEntry)=0;
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
	void fillItems(const uint160& accountID, Ledger::ref ledger);

public:

	AccountItems(const uint160& accountID, Ledger::ref ledger, AccountItem::pointer ofType);

	std::vector<AccountItem::pointer>& getItems() { return(mItems); }
};

#endif
