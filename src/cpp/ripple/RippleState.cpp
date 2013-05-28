

AccountItem::pointer RippleState::makeItem(const uint160& accountID, SerializedLedgerEntry::ref ledgerEntry)
{
	if (!ledgerEntry || ledgerEntry->getType() != ltRIPPLE_STATE)
		return AccountItem::pointer();
	RippleState* rs = new RippleState(ledgerEntry);
	rs->setViewAccount(accountID);

	return AccountItem::pointer(rs);
}

RippleState::RippleState(SerializedLedgerEntry::ref ledgerEntry) : AccountItem(ledgerEntry),
	mValid(false),
	mViewLowest(true),

	mLowLimit(ledgerEntry->getFieldAmount(sfLowLimit)),
	mHighLimit(ledgerEntry->getFieldAmount(sfHighLimit)),

	mLowID(mLowLimit.getIssuer()),
	mHighID(mHighLimit.getIssuer()),

	mBalance(ledgerEntry->getFieldAmount(sfBalance))
{
	mFlags			= mLedgerEntry->getFieldU32(sfFlags);

	mLowQualityIn	= mLedgerEntry->getFieldU32(sfLowQualityIn);
	mLowQualityOut	= mLedgerEntry->getFieldU32(sfLowQualityOut);

	mHighQualityIn	= mLedgerEntry->getFieldU32(sfHighQualityIn);
	mHighQualityOut	= mLedgerEntry->getFieldU32(sfHighQualityOut);

	mValid		= true;
}

void RippleState::setViewAccount(const uint160& accountID)
{
	bool	bViewLowestNew	= mLowID == accountID;

	if (bViewLowestNew != mViewLowest)
	{
		mViewLowest	= bViewLowestNew;
		mBalance.negate();
	}
}

Json::Value RippleState::getJson(int)
{
	Json::Value ret(Json::objectValue);
	ret["low_id"] = mLowID.GetHex();
	ret["high_id"] = mHighID.GetHex();
	return ret;
}

// vim:ts=4
