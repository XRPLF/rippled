#ifndef __TRANSACTIONMETA__
#define __TRANSACTIONMETA__

#include <vector>
#include <set>

#include <boost/shared_ptr.hpp>

#include "../json/value.h"

#include "uint256.h"
#include "Serializer.h"
#include "SerializedTypes.h"


class TransactionMetaNodeEntry
{ // a way that a transaction has affected a node
public:

	typedef boost::shared_ptr<TransactionMetaNodeEntry> pointer;

	static const int TMNEndOfMetadata =	0;
	static const int TMNChangedBalance = 1;

	int mType;
	TransactionMetaNodeEntry(int type) : mType(type) { ; }

	int getType() const { return mType; }
	virtual Json::Value getJson(int) const = 0;
	virtual void addRaw(Serializer&) const = 0;
	virtual int compare(const TransactionMetaNodeEntry&) const = 0;

	bool operator<(const TransactionMetaNodeEntry&) const;
	bool operator<=(const TransactionMetaNodeEntry&) const;
	bool operator>(const TransactionMetaNodeEntry&) const;
	bool operator>=(const TransactionMetaNodeEntry&) const;
};

class TMNEBalance : public TransactionMetaNodeEntry
{ // a transaction affected the balance of a node
public:

	static const int TMBTwoAmounts 	= 0x001;
	static const int TMBDestroyed	= 0x010;
	static const int TMBPaidFee		= 0x020;
	static const int TMBRipple		= 0x100;
	static const int TMBOffer		= 0x200;

protected:
	unsigned mFlags;
	STAmount mFirstAmount, mSecondAmount;

public:
	TMNEBalance() : TransactionMetaNodeEntry(TMNChangedBalance), mFlags(0) { ; }

	TMNEBalance(SerializerIterator&);
	virtual void addRaw(Serializer&) const;

	unsigned getFlags() const				{ return mFlags; }
	const STAmount& getFirstAmount() const	{ return mFirstAmount; }
	const STAmount& getSecondAmount() const	{ return mSecondAmount; }

	void adjustFirstAmount(const STAmount&);
	void adjustSecondAmount(const STAmount&);
	void setFlags(unsigned flags);

	virtual Json::Value getJson(int) const;
	virtual int compare(const TransactionMetaNodeEntry&) const;
};

class TransactionMetaNode
{ // a node that has been affected by a transaction
public:
	typedef boost::shared_ptr<TransactionMetaNode> pointer;

protected:
	uint256 mNode;
	uint256 mPreviousTransaction;
	uint32 mPreviousLedger;
	std::set<TransactionMetaNodeEntry::pointer> mEntries;

public:
	TransactionMetaNode(const uint256 &node) : mNode(node) { ; }

	const uint256& getNode() const												{ return mNode; }
	const uint256& getPreviousTransaction() const								{ return mPreviousTransaction; }
	uint32 getPreviousLedger() const											{ return mPreviousLedger; }
	const std::set<TransactionMetaNodeEntry::pointer>& peekEntries() const		{ return mEntries; }

	bool operator<(const TransactionMetaNode& n) const	{ return mNode < n.mNode; }
	bool operator<=(const TransactionMetaNode& n) const	{ return mNode <= n.mNode; }
	bool operator>(const TransactionMetaNode& n) const	{ return mNode > n.mNode; }
	bool operator>=(const TransactionMetaNode& n) const	{ return mNode >= n.mNode; }

	TransactionMetaNode(const uint256&node, SerializerIterator&);
	void addRaw(Serializer&) const;
	Json::Value getJson(int) const;
};

class TransactionMetaSet
{
protected:
	uint256 mTransactionID;
	uint32 mLedger;
	std::set<TransactionMetaNode> mNodes;

public:
	TransactionMetaSet(const uint256& txID, uint32 ledger) : mTransactionID(txID), mLedger(ledger)
	{ ; }
	TransactionMetaSet(uint32 ledger, const std::vector<unsigned char>&);

	bool isNodeAffected(const uint256&) const;
	TransactionMetaNode getAffectedNode(const uint256&);
	const TransactionMetaNode& peekAffectedNode(const uint256&) const;

	Json::Value getJson(int) const;
	void addRaw(Serializer&) const;

	void threadNode(const uint256& node, const uint256& previousTransaction, uint32 previousLedger);
	bool signedBy(const uint256& node);
	bool adjustBalance(const uint256& node, unsigned flags, const STAmount &amount);
	bool adjustBalances(const uint256& node, unsigned flags, const STAmount &firstAmt, const STAmount &secondAmt);

};

#endif
