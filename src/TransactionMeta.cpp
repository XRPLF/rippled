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

void TMNEUnfunded::addRaw(Serializer& sit) const
{
	sit.add8(mType);
}

Json::Value TMNEUnfunded::getJson(int) const
{
	return Json::Value("delete_unfunded");
}

int TMNEUnfunded::compare(const TransactionMetaNodeEntry&) const
{
	assert(false); // Can't be two deletes for same node
	return 0;
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
			mEntries.insert(boost::shared_ptr<TransactionMetaNodeEntry>(new TMNEBalance(sit)));
		if (type == TransactionMetaNodeEntry::TMNDeleteUnfunded)
			mEntries.insert(boost::shared_ptr<TransactionMetaNodeEntry>(new TMNEUnfunded()));
		else if (type != TransactionMetaNodeEntry::TMNEndOfMetadata)
			throw std::runtime_error("Unparseable metadata");
	} while (type != TransactionMetaNodeEntry::TMNEndOfMetadata);
}

void TransactionMetaNode::addRaw(Serializer& s) const
{
	s.add256(mNode);
	s.add256(mPreviousTransaction);
	s.add32(mPreviousLedger);
	for (std::set<TransactionMetaNodeEntry::pointer>::const_iterator it = mEntries.begin(), end = mEntries.end();
			it != end; ++it)
		(*it)->addRaw(s);
	s.add8(TransactionMetaNodeEntry::TMNEndOfMetadata);
}

Json::Value TransactionMetaNode::getJson(int v) const
{
	Json::Value ret = Json::objectValue;
	ret["node"] = mNode.GetHex();
	ret["previous_transaction"] = mPreviousTransaction.GetHex();
	ret["previous_ledger"] = mPreviousLedger;

	Json::Value e = Json::arrayValue;
	for (std::set<TransactionMetaNodeEntry::pointer>::const_iterator it = mEntries.begin(), end = mEntries.end();
			it != end; ++it)
		e.append((*it)->getJson(v));
	ret["entries"] = e;

	return ret;
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

Json::Value TransactionMetaSet::getJson(int v) const
{
	Json::Value ret = Json::objectValue;

	ret["transaction_id"] = mTransactionID.GetHex();
	ret["ledger"] = mLedger;

	Json::Value e = Json::arrayValue;
	for (std::set<TransactionMetaNode>::const_iterator it = mNodes.begin(), end = mNodes.end();
			it != end; ++it)
			e.append(it->getJson(v));
	ret["nodes_affected"] = e;

	return ret;
}

bool TransactionMetaSet::isNodeAffected(const uint256& node) const
{
	for (std::set<TransactionMetaNode>::const_iterator it = mNodes.begin(), end = mNodes.end();
			it != end; ++it)
		if (it->getNode() == node)
			return true;
	return false;
}

TransactionMetaNode TransactionMetaSet::getAffectedNode(const uint256& node)
{
	for (std::set<TransactionMetaNode>::const_iterator it = mNodes.begin(), end = mNodes.end();
			it != end; ++it)
		if (it->getNode() == node)
			return *it;
	return TransactionMetaNode(uint256());
}

const TransactionMetaNode& TransactionMetaSet::peekAffectedNode(const uint256& node) const
{
	for (std::set<TransactionMetaNode>::const_iterator it = mNodes.begin(), end = mNodes.end();
			it != end; ++it)
		if (it->getNode() == node)
			return *it;
	throw std::runtime_error("Affected node not found");
}

void TransactionMetaSet::init(const uint256& id, uint32 ledger)
{
	mTransactionID = id;
	mLedger = ledger;
	mNodes.clear();
}

void TransactionMetaSet::swap(TransactionMetaSet& s)
{
	assert((mTransactionID == s.mTransactionID) && (mLedger == s.mLedger));
	mNodes.swap(s.mNodes);
}
