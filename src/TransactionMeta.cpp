#include "TransactionMeta.h"

#include <algorithm>

#include <boost/make_shared.hpp>
#include <boost/bind.hpp>
#include <boost/foreach.hpp>

TransactionMetaSet::TransactionMetaSet(const uint256& txid, uint32 ledger, const std::vector<unsigned char>& vec) :
	mTransactionID(txid), mLedger(ledger), mNodes(sfAffectedNodes)
{
	Serializer s(vec);
	SerializerIterator sit(s);

	std::auto_ptr<SerializedType> pobj = STObject::deserialize(sit, sfAffectedNodes);
	STObject *obj = static_cast<STObject*>(pobj.get());
	if (!obj)
		throw std::runtime_error("bad metadata");

	mResult = obj->getFieldU8(sfTransactionResult);
	mNodes = * dynamic_cast<STArray*>(&obj->getField(sfAffectedNodes));
}

bool TransactionMetaSet::isNodeAffected(const uint256& node) const
{
	for (STArray::const_iterator it = mNodes.begin(); it != mNodes.end(); ++it)
		if (it->getFieldH256(sfLedgerIndex) == node)
			return true;
	return false;
}

STObject& TransactionMetaSet::getAffectedNode(const uint256& node, SField::ref type, bool overrideType)
{
	assert(&type);
	for (STArray::iterator it = mNodes.begin(); it != mNodes.end(); ++it)
	{
		if (it->getFieldH256(sfLedgerIndex) == node)
		{
			if (overrideType)
				it->setFName(type);
			return *it;
		}
	}

	mNodes.push_back(STObject(type));
	STObject& obj = mNodes.back();

	assert(obj.getFName() == type);
	obj.setFieldH256(sfLedgerIndex, node);

	return obj;
}

const STObject& TransactionMetaSet::peekAffectedNode(const uint256& node) const
{
	for (STArray::const_iterator it = mNodes.begin(); it != mNodes.end(); ++it)
		if (it->getFieldH256(sfLedgerIndex) == node)
			return *it;
	throw std::runtime_error("Affected node not found");
}

void TransactionMetaSet::init(const uint256& id, uint32 ledger)
{
	mTransactionID = id;
	mLedger = ledger;
	mNodes = STArray(sfAffectedNodes);
}

void TransactionMetaSet::swap(TransactionMetaSet& s)
{
	assert((mTransactionID == s.mTransactionID) && (mLedger == s.mLedger));
	mNodes.swap(s.mNodes);
}

bool TransactionMetaSet::thread(STObject& node, const uint256& prevTxID, uint32 prevLgrID)
{
	if (node.getFieldIndex(sfLastTxnID) == -1)
	{
		assert(node.getFieldIndex(sfLastTxnSeq) == -1);
		node.setFieldH256(sfLastTxnID, prevTxID);
		node.setFieldU32(sfLastTxnSeq, prevLgrID);
		return true;
	}
	assert(node.getFieldH256(sfLastTxnID) == prevTxID);
	assert(node.getFieldU32(sfLastTxnSeq) == prevLgrID);
	return false;
}

static bool compare(const STObject& o1, const STObject& o2)
{
	return o1.getFieldH256(sfLedgerIndex) < o2.getFieldH256(sfLedgerIndex);
}

STObject TransactionMetaSet::getAsObject() const
{
	STObject metaData(sfTransactionMetaData);
	metaData.setFieldU8(sfTransactionResult, mResult);
	metaData.addObject(mNodes);
	return metaData;
}

void TransactionMetaSet::addRaw(Serializer& s, TER result)
{
	mResult = static_cast<int>(result);
	assert((mResult == 0) || ((mResult > 100) && (mResult <= 255)));

	mNodes.sort(compare);

	getAsObject().add(s);
}

// vim:ts=4
