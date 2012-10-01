#include "RippleState.h"

RippleState::RippleState(SerializedLedgerEntry::pointer ledgerEntry) :
	mLedgerEntry(ledgerEntry),
	mValid(false),
	mViewLowest(true)
{
	if (!mLedgerEntry || mLedgerEntry->getType() != ltRIPPLE_STATE) return;

	mLowLimit		= mLedgerEntry->getValueFieldAmount(sfLowLimit);
	mHighLimit		= mLedgerEntry->getValueFieldAmount(sfHighLimit);

	mLowID			= NewcoinAddress::createAccountID(mLowLimit.getIssuer());
	mHighID			= NewcoinAddress::createAccountID(mHighLimit.getIssuer());

	mLowQualityIn	= mLedgerEntry->getValueFieldU32(sfLowQualityIn);
	mLowQualityOut	= mLedgerEntry->getValueFieldU32(sfLowQualityOut);

	mHighQualityIn	= mLedgerEntry->getValueFieldU32(sfHighQualityIn);
	mHighQualityOut	= mLedgerEntry->getValueFieldU32(sfHighQualityOut);

	mBalance	= mLedgerEntry->getValueFieldAmount(sfBalance);

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
