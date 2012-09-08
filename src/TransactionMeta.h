#ifndef __TRANSACTIONMETA__
#define __TRANSACTIONMETA__

#include <vector>

#include <boost/shared_ptr.hpp>
#include <boost/ptr_container/ptr_vector.hpp>

#include "../json/value.h"

#include "uint256.h"
#include "Serializer.h"
#include "SerializedTypes.h"

// master record types
static const int TMNEndOfMetadata		= 0x00;
static const int TMNCreatedNode			= 0x10;	// This transaction created this node
static const int TMNDeletedNode			= 0x11;
static const int TMNModifiedNode		= 0x12;

// sub record types - special
static const int TMSEndOfNode			= 0x00;
static const int TMSThread				= 0x01; // Holds previous TxID and LgrSeq for threading

// sub record types - containing an amount
static const int TMSPrevBalance			= 0x11; // Balances prior to the transaction
static const int TMSFinalBalance		= 0x12; // deleted with non-zero balance
static const int TMSPrevTakerPays		= 0x13;
static const int TMSPrevTakerGets		= 0x14;
static const int TMSFinalTakerPays		= 0x15; // Balances at node deletion time
static const int TMSFinalTakerGets		= 0x16;

// sub record types - containing an account (for example, for when a nickname is transferred)
static const int TMSPrevAccount			= 0x20;
static const int TMSLowID				= 0x21;
static const int TMSHighID				= 0x22;


class TransactionMetaNodeEntry
{ // a way that a transaction has affected a node
public:
	typedef boost::shared_ptr<TransactionMetaNodeEntry> pointer;

protected:
	int mType;

public:
	TransactionMetaNodeEntry(int type) : mType(type) { ; }

	int getType() const { return mType; }
	virtual Json::Value getJson(int) const = 0;
	virtual void addRaw(Serializer&) const = 0;

	bool operator<(const TransactionMetaNodeEntry&) const;
	bool operator<=(const TransactionMetaNodeEntry&) const;
	bool operator>(const TransactionMetaNodeEntry&) const;
	bool operator>=(const TransactionMetaNodeEntry&) const;

	virtual std::auto_ptr<TransactionMetaNodeEntry> clone() const
	{ return std::auto_ptr<TransactionMetaNodeEntry>(duplicate()); }

protected:
	virtual int compare(const TransactionMetaNodeEntry&) const = 0;
	virtual TransactionMetaNodeEntry* duplicate(void) const = 0;
};

class TMNEThread : public TransactionMetaNodeEntry
{
protected:
	uint256 mPrevTxID;
	uint32 mPrevLgrSeq;

public:
	TMNEThread() : TransactionMetaNodeEntry(TMSThread), mPrevLgrSeq(0) { ; }
	TMNEThread(uint256 prevTx, uint32 prevLgrSeq)
		: TransactionMetaNodeEntry(TMSThread), mPrevTxID(prevTx), mPrevLgrSeq(prevLgrSeq)
	{ ; }
	TMNEThread(SerializerIterator&);

	virtual void addRaw(Serializer&) const;
	virtual Json::Value getJson(int) const;

protected:
	virtual TransactionMetaNodeEntry* duplicate(void) const { return new TMNEThread(*this); }
	virtual int compare(const TransactionMetaNodeEntry&) const;
};

class TMNEAmount : public TransactionMetaNodeEntry
{ // a transaction affected the balance of a node
protected:
	STAmount mAmount;

public:
	TMNEAmount(int type) : TransactionMetaNodeEntry(type) { ; }
	TMNEAmount(int type, const STAmount &a) : TransactionMetaNodeEntry(type), mAmount(a) { ; }

	TMNEAmount(int type, SerializerIterator&);
	virtual void addRaw(Serializer&) const;

	const STAmount& getAmount() const	{ return mAmount; }
	void setAmount(const STAmount& a)	{ mAmount = a; }

	virtual Json::Value getJson(int) const;

protected:
	virtual TransactionMetaNodeEntry* duplicate(void) const { return new TMNEAmount(*this); }
	virtual int compare(const TransactionMetaNodeEntry&) const;
};

class TMNEAccount : public TransactionMetaNodeEntry
{ // node was deleted because it was unfunded
protected:
	NewcoinAddress mAccount;

public:
	TMNEAccount(int type, const NewcoinAddress& acct) : TransactionMetaNodeEntry(type), mAccount(acct) { ; }
	TMNEAccount(int type, SerializerIterator&);
	virtual void addRaw(Serializer&) const;
	virtual Json::Value getJson(int) const;

	const NewcoinAddress& getAccount() const		{ return mAccount; }
	void setAccount(const NewcoinAddress& a)		{ mAccount = a; }

protected:
	virtual TransactionMetaNodeEntry* duplicate(void) const { return new TMNEAccount(*this); }
	virtual int compare(const TransactionMetaNodeEntry&) const;
};

inline TransactionMetaNodeEntry* new_clone(const TransactionMetaNodeEntry& s)	{ return s.clone().release(); }
inline void delete_clone(const TransactionMetaNodeEntry* s)						{ boost::checked_delete(s); }

class TransactionMetaNode
{ // a node that has been affected by a transaction
public:
	typedef boost::shared_ptr<TransactionMetaNode> pointer;

protected:
	int mType;
	uint256 mNode;
	boost::ptr_vector<TransactionMetaNodeEntry> mEntries;

public:
	TransactionMetaNode(const uint256 &node, int type) : mType(type), mNode(node) { ; }

	const uint256& getNode() const												{ return mNode; }
	const boost::ptr_vector<TransactionMetaNodeEntry>& peekEntries() const		{ return mEntries; }

	TransactionMetaNodeEntry* findEntry(int nodeType);
	void addNode(TransactionMetaNodeEntry*);

	bool operator<(const TransactionMetaNode& n) const	{ return mNode < n.mNode; }
	bool operator<=(const TransactionMetaNode& n) const	{ return mNode <= n.mNode; }
	bool operator>(const TransactionMetaNode& n) const	{ return mNode > n.mNode; }
	bool operator>=(const TransactionMetaNode& n) const	{ return mNode >= n.mNode; }

	bool thread(const uint256& prevTx, uint32 prevLgr);

	TransactionMetaNode(int type, const uint256& node, SerializerIterator&);
	void addRaw(Serializer&);
	Json::Value getJson(int) const;

	bool addAmount(int nodeType, const STAmount& amount);
	bool addAccount(int nodeType, const NewcoinAddress& account);
	TMNEAmount* findAmount(int nodeType);
};


class TransactionMetaSet
{
protected:
	uint256 mTransactionID;
	uint32 mLedger;
	std::map<uint256, TransactionMetaNode> mNodes;

public:
	TransactionMetaSet() : mLedger(0) { ; }
	TransactionMetaSet(const uint256& txID, uint32 ledger) : mTransactionID(txID), mLedger(ledger) { ; }
	TransactionMetaSet(uint32 ledger, const std::vector<unsigned char>&);

	void init(const uint256& transactionID, uint32 ledger);
	void clear() { mNodes.clear(); }
	void swap(TransactionMetaSet&);

	const uint256& getTxID()	{ return mTransactionID; }
	uint32 getLgrSeq()			{ return mLedger; }

	bool isNodeAffected(const uint256&) const;
	TransactionMetaNode& getAffectedNode(const uint256&, int type);
	const TransactionMetaNode& peekAffectedNode(const uint256&) const;

	Json::Value getJson(int) const;
	void addRaw(Serializer&);
};

#endif

// vim:ts=4
