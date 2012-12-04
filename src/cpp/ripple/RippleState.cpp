#include "RippleState.h"


AccountItem::pointer RippleState::makeItem(uint160& accountID, SerializedLedgerEntry::pointer ledgerEntry)
{
	if (!mLedgerEntry || mLedgerEntry->getType() != ltRIPPLE_STATE) return(AccountItem::pointer());
	RippleState* rs=new RippleState(ledgerEntry);
	rs->setViewAccount(accountID);

	return(AccountItem::pointer(rs));
}

RippleState::RippleState(SerializedLedgerEntry::pointer ledgerEntry) : AccountItem(ledgerEntry),
	mValid(false),
	mViewLowest(true)
{
	mLowLimit		= mLedgerEntry->getFieldAmount(sfLowLimit);
	mHighLimit		= mLedgerEntry->getFieldAmount(sfHighLimit);

	mLowID			= RippleAddress::createAccountID(mLowLimit.getIssuer());
	mHighID			= RippleAddress::createAccountID(mHighLimit.getIssuer());

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
