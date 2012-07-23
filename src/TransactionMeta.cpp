#include "TransactionMeta.h"

bool TransactionMetaNodeEntry::operator<(const TransactionMetaNodeEntry& e) const
{
	if (mType < e.mType) return true;
	if (mType > e.mType) return false;
	return compare(e) < 0;
}

bool TransactionMetaNodeEntry::operator<=(const TransactionMetaNodeEntry& e) const
{
	if (mType < e.mType) return true;
	if (mType > e.mType) return false;
	return compare(e) <= 0;
}

bool TransactionMetaNodeEntry::operator>(const TransactionMetaNodeEntry& e) const
{
	if (mType > e.mType) return true;
	if (mType < e.mType) return false;
	return compare(e) > 0;
}

bool TransactionMetaNodeEntry::operator>=(const TransactionMetaNodeEntry& e) const
{
	if (mType > e.mType) return true;
	if (mType < e.mType) return false;
	return compare(e) >= 0;
}

void TMNEBalance::adjustFirstAmount(const STAmount& a)
{
	mFirstAmount += a;
}

void TMNEBalance::adjustSecondAmount(const STAmount& a)
{
	mSecondAmount += a;
	mFlags |= TMBTwoAmounts;
}

int TMNEBalance::compare(const TransactionMetaNodeEntry&) const
{
	assert(false); // should never be two TMNEBalance entries for the same node (as of now)
	return 0;
}

Json::Value TMNEBalance::getJson(int p) const
{
	Json::Value ret(Json::objectValue);

	if ((mFlags & TMBDestroyed) != 0)
		ret["destroyed"] = "true";
	if ((mFlags & TMBPaidFee) != 0)
		ret["transaction_fee"] = "true";

	if ((mFlags & TMBRipple) != 0)
		ret["type"] = "ripple";
	else if ((mFlags & TMBOffer) != 0)
		ret["type"] = "offer";
	else
		ret["type"] = "account";

	if (!mFirstAmount.isZero())
		ret["amount"] = mFirstAmount.getJson(p);
	if (!mSecondAmount.isZero())
		ret["second_amount"] = mSecondAmount.getJson(p);

	return ret;
}
