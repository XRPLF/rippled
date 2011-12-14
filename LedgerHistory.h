#ifndef __LEDGERHISTORY__
#define __LEDGERHISTORY__

#include "Ledger.h"

class LedgerHistory
{
	boost::recursive_mutex mLock;

	std::map<uint32, Ledger::pointer> mLedgersByIndex;
	std::map<uint256, Ledger::pointer> mLedgersByHash;

public:
	LedgerHistory() { ; }
	
	void addLedger(Ledger::pointer ledger);
	void addAcceptedLedger(Ledger::pointer ledger);

	Ledger::pointer getLedgerBySeq(uint32 index);
	Ledger::pointer getLedgerByHash(const uint256& hash);
	Ledger::pointer canonicalizeLedger(Ledger::pointer, bool cache);
};

#endif