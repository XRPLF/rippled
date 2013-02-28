#include "RippleState.h"


AccountItem::pointer RippleState::makeItem(const uint160& accountID, SerializedLedgerEntry::ref ledgerEntry)
{
	if (!ledgerEntry || ledgerEntry->getType() != ltRIPPLE_STATE) return(AccountItem::pointer());
	RippleState* rs=new RippleState(ledgerEntry);
	rs->setViewAccount(accountID);

	return(AccountItem::pointer(rs));
}

RippleState::RippleState(SerializedLedgerEntry::ref ledgerEntry) : AccountItem(ledgerEntry),
	mValid(false), mViewLowest(true)
{
	for (int i = 0, iMax = mLedgerEntry->getCount(); i < iMax; ++i)
	{
		const SerializedType* entry = mLedgerEntry->peekAtPIndex(i);
		assert(entry);

		if (entry->getFName() == sfFlags)
			mFlags = static_cast<const STUInt32*>(entry)->getValue();
		else if (entry->getFName() == sfLowLimit)
		{
			mLowLimit = *static_cast<const STAmount*>(entry);
			mLowID = RippleAddress::createAccountID(mLowLimit.getIssuer());
		}
		else if (entry->getFName() == sfHighLimit)
		{
			mHighLimit = *static_cast<const STAmount*>(entry);
			mHighID = RippleAddress::createAccountID(mHighLimit.getIssuer());
		}
		else if (entry->getFName() == sfLowQualityIn)
			mLowQualityIn = static_cast<const STUInt32*>(entry)->getValue();
		else if (entry->getFName() == sfHighQualityIn)
			mHighQualityIn = static_cast<const STUInt32*>(entry)->getValue();
		else if (entry->getFName() == sfBalance)
			mBalance = *static_cast<const STAmount*>(entry);
	}

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
