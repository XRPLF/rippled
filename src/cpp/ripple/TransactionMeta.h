#ifndef __TRANSACTIONMETA__
#define __TRANSACTIONMETA__

#include <vector>

#include <boost/shared_ptr.hpp>
#include <boost/ptr_container/ptr_vector.hpp>

#include "../json/value.h"

#include "uint256.h"
#include "Serializer.h"
#include "SerializedTypes.h"
#include "SerializedObject.h"
#include "SerializedLedger.h"
#include "TransactionErr.h"

class TransactionMetaSet
{
public:
	typedef boost::shared_ptr<TransactionMetaSet> pointer;

protected:
	uint256	mTransactionID;
	uint32	mLedger;
	int		mResult;

	STArray mNodes;

public:
	TransactionMetaSet() : mLedger(0), mResult(255) { ; }
	TransactionMetaSet(const uint256& txID, uint32 ledger) : mTransactionID(txID), mLedger(ledger), mResult(255) { ; }
	TransactionMetaSet(const uint256& txID, uint32 ledger, const std::vector<unsigned char>&);

	void init(const uint256& transactionID, uint32 ledger);
	void clear() { mNodes.clear(); }
	void swap(TransactionMetaSet&);

	const uint256& getTxID()	{ return mTransactionID; }
	uint32 getLgrSeq()			{ return mLedger; }
	int getResult() const		{ return mResult; }
	TER getResultTER() const	{ return static_cast<TER>(mResult); }

	bool isNodeAffected(const uint256&) const;
	void setAffectedNode(const uint256&, SField::ref type, uint16 nodeType);
	STObject& getAffectedNode(SLE::ref node, SField::ref type); // create if needed
	STObject& getAffectedNode(const uint256&);
	const STObject& peekAffectedNode(const uint256&) const;
	//std::vector<RippleAddress> getAffectedAccounts();


	Json::Value getJson(int p) const { return getAsObject().getJson(p); }
	void addRaw(Serializer&, TER);

	STObject getAsObject() const;

	static bool thread(STObject& node, const uint256& prevTxID, uint32 prevLgrID);
};

#endif

// vim:ts=4
