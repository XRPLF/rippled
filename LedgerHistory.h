#ifndef __LEDGERHISTORY__
#define __LEDGERHISTORY__

#include "Ledger.h"

class LedgerHistory
{
	std::map<uint32, Ledger::pointer> mLedgersByIndex;
	std::map<uint256, Ledger::pointer> mLedgersByHash;

	bool loadClosedLedger(uint32 index);
	bool loadLedger(const uint256& hash);

public:
	LedgerHistory() { ; }
	
	void addLedger(Ledger::pointer ledger);
	void addAcceptedLedger(Ledger::pointer ledger);

	Ledger::pointer getLedgerBySeq(uint32 index);
	Ledger::pointer getLedgerByHash(const uint256& hash);
};

#endif