
#include <string>

#include "boost/bind.hpp"

#include "LedgerHistory.h"
#include "Config.h"
#include "Application.h"

void LedgerHistory::addLedger(Ledger::pointer ledger)
{
	uint256 h(ledger->getHash());
	boost::recursive_mutex::scoped_lock sl(mLock);
	if(!mLedgersByHash.count(h)) mLedgersByHash.insert(std::make_pair(h, ledger));
}

void LedgerHistory::addAcceptedLedger(Ledger::pointer ledger)
{
	assert(ledger && ledger->isAccepted());
	uint256 h(ledger->getHash());
	boost::recursive_mutex::scoped_lock sl(mLock);
	mLedgersByIndex.insert(std::make_pair(ledger->getLedgerSeq(), ledger));
	mLedgersByHash.insert(std::make_pair(h, ledger));
	theApp->getIOService().post(boost::bind(&Ledger::saveAcceptedLedger, ledger));
}

Ledger::pointer LedgerHistory::getLedgerBySeq(uint32 index)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	if(mLedgersByIndex.count(index)) return mLedgersByIndex[index];
	sl.unlock();
	Ledger::pointer ret(Ledger::loadByIndex(index));
	if(!ret) return ret;

	assert(ret->getLedgerSeq()==index);
	uint256 h=ret->getHash();
	sl.lock();
	mLedgersByIndex.insert(std::make_pair(index, ret));
	mLedgersByHash.insert(std::make_pair(h, ret));
	return ret;
}

Ledger::pointer LedgerHistory::getLedgerByHash(const uint256& hash)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	if(mLedgersByHash.count(hash)!=0) return mLedgersByHash[hash];

	sl.unlock();
	Ledger::pointer ret=Ledger::loadByHash(hash);
	if(!ret) return ret;

	assert(ret->getHash()==hash);
	sl.lock();
	mLedgersByHash.insert(std::make_pair(hash, ret));
	if(ret->isAccepted()) mLedgersByIndex.insert(std::make_pair(ret->getLedgerSeq(), ret));
	return ret;
}

Ledger::pointer LedgerHistory::canonicalizeLedger(Ledger::pointer ledger, bool save)
{
	uint256 h(ledger->getHash());

	boost::recursive_mutex::scoped_lock sl(mLock);
	if(mLedgersByHash.count(h)!=0) return mLedgersByHash[h];
	if(!save) return ledger;

	assert(ledger->getHash()==h);
	mLedgersByHash.insert(std::make_pair(h, ledger));
	if(ledger->isAccepted()) mLedgersByIndex.insert(std::make_pair(ledger->getLedgerSeq(), ledger));
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
