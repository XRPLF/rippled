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

	mBalance	= mLedgerEntry->getIValueFieldAmount(sfBalance);

	mValid		= true;
}

void RippleState::setViewAccount(const uint160& accountID)
{
	bool	bViewLowestNew	= mLowID.getAccountID() == accountID;

	if (bViewLowestNew != mViewLowest)
	{
		mViewLowest	= bViewLowestNew;
		mBalance.negate();
	}
}

// vim:ts=4
