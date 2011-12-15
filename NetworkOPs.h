#ifndef __NETWORK_OPS__
#define __NETWORK_OPS__

#include "Transaction.h"
#include "AccountState.h"

// Operations that clients may wish to perform against the network

class NetworkOPs
{
	enum Fault
	{
		IO_ERROR=1,
		NO_NETWORK=2,
	};

public:

	// network information
	uint64 getNetworkTime();
	uint32 getCurrentLedgerID();

	// transaction operations
	Transaction::pointer processTransaction(Transaction::pointer transaction);
	Transaction::pointer findTransactionByID(const uint256& transactionID);
	int findTransactionsBySource(std::list<Transaction::pointer>&, const uint160& sourceAccount,
		uint32 minSeq, uint32 maxSeq);
	int findTransactionsByDestination(std::list<Transaction::pointer>&, const uint160& destinationAccount,
		uint32 startLedgerSeq, uint32 endLedgerSeq, int maxTransactions);

	// account operations
	AccountState::pointer getAccountState(const uint160& accountID);

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
