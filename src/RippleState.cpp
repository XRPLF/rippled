#include "RippleState.h"

RippleState::RippleState(SerializedLedgerEntry::pointer ledgerEntry) :
	mLedgerEntry(ledgerEntry),
	mValid(false),
	mViewLowest(true)
{
	if (!mLedgerEntry || mLedgerEntry->getType() != ltRIPPLE_STATE) return;

	mLowLimit		= mLedgerEntry->getFieldAmount(sfLowLimit);
	mHighLimit		= mLedgerEntry->getFieldAmount(sfHighLimit);

	mLowID			= NewcoinAddress::createAccountID(mLowLimit.getIssuer());
	mHighID			= NewcoinAddress::createAccountID(mHighLimit.getIssuer());

	mLowQualityIn	= mLedgerEntry->getFieldU32(sfLowQualityIn);
	mLowQualityOut	= mLedgerEntry->getFieldU32(sfLowQualityOut);

	mHighQualityIn	= mLedgerEntry->getFieldU32(sfHighQualityIn);
	mHighQualityOut	= mLedgerEntry->getFieldU32(sfHighQualityOut);

	mBalance	= mLedgerEntry->getFieldAmount(sfBalance);

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
