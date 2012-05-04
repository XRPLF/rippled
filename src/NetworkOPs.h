#ifndef __NETWORK_OPS__
#define __NETWORK_OPS__

#include <boost/asio.hpp>

#include "Transaction.h"
#include "AccountState.h"

// Operations that clients may wish to perform against the network
// Master operational handler, server sequencer, network tracker

class Peer;

class NetworkOPs
{
public:
	enum Fault
	{ // exceptions these functions can throw
		IO_ERROR = 1,
		NO_NETWORK = 2,
	};

	enum OperatingMode
	{ // how we process transactions or account balance requests
		omDISCONNECTED = 0,	// not ready to process requests
		omCONNECTED = 1,		// convinced we are talking to the network
		omTRACKING = 2,		// convinced we agree with the network
		omFULL = 3			// we have the ledger and can even validate
	};

protected:
	OperatingMode				mMode;
	boost::asio::deadline_timer mNetTimer;

public:
	NetworkOPs(boost::asio::io_service& io_service);

	// network information
	uint64 getNetworkTime();
	uint32 getCurrentLedgerID();
	OperatingMode getOperatingMode() { return mMode; }

	// transaction operations
	Transaction::pointer processTransaction(Transaction::pointer transaction, Peer* source = NULL);
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

	// tree synchronization operations
	bool getTransactionTreeNodes(uint32 ledgerSeq, const uint256& myNodeID,
		const std::vector<unsigned char>& myNode, std::list<std::vector<unsigned char> >& newNodes);
	bool getAccountStateNodes(uint32 ledgerSeq, const uint256& myNodeId,
		const std::vector<unsigned char>& myNode, std::list<std::vector<unsigned char> >& newNodes);

	// network state machine
	void checkState();

protected:

	void setStateTimer(int seconds);

};

#endif
