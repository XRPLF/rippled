
#include <string>

#include "boost/bind.hpp"

#include "LedgerHistory.h"
#include "Config.h"
#include "Application.h"

void LedgerHistory::addLedger(Ledger::pointer ledger)
{
	uint256 h(ledger->getHash());
	boost::recursive_mutex::scoped_lock sl(mLock);
	mLedgersByHash.insert(std::make_pair(h, ledger));
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
	std::map<uint32, Ledger::pointer>::iterator it(mLedgersByIndex.find(index));
	if(it!=mLedgersByIndex.end()) return it->second;
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
	std::map<uint256, Ledger::pointer>::iterator it(mLedgersByHash.find(hash));
	if(it!=mLedgersByHash.end()) return it->second;
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
	if(!save)
	{ // return input ledger if not in map, otherwise, return corresponding map ledger
		std::map<uint256, Ledger::pointer>::iterator it(mLedgersByHash.find(h));
		if(it!=mLedgersByHash.end()) return it->second;
	}
	else
	{ // save input ledger in map if not in map, otherwise return corresponding map ledger
		std::pair<std::map<uint256,
			Ledger::pointer>::iterator, bool> sp(mLedgersByHash.insert(std::make_pair(h, ledger)));
		if(!sp.second) // ledger was not inserted
			return sp.first->second;
		if(ledger->isAccepted())
			mLedgersByIndex.insert(std::make_pair(ledger->getLedgerSeq(), ledger));
	}
	return ledger;
}
