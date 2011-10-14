#ifndef __LEDGERHISTORY__
#define __LEDGERHISTORY__

#include "Ledger.h"

/*
This is all the history that you know of. 
*/
class LedgerHistory
{
	std::map<uint64, Ledger::pointer> mLedgers;
	bool loadLedger(uint64 index);
public:
	void load();
	
	void addLedger(Ledger::pointer ledger);

	Ledger::pointer getLedger(uint64 index);
};

#endif