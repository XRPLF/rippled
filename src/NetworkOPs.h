#ifndef __NETWORK_OPS__
#define __NETWORK_OPS__

#include "Transaction.h"
#include "AccountState.h"

// Operations that clients may wish to perform against the network
// Master operational handler, server sequencer, network tracker

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
		DISCONNECTED=0,	// not ready to process requests
		CONNECTED=1,	// convinced we are talking to the network
		TRACKING=2,		// convinced we agree with the network
		FULL=3			// we have the ledger and can even validate
	};

public:
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
