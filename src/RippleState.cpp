#include "RippleState.h"

RippleState::RippleState(SerializedLedgerEntry::pointer ledgerEntry) :
	mLedgerEntry(ledgerEntry),
	mValid(false),
	mViewLowest(true)
{
	if (!mLedgerEntry || mLedgerEntry->getType() != ltRIPPLE_STATE) return;

	mLowID		= mLedgerEntry->getIValueFieldAccount(sfLowID);
	mHighID		= mLedgerEntry->getIValueFieldAccount(sfHighID);

	mLowLimit	= mLedgerEntry->getIValueFieldAmount(sfLowLimit);
	mHighLimit	= mLedgerEntry->getIValueFieldAmount(sfHighLimit);

	// YYY Should never fail.
	if (mLowID.isValid() && mHighID.isValid())
		mValid = true;
}

void RippleState::setViewAccount(const NewcoinAddress& naView)
{
	bool	bViewLowestNew	= mLowID.getAccountID() == naView.getAccountID();

	if (bViewLowestNew != mViewLowest)
	{
		mViewLowest	= bViewLowestNew;
		mBalance.changeSign();
	}
}

// vim:ts=4
