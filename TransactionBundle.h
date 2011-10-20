#ifndef __TRANSACTIONBUNDLE__
#define __TRANSACTIONBUNDLE__

#include <list>
#include "newcoin.pb.h"
#include "types.h"
#include "Transaction.h"


class TransactionBundle
{
	// we have to order this before we hash so the hash will be consistent 
	std::list<TransactionPtr> mTransactions;
	std::list<TransactionPtr> mDisacrdedTransactions;
	//std::list<newcoin::Transaction> mAllTransactions;
public:
	TransactionBundle();

	void clear(){ mTransactions.clear(); }

	unsigned int size(){ return(mTransactions.size()); }

	void addTransactionsToPB(newcoin::FullLedger* ledger);

	bool hasTransaction(TransactionPtr trans);

	// returns the amount of money this address holds at the end time
	// it will discard any transactions till endTime that bring amount held under 0
	uint64 checkValid(std::string address, uint64 startAmount,
		int startTime,int endTime);

	void updateMap(std::map<std::string,uint64>& moneyMap);

	// will check if all transactions after this are valid
	//void checkTransactions();
	void addTransaction(TransactionPtr trans);

	// transaction is valid and signed except the guy didn't have the money
	void addDiscardedTransaction(TransactionPtr trans);

	
	//static uint64 getTotalTransAmount(TransactionPtr trans);
};

#endif