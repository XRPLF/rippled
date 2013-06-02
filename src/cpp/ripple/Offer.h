#ifndef RIPPLE_OFFER_H
#define RIPPLE_OFFER_H

#include "AccountItems.h"

class Offer : public AccountItem
{
	RippleAddress mAccount;
	STAmount mTakerGets;
	STAmount mTakerPays;
	int mSeq;


	Offer(SerializedLedgerEntry::pointer ledgerEntry);	// For accounts in a ledger
public:
	Offer(){}
	virtual ~Offer(){}
	AccountItem::pointer makeItem(const uint160&, SerializedLedgerEntry::ref ledgerEntry);
	LedgerEntryType getType(){ return(ltOFFER); }

	const STAmount& getTakerPays(){ return(mTakerPays); }
	const STAmount& getTakerGets(){ return(mTakerGets); }
	const RippleAddress& getAccount(){ return(mAccount); }
	int getSeq(){ return(mSeq); }
	Json::Value getJson(int);

};

#endif

// vim:ts=4
