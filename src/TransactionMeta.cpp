#include "TransactionMeta.h"

#include <algorithm>

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

TMNEThread::TMNEThread(SerializerIterator& sit) : TransactionMetaNodeEntry(TMSThread)
{
	mPrevTxID = sit.get256();
	mPrevLgrSeq = sit.get32();
}

void TMNEThread::addRaw(Serializer& sit) const
{
	sit.add8(mType);
	sit.add256(mPrevTxID);
	sit.add32(mPrevLgrSeq);
}

int TMNEThread::compare(const TransactionMetaNodeEntry&) const
{
	assert(false); // should never be two entries for the same node (as of now)
	return 0;
}

Json::Value TMNEThread::getJson(int) const
{
	Json::Value inner(Json::objectValue);
	inner["prev_transaction"] = mPrevTxID.GetHex();
	inner["prev_ledger_seq"] = mPrevLgrSeq;

	Json::Value outer(Json::objectValue);
	outer["thread"] = inner;
	return outer;
}

TMNEAmount::TMNEAmount(int type, SerializerIterator& sit) : TransactionMetaNodeEntry(type)
{
	mPrevAmount = *dynamic_cast<STAmount*>(STAmount::deserialize(sit, NULL).get()); // Ouch
}

void TMNEAmount::addRaw(Serializer& s) const
{
	s.add8(mType);
	mPrevAmount.add(s);
}

Json::Value TMNEAmount::getJson(int v) const
{
	Json::Value outer(Json::objectValue);
	switch (mType)
	{
		case TMSPrevBalance:		outer["prev_balance"] = mPrevAmount.getJson(v); break;
		case TMSPrevTakerPays:		outer["prev_taker_pays"] = mPrevAmount.getJson(v); break;
		case TMSPrevTakerGets:		outer["prev_taker_gets"] = mPrevAmount.getJson(v); break;
		case TMSFinalTakerPays:		outer["final_taker_pays"] = mPrevAmount.getJson(v); break;
		case TMSFinalTakerGets:		outer["final_taker_gets"] = mPrevAmount.getJson(v); break;
		default: assert(false);
	}
	return outer;
}

int TMNEAmount::compare(const TransactionMetaNodeEntry& e) const
{
	assert(getType() != e.getType());
	return getType() - e.getType();
}

TMNEAccount::TMNEAccount(int type, SerializerIterator& sit) : TransactionMetaNodeEntry(type)
{
	mPrevAccount = sit.get256();
}

void TMNEAccount::addRaw(Serializer& sit) const
{
	sit.add8(mType);
	sit.add256(mPrevAccount);
}

Json::Value TMNEAccount::getJson(int) const
{
	Json::Value outer(Json::objectValue);
	outer["prev_account"] = mPrevAccount.GetHex();
	return outer;
}

int TMNEAccount::compare(const TransactionMetaNodeEntry&) const
{
	assert(false); // Can't be two modified accounts of same type for same node
	return 0;
}

TransactionMetaNode::TransactionMetaNode(int type, const uint256& node, SerializerIterator& sit)
		: mType(type), mNode(node)
{
	while (1)
	{
		int nType = sit.get8();
		switch (nType)
		{
			case TMSEndOfNode:
				return;

			case TMSThread:
				mEntries.push_back(new TMNEThread(sit));
				break;

			// Nodes that contain an amount
			case TMSPrevBalance:
			case TMSPrevTakerPays:
			case TMSPrevTakerGets:
			case TMSFinalTakerPays:
			case TMSFinalTakerGets:
				mEntries.push_back(new TMNEAmount(nType, sit));

			case TMSPrevAccount:
				mEntries.push_back(new TMNEAccount(nType, sit));
		}
	}
}

void TransactionMetaNode::addRaw(Serializer& s)
{
	s.add8(mType);
	s.add256(mNode);
	mEntries.sort();
	for (boost::ptr_vector<TransactionMetaNodeEntry>::const_iterator it = mEntries.begin(), end = mEntries.end();
			it != end; ++it)
		it->addRaw(s);
	s.add8(TMSEndOfNode);
}

TransactionMetaNodeEntry* TransactionMetaNode::findEntry(int nodeType)
{
	for (boost::ptr_vector<TransactionMetaNodeEntry>::iterator it = mEntries.begin(), end = mEntries.end();
			it != end; ++it)
		if (it->getType() == nodeType)
			return &*it;
	return NULL;
}

TMNEAmount* TransactionMetaNode::findAmount(int nType)
{
	for (boost::ptr_vector<TransactionMetaNodeEntry>::iterator it = mEntries.begin(), end = mEntries.end();
			it != end; ++it)
		if (it->getType() == nType)
			return dynamic_cast<TMNEAmount *>(&*it);
	TMNEAmount* node = new TMNEAmount(nType);
	mEntries.push_back(node);
	return node;
}

void TransactionMetaNode::addNode(TransactionMetaNodeEntry* node)
{
	mEntries.push_back(node);
}

void TransactionMetaNode::thread(const uint256& prevTx, uint32 prevLgr)
{
	// WRITEME
}

Json::Value TransactionMetaNode::getJson(int v) const
{
	Json::Value ret = Json::objectValue;

	switch (mType)
	{
		case TMNCreatedNode:	ret["action"] = "create"; break;
		case TMNDeletedNode:	ret["action"] = "delete"; break;
		case TMNModifiedNode:	ret["action"] = "modify"; break;
		default:
			assert(false);
	}

	ret["node"] = mNode.GetHex();

	Json::Value e = Json::arrayValue;
	for (boost::ptr_vector<TransactionMetaNodeEntry>::const_iterator it = mEntries.begin(), end = mEntries.end();
			it != end; ++it)
		e.append(it->getJson(v));
	ret["entries"] = e;

	return ret;
}

TransactionMetaSet::TransactionMetaSet(uint32 ledger, const std::vector<unsigned char>& vec) : mLedger(ledger)
{
	Serializer s(vec);
	SerializerIterator sit(s);

	mTransactionID = sit.get256();

	int type;
	while ((type = sit.get8()) != TMNEndOfMetadata)
	{
		uint256 node = sit.get256();
		mNodes.insert(std::make_pair(node, TransactionMetaNode(type, node, sit)));
	}
}

void TransactionMetaSet::addRaw(Serializer& s)
{
	s.add256(mTransactionID);
	for (std::map<uint256, TransactionMetaNode>::iterator it = mNodes.begin(), end = mNodes.end();
			it != end; ++it)
		it->second.addRaw(s);
	s.add8(TMNEndOfMetadata);
}

Json::Value TransactionMetaSet::getJson(int v) const
{
	Json::Value ret = Json::objectValue;

	ret["transaction_id"] = mTransactionID.GetHex();
	ret["ledger"] = mLedger;

	Json::Value e = Json::arrayValue;
	for (std::map<uint256, TransactionMetaNode>::const_iterator it = mNodes.begin(), end = mNodes.end();
			it != end; ++it)
		e.append(it->second.getJson(v));
	ret["nodes_affected"] = e;

	return ret;
}

bool TransactionMetaSet::isNodeAffected(const uint256& node) const
{
	return mNodes.find(node) != mNodes.end();
}

const TransactionMetaNode& TransactionMetaSet::peekAffectedNode(const uint256& node) const
{
	std::map<uint256, TransactionMetaNode>::const_iterator it = mNodes.find(node);
	if (it != mNodes.end())
		return it->second;
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

TransactionMetaNode& TransactionMetaSet::modifyNode(const uint256& node)
{
	std::map<uint256, TransactionMetaNode>::iterator it = mNodes.find(node);
	if (it != mNodes.end())
		return it->second;
	return mNodes.insert(std::make_pair(node, TransactionMetaNode(node))).first->second;
}

#if 0
void TransactionMetaSet::threadNode(const uint256& node, const uint256& prevTx, uint32 prevLgr)
{
	modifyNode(node).thread(prevTx, prevLgr);
}

void TransactionMetaSet::deleteUnfunded(const uint256& nodeID,
	const STAmount& firstBalance, const STAmount &secondBalance)
{
	TransactionMetaNode& node = modifyNode(nodeID);
	TMNEUnfunded* entry = dynamic_cast<TMNEUnfunded*>(node.findEntry(TransactionMetaNodeEntry::TMNDeleteUnfunded));
	if (entry)
		entry->setBalances(firstBalance, secondBalance);
	else
		node.addNode(new TMNEUnfunded(firstBalance, secondBalance));
}
#endif 