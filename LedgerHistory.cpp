
#include <string>

#include "boost/bind.hpp"

#include "LedgerHistory.h"
#include "Config.h"
#include "Application.h"

void LedgerHistory::addLedger(Ledger::pointer ledger)
{
	uint256 h(ledger->getHash());
	boost::recursive_mutex::scoped_lock sl(mLock);
	if(!mLedgersByHash[h]) mLedgersByHash[h]=ledger;
}

void LedgerHistory::addAcceptedLedger(Ledger::pointer ledger)
{
	assert(ledger && ledger->isAccepted());
	uint256 h(ledger->getHash());
	boost::recursive_mutex::scoped_lock sl(mLock);
	if(!!mLedgersByHash[h]) return;
	mLedgersByHash[h]=ledger;
	mLedgersByIndex[ledger->getLedgerSeq()]=ledger;

	theApp->getIOService().post(boost::bind(&Ledger::saveAcceptedLedger, ledger));
}

Ledger::pointer LedgerHistory::getLedgerBySeq(uint32 index)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	Ledger::pointer ret(mLedgersByIndex[index]);
	if(ret) return ret;
	sl.unlock();
	ret=Ledger::loadByIndex(index);
	if(!ret) return ret;

	assert(ret->getLedgerSeq()==index);
	uint256 h=ret->getHash();
	sl.lock();
	mLedgersByIndex[index]=ret;
	mLedgersByHash[h]=ret;
	return ret;
}

Ledger::pointer LedgerHistory::getLedgerByHash(const uint256& hash)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	Ledger::pointer ret(mLedgersByHash[hash]);
	if(ret) return ret;

	sl.unlock();
	ret=Ledger::loadByHash(hash);
	if(!ret) return ret;

	assert(ret->getHash()==hash);
	sl.lock();
	mLedgersByHash[hash]=ret;
	if(ret->isAccepted()) mLedgersByIndex[ret->getLedgerSeq()]=ret;
	return ret;
}

Ledger::pointer LedgerHistory::canonicalizeLedger(Ledger::pointer ledger, bool save)
{
	uint256 h(ledger->getHash());

	boost::recursive_mutex::scoped_lock sl(mLock);
	Ledger::pointer ret(mLedgersByHash[h]);
	if(ret) return ret;
	if(!save) return ledger;
	assert(ret->getHash()==h);
	mLedgersByHash[h]=ledger;
	if(ret->isAccepted()) mLedgersByIndex[ret->getLedgerSeq()]=ledger;
	return ledger;
}

#if 0
bool LedgerHistory::loadLedger(const uint256& hash)
{
	Ledger::pointer ledger=Ledger::pointer(new Ledger());
	if(ledger->load(hash))
	{
		mAllLedgers[hash]=ledger;
		return(true);
	}
	return(false);
}

bool LedgerHistory::loadAcceptedLedger(uint32 index)
{
	// TODO: LedgerHistory::loadAcceptedLedger(uint32 index)
	/*
	Ledger::pointer ledger=theApp->getSerializer()->loadAcceptedLedger(index);
	if(ledger)
	{
		mAcceptedLedgers[index]=ledger;
		return(true);
	}*/
	return(false);
}

void LedgerHistory::addAcceptedLedger(Ledger::pointer ledger)
{
	mAcceptedLedgers[ledger->getIndex()]=ledger;
}

// this will see if the ledger is in memory
// if not it will check disk and load it
// if not it will return NULL
Ledger::pointer LedgerHistory::getAcceptedLedger(uint32 index)
{
	if(mAcceptedLedgers.count(index))
		return(mAcceptedLedgers[index]);
	if(loadAcceptedLedger(index)) return(mAcceptedLedgers[index]);
	return(Ledger::pointer());
}

void LedgerHistory::addLedger(Ledger::pointer ledger)
{
	mAcceptedLedgers[ledger->getIndex()]=ledger;
	ledger->save();
}

Ledger::pointer LedgerHistory::getLedger(const uint256& hash)
{
	if(mAllLedgers.count(hash))
		return(mAllLedgers[hash]);
	if(loadLedger(hash)) return(mAllLedgers[hash]);
	return(Ledger::pointer());
}
#endif
