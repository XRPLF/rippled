#ifndef SHAMAPSYNC_H
#define SHAMAPSYNC_H

#include "SHAMap.h"
#include "Application.h"

// Sync filters allow low-level SHAMapSync code to interact correctly with
// higher-level structures such as caches and transaction stores

// This class is needed on both add and check functions
class ConsensusTransSetSF : public SHAMapSyncFilter
{ // sync filter for transaction sets during consensus building
public:
	ConsensusTransSetSF() { ; }

	virtual void gotNode(bool fromFilter, const SHAMapNode& id, const uint256& nodeHash,
		const std::vector<unsigned char>& nodeData, SHAMapTreeNode::TNType);

	virtual bool haveNode(const SHAMapNode& id, const uint256& nodeHash, std::vector<unsigned char>& nodeData);
};

// This class is only needed on add functions
// sync filter for account state nodes during ledger sync
class AccountStateSF : public SHAMapSyncFilter
{
public:
	AccountStateSF(uint32 ledgerSeq) : mLedgerSeq(ledgerSeq)
	{ ; }

	virtual void gotNode(bool fromFilter, const SHAMapNode& id, const uint256& nodeHash,
		const std::vector<unsigned char>& nodeData, SHAMapTreeNode::TNType)
	{
		theApp->getHashedObjectStore().store(hotACCOUNT_NODE, mLedgerSeq, nodeData, nodeHash);
	}
	virtual bool haveNode(const SHAMapNode& id, const uint256& nodeHash, std::vector<unsigned char>& nodeData)
	{
		return theApp->getOPs().getFetchPack(nodeHash, nodeData);
	}

private:
	uint32 mLedgerSeq;
};

// This class is only needed on add functions
// sync filter for transactions tree during ledger sync
class TransactionStateSF : public SHAMapSyncFilter
{ 
public:
	TransactionStateSF(uint32 ledgerSeq) : mLedgerSeq(ledgerSeq)
	{ ; }

	virtual void gotNode(bool fromFilter, const SHAMapNode& id, const uint256& nodeHash,
		const std::vector<unsigned char>& nodeData, SHAMapTreeNode::TNType type)
	{
		theApp->getHashedObjectStore().store(
			(type == SHAMapTreeNode::tnTRANSACTION_NM) ? hotTRANSACTION : hotTRANSACTION_NODE,
			mLedgerSeq, nodeData, nodeHash);
	}
	virtual bool haveNode(const SHAMapNode& id, const uint256& nodeHash, std::vector<unsigned char>& nodeData)
	{
		return theApp->getOPs().getFetchPack(nodeHash, nodeData);
	}

private:
	uint32 mLedgerSeq;
};

#endif
