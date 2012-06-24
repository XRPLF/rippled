#ifndef __SHAMAPYSNC__
#define __SHAMAPSYNC__

#include "SHAMap.h"
#include "Application.h"

// Sync filters allow low-level SHAMapSync code to interact correctly with
// higher-level structures such as caches and transaction stores

class ConsensusTransSetSF : public SHAMapSyncFilter
{ // sync filter for transaction sets during consensus building
public:
	ConsensusTransSetSF() { ; }
	virtual void gotNode(const SHAMapNode& id, const uint256& nodeHash,
		const std::vector<unsigned char>& nodeData, bool isLeaf)
	{
		// WRITEME: If 'isLeaf' is true, this is a transaction
		theApp->getTempNodeCache().store(nodeHash, nodeData);
	}
	virtual bool haveNode(const SHAMapNode& id, const uint256& nodeHash, std::vector<unsigned char>& nodeData)
	{
		// WRITEME: We could check our own map, we could check transaction tables
		return theApp->getTempNodeCache().retrieve(nodeHash, nodeData);
	}
};

class AccountStateSF : public SHAMapSyncFilter
{ // sync filter for account state nodes during ledger sync
protected:
	uint256 mLedgerHash;
	uint32 mLedgerSeq;

public:
	AccountStateSF(const uint256& ledgerHash, uint32 ledgerSeq) : mLedgerHash(ledgerHash), mLedgerSeq(ledgerSeq)
	{ ; }

	virtual void gotNode(const SHAMapNode& id, const uint256& nodeHash,
		const std::vector<unsigned char>& nodeData, bool isLeaf)
	{
		theApp->getHashedObjectStore().store(ACCOUNT_NODE, mLedgerSeq, nodeData, nodeHash);
	}
	virtual bool haveNode(const SHAMapNode& id, const uint256& nodeHash, std::vector<unsigned char>& nodeData)
	{ // fetchNode already tried
		return false;
	}
};

class TransactionStateSF : public SHAMapSyncFilter
{ // sync filter for transactions tree during ledger sync
protected:
	uint256 mLedgerHash;
	uint32 mLedgerSeq;

public:
	TransactionStateSF(const uint256& ledgerHash, uint32 ledgerSeq) : mLedgerHash(ledgerHash), mLedgerSeq(ledgerSeq)
	{ ; }

	virtual void gotNode(const SHAMapNode& id, const uint256& nodeHash,
		const std::vector<unsigned char>& nodeData, bool isLeaf)
	{
		theApp->getHashedObjectStore().store(isLeaf ? TRANSACTION : TRANSACTION_NODE, mLedgerSeq, nodeData, nodeHash);
	}
	virtual bool haveNode(const SHAMapNode& id, const uint256& nodeHash, std::vector<unsigned char>& nodeData)
	{ // fetchNode already tried
		return false;
	}
};

#endif
