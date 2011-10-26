#ifndef __LEDGERHISTORY__
#define __LEDGERHISTORY__

#include "Ledger.h"

/*
This is a collection of all the ledgers you know about.
One thread of them is the thread you validate. 
But you keep others that you have heard about.

Why do you need to keep them all? 
	In case people ask.
	To make sure that there isn't a conflict between the validated ledgers
*/

class LedgerHistory
{
	std::map<uint32, Ledger::pointer> mAcceptedLedgers;
	std::map<uint256, Ledger::pointer> mAllLedgers;

	bool loadAcceptedLedger(uint32 index);
	bool loadLedger(uint256& hash);
public:
	void load();
	
	void addLedger(Ledger::pointer ledger);
	void addAcceptedLedger(Ledger::pointer ledger);

	Ledger::pointer getAcceptedLedger(uint32 index);
	Ledger::pointer getLedger(uint256& hash);
};

#endif