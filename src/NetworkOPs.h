#ifndef __NETWORK_OPS__
#define __NETWORK_OPS__

#include "Transaction.h"
#include "AccountState.h"

// Operations that clients may wish to perform against the network

class Peer;

class NetworkOPs
{
	enum Fault
	{ // exceptions these functions can throw
		IO_ERROR=1,
		NO_NETWORK=2,
	};

	enum OperatingMode
	{ // how we process transactions or account balance requests
		FAULTED=0,		// we are unable to process requests (not ready or no network)
		FULL_LOCAL=1,	// we are in full local sync
		PART_LOCAL=2,	// we can validate remote data but have to request it
		REMOTE=3		// we have to trust remote nodes
	};

public:

	// context information
	OperatingMode getOperatingMode();

	// network information
	uint64 getNetworkTime();
	uint32 getCurrentLedgerID();

	// transaction operations
	Transaction::pointer processTransaction(Transaction::pointer transaction, Peer* source=NULL);
	Transaction::pointer findTransactionByID(const uint256& transactionID);
	int findTransactionsBySource(std::list<Transaction::pointer>&, const NewcoinAddress& sourceAccount,
		uint32 minSeq, uint32 maxSeq);
	int findTransactionsByDestination(std::list<Transaction::pointer>&, const NewcoinAddress& destinationAccount,
		uint32 startLedgerSeq, uint32 endLedgerSeq, int maxTransactions);

	// account operations
	AccountState::pointer getAccountState(const NewcoinAddress& accountID);

	// contact block operations

	// raw object operations
	bool findRawLedger(const uint256& ledgerHash, std::vector<unsigned char>& rawLedger);
	bool findRawTransaction(const uint256& transactionHash, std::vector<unsigned char>& rawTransaction);
	bool findAccountNode(const uint256& nodeHash, std::vector<unsigned char>& rawAccountNode);
	bool findTransactionNode(const uint256& nodeHash, std::vector<unsigned char>& rawTransactionNode);

	// tree synchronzation operations
	bool getTransactionTreeNodes(uint32 ledgerSeq, const uint256& myNodeID,
		const std::vector<unsigned char>& myNode, std::list<std::vector<unsigned char> >& newNodes);
	bool getAccountStateNodes(uint32 ledgerSeq, const uint256& myNodeId,
		const std::vector<unsigned char>& myNode, std::list<std::vector<unsigned char> >& newNodes);
};

#endif
