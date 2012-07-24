#include "TransactionMeta.h"

#include <boost/make_shared.hpp>
#include <boost/bind.hpp>

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

TMNEBalance::TMNEBalance(SerializerIterator& sit) : TransactionMetaNodeEntry(TMNChangedBalance)
{
	mFlags = sit.get32();
	mFirstAmount = * dynamic_cast<STAmount*>(STAmount::deserialize(sit, "FirstAmount").get());
	if ((mFlags & TMBTwoAmounts) != 0)
		mSecondAmount = * dynamic_cast<STAmount*>(STAmount::deserialize(sit, "SecondAmount").get());
}

void TMNEBalance::addRaw(Serializer& sit) const
{
	sit.add8(mType);
	sit.add32(mFlags);
	mFirstAmount.add(sit);
	if ((mFlags & TMBTwoAmounts) != 0)
		mSecondAmount.add(sit);
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

TransactionMetaNode::TransactionMetaNode(const uint256& node, SerializerIterator& sit) : mNode(node)
{
	mNode = sit.get256();
	mPreviousTransaction = sit.get256();
	mPreviousLedger = sit.get32();
	int type;
	do
	{
		type = sit.get8();
		if (type == TransactionMetaNodeEntry::TMNChangedBalance)
			mEntries.push_back(boost::shared_ptr<TransactionMetaNodeEntry>(new TMNEBalance(sit)));
		else if (type != TransactionMetaNodeEntry::TMNEndOfMetadata)
			throw std::runtime_error("Unparseable metadata");
	} while (type != TransactionMetaNodeEntry::TMNEndOfMetadata);
}

void TransactionMetaNode::addRaw(Serializer& s) const
{
	s.add256(mNode);
	s.add256(mPreviousTransaction);
	s.add32(mPreviousLedger);
	for (std::list<TransactionMetaNodeEntry::pointer>::const_iterator it = mEntries.begin(), end = mEntries.end();
			it != end; ++it)
		(*it)->addRaw(s);
	s.add8(TransactionMetaNodeEntry::TMNEndOfMetadata);
}

TransactionMetaSet::TransactionMetaSet(uint32 ledger, const std::vector<unsigned char>& vec) : mLedger(ledger)
{
	Serializer s(vec);
	SerializerIterator sit(s);

	mTransactionID = sit.get256();

	do
	{
		uint256 node = sit.get256();
		if (node.isZero())
			break;
		mNodes.insert(TransactionMetaNode(node, sit));
	} while(1);
}

void TransactionMetaSet::addRaw(Serializer& s) const
{
	s.add256(mTransactionID);
	for (std::set<TransactionMetaNode>::const_iterator it = mNodes.begin(), end = mNodes.end();
			it != end; ++it)
		it->addRaw(s);
	s.add256(uint256());
}
