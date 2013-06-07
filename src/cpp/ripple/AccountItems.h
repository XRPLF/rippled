#ifndef __ACCOUNT_ITEMS__
#define __ACCOUNT_ITEMS__

#include "Ledger.h"
#include "SerializedLedger.h"

//
// Fetch ledger entries from an account's owner dir.
//
class AccountItem
{
public:
	typedef boost::shared_ptr<AccountItem> pointer;
	typedef const pointer& ref;

	AccountItem(){ }
	AccountItem(SerializedLedgerEntry::ref ledger);
	virtual ~AccountItem() { ; }
	virtual AccountItem::pointer makeItem(const uint160& accountID, SerializedLedgerEntry::ref ledgerEntry)=0;
	virtual LedgerEntryType getType()=0;
	virtual Json::Value getJson(int)=0;

	SerializedLedgerEntry::pointer getSLE() { return mLedgerEntry; }
	const SerializedLedgerEntry& peekSLE() const { return *mLedgerEntry; }
	SerializedLedgerEntry& peekSLE() { return *mLedgerEntry; }

	std::vector<unsigned char> getRaw() const;

    // VFALCO TODO make an accessor for mLedgerEntry so we can change protected to private
protected:
	SerializedLedgerEntry::pointer	mLedgerEntry;
};

class AccountItems
{
public:
	typedef boost::shared_ptr<AccountItems> pointer;

	AccountItems(const uint160& accountID, Ledger::ref ledger, AccountItem::pointer ofType);

	std::vector<AccountItem::pointer>& getItems() { return(mItems); }
	Json::Value getJson(int);

private:
	AccountItem::pointer mOfType;

	std::vector<AccountItem::pointer> mItems;
	void fillItems(const uint160& accountID, Ledger::ref ledger);
};

#endif

// vim:ts=4
