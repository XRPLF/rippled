#ifndef RIPPLE_OFFER_H
#define RIPPLE_OFFER_H

class Offer : public AccountItem
{
public:
	Offer () {}
	
    virtual ~Offer(){}
	
    AccountItem::pointer makeItem (const uint160&, SerializedLedgerEntry::ref ledgerEntry);
	
    LedgerEntryType getType(){ return(ltOFFER); }

	const STAmount& getTakerPays(){ return(mTakerPays); }
	const STAmount& getTakerGets(){ return(mTakerGets); }
	const RippleAddress& getAccount(){ return(mAccount); }
	int getSeq(){ return(mSeq); }
	Json::Value getJson(int);

private:
    // For accounts in a ledger
	explicit Offer (SerializedLedgerEntry::pointer ledgerEntry);

private:
	RippleAddress mAccount;
	STAmount mTakerGets;
	STAmount mTakerPays;
	int mSeq;
};

#endif

// vim:ts=4
