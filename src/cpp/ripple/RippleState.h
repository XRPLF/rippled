#ifndef __RIPPLESTATE__
#define __RIPPLESTATE__

//
// A ripple line's state.
// - Isolate ledger entry format.
//

#include "SerializedLedger.h"
#include "AccountItems.h"

#include <boost/shared_ptr.hpp>

class RippleState : public AccountItem
{
public:
	typedef boost::shared_ptr<RippleState> pointer;

private:
	RippleAddress					mLowID;
	RippleAddress					mHighID;

	STAmount						mLowLimit;
	STAmount						mHighLimit;

	uint64							mLowQualityIn;
	uint64							mLowQualityOut;
	uint64							mHighQualityIn;
	uint64							mHighQualityOut;

	STAmount						mBalance;

	bool							mValid;
	bool							mViewLowest;

	RippleState(SerializedLedgerEntry::ref ledgerEntry);	// For accounts in a ledger

public:
	RippleState(){ }
	AccountItem::pointer makeItem(const uint160& accountID, SerializedLedgerEntry::ref ledgerEntry);
	LedgerEntryType getType(){ return(ltRIPPLE_STATE); }

	void					setViewAccount(const uint160& accountID);

	const RippleAddress	getAccountID() const		{ return mViewLowest ? mLowID : mHighID; }
	const RippleAddress	getAccountIDPeer() const	{ return mViewLowest ? mHighID : mLowID; }

	STAmount				getBalance() const			{ return mBalance; }

	STAmount				getLimit() const			{ return mViewLowest ? mLowLimit : mHighLimit; }
	STAmount				getLimitPeer() const		{ return mViewLowest ? mHighLimit : mLowLimit; }

	uint32					getQualityIn() const		{ return((uint32) (mViewLowest ? mLowQualityIn : mHighQualityIn)); }
	uint32					getQualityOut() const		{ return((uint32) (mViewLowest ? mLowQualityOut : mHighQualityOut)); }

	SerializedLedgerEntry::pointer getSLE() { return mLedgerEntry; }
	const SerializedLedgerEntry& peekSLE() const { return *mLedgerEntry; }
	SerializedLedgerEntry& peekSLE() { return *mLedgerEntry; }

	std::vector<unsigned char> getRaw() const;
};
#endif
// vim:ts=4
