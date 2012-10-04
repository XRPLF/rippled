#ifndef __RIPPLESTATE__
#define __RIPPLESTATE__

//
// A ripple line's state.
// - Isolate ledger entry format.
//

#include "SerializedLedger.h"

#include <boost/shared_ptr.hpp>

class RippleState
{
public:
	typedef boost::shared_ptr<RippleState> pointer;

private:
	SerializedLedgerEntry::pointer	mLedgerEntry;

	NewcoinAddress					mLowID;
	NewcoinAddress					mHighID;

	STAmount						mLowLimit;
	STAmount						mHighLimit;

	uint64							mLowQualityIn;
	uint64							mLowQualityOut;
	uint64							mHighQualityIn;
	uint64							mHighQualityOut;

	STAmount						mBalance;

	bool							mValid;
	bool							mViewLowest;

public:
	RippleState(SerializedLedgerEntry::pointer ledgerEntry);	// For accounts in a ledger

	void					setViewAccount(const uint160& accountID);

	const NewcoinAddress	getAccountID() const		{ return mViewLowest ? mLowID : mHighID; }
	const NewcoinAddress	getAccountIDPeer() const	{ return mViewLowest ? mHighID : mLowID; }

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
