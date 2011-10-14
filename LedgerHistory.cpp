#include "LedgerHistory.h"
#include "Config.h"

/*
Soon we should support saving the ledger in a real DB
For now save them all in 
*/

void LedgerHistory::load()
{

}

bool LedgerHistory::loadLedger(uint64 index)
{
	Ledger::pointer ledger(new Ledger(index));
	if(ledger->load(theConfig.HISTORY_DIR))
	{
		mLedgers[index]=ledger;
	}
	return(false);
}

// this will see if the ledger is in memory
// if not it will check disk and load it
// if not it will return NULL
Ledger::pointer LedgerHistory::getLedger(uint64 index)
{
	if(mLedgers.count(index))
		return(mLedgers[index]);
	if(loadLedger(index)) return(mLedgers[index]);
	return(Ledger::pointer());
}

void LedgerHistory::addLedger(Ledger::pointer ledger)
{
	mLedgers[ledger->getIndex()]=ledger;
	ledger->save(theConfig.HISTORY_DIR);
}