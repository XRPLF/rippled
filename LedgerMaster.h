#ifndef __LEDGERMASTER__
#define __LEDGERMASTER__

#include "Ledger.h"
#include "LedgerHistory.h"
#include "Peer.h"
#include "types.h"
#include "Transaction.h"

/*
Handles:
	collecting the current ledger
	finalizing the ledger
	validating the past ledger
	keeping the ledger history

	There is one ledger we are working on. 

*/
class LedgerMaster
{
	Ledger::pointer mCurrentLedger;
	Ledger::pointer mFinalizingLedger;

	LedgerHistory mLedgerHistory;
	std::list<TransactionPtr> mFutureTransactions;
	std::list< std::pair<Peer::pointer,newcoin::ProposeLedger> > mFutureProposals;

	//bool mAfterProposed;


	void addFutureProposal(Peer::pointer peer,newcoin::ProposeLedger& packet);
	void applyFutureProposals(uint32 ledgerIndex);
	void applyFutureTransactions(uint32 ledgerIndex);
	bool isValidTransaction(TransactionPtr trans);
	bool isTransactionOnFutureList(TransactionPtr trans);
public:
	LedgerMaster();

	void load();
	void save();

	uint32 getCurrentLedgerIndex();

	Ledger::pointer getAcceptedLedger(uint32 index){ return(mLedgerHistory.getAcceptedLedger(index)); }
	Ledger::pointer getLedger(const uint256& hash){ return(mLedgerHistory.getLedger(hash)); }

	int64 getAmountHeld(std::string& addr);
	int64 getAmountHeld(uint160& addr);
	Ledger::Account* getAccount(uint160& addr){ return(mCurrentLedger->getAccount(addr)); }

	bool addTransaction(TransactionPtr trans);
	void addFullLedger(newcoin::FullLedger& ledger);

	void startFinalization();
	void sendProposal();
	void endFinalization();
	void checkLedgerProposal(Peer::pointer peer,newcoin::ProposeLedger& packet);
	void checkConsensus(uint32 ledgerIndex);
};

#endif