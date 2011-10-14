#ifndef __LEDGERMASTER__
#define __LEDGERMASTER__

#include "Ledger.h"
#include "LedgerHistory.h"
#include "Peer.h"
#include "types.h"

/*
Handles:
	collecting the current ledger
	finalizing the ledger
	validating the past ledger
	keeping the ledger history
*/
class LedgerMaster
{
	Ledger::pointer mCurrentLedger;
	Ledger::pointer mFinalizingLedger;

	LedgerHistory mLedgerHistory;
	std::list<newcoin::Transaction> mFutureTransactions;
	std::list< std::pair<Peer::pointer,newcoin::ProposeLedger> > mFutureProposals;

	bool mAfterProposed;


	void addFutureProposal(Peer::pointer peer,newcoin::ProposeLedger& packet);
	void applyFutureProposals();
	void applyFutureTransactions();
	bool isValidTransactionSig(newcoin::Transaction& trans);
public:
	LedgerMaster();

	void load();
	void save();

	uint64 getCurrentLedgerIndex();
	int getCurrentLedgerSeconds();
	Ledger::pointer getLedger(uint64 index);
	uint64 getAmountHeld(std::string& addr);

	bool addTransaction(newcoin::Transaction& trans);
	void gotFullLedger(newcoin::FullLedger& ledger);

	void nextLedger();
	void sendProposal();
	void checkLedgerProposal(Peer::pointer peer,newcoin::ProposeLedger& packet);
};

#endif