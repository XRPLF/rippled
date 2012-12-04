#include "AccountItems.h"


class Offer : public AccountItem
{
	RippleAddress mAccount;
	STAmount mTakerGets;
	STAmount mTakerPays;


	Offer(SerializedLedgerEntry::pointer ledgerEntry);	// For accounts in a ledger
public:
	Offer(){}
	AccountItem::pointer makeItem(uint160&, SerializedLedgerEntry::pointer ledgerEntry);
	LedgerEntryType getType(){ return(ltOFFER); }

	STAmount getTakerPays(){ return(mTakerPays); }
	STAmount getTakerGets(){ return(mTakerGets); }
	RippleAddress getAccount(){ return(mAccount); }

};