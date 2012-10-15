
#include <string>

#include <boost/bind.hpp>
#include <boost/thread.hpp>

#include "LedgerHistory.h"
#include "Config.h"
#include "Application.h"

#ifndef CACHED_LEDGER_NUM
#define CACHED_LEDGER_NUM 512
#endif

#ifndef CACHED_LEDGER_AGE
#define CACHED_LEDGER_AGE 600
#endif

LedgerHistory::LedgerHistory() : mLedgersByHash(CACHED_LEDGER_NUM, CACHED_LEDGER_AGE)
{
;
}

void LedgerHistory::addLedger(Ledger::pointer ledger)
{
	mLedgersByHash.canonicalize(ledger->getHash(), ledger, true);
}

void LedgerHistory::addAcceptedLedger(Ledger::pointer ledger)
{
	assert(ledger && ledger->isAccepted());
	uint256 h(ledger->getHash());
	boost::recursive_mutex::scoped_lock sl(mLedgersByHash.peekMutex());
	mLedgersByHash.canonicalize(h, ledger, true);
	assert(ledger);
	assert(ledger->isAccepted());
	assert(ledger->isImmutable());
	mLedgersByIndex.insert(std::make_pair(ledger->getLedgerSeq(), ledger));

	boost::thread thread(boost::bind(&Ledger::saveAcceptedLedger, ledger));
	thread.detach();
}

Ledger::pointer LedgerHistory::getLedgerBySeq(uint32 index)
{
	boost::recursive_mutex::scoped_lock sl(mLedgersByHash.peekMutex());
	std::map<uint32, Ledger::pointer>::iterator it(mLedgersByIndex.find(index));
	if (it != mLedgersByIndex.end()) return it->second;
	sl.unlock();

	Ledger::pointer ret(Ledger::loadByIndex(index));
	if (!ret) return ret;
	assert(ret->getLedgerSeq() == index);

	sl.lock();
	mLedgersByHash.canonicalize(ret->getHash(), ret);
	mLedgersByIndex.insert(std::make_pair(index, ret));
	return ret;
}

Ledger::pointer LedgerHistory::getLedgerByHash(const uint256& hash)
{
	Ledger::pointer ret = mLedgersByHash.fetch(hash);
	if (ret) return ret;

#if 0
//		FIXME: A ledger without SHA maps isn't very useful
// 		This code will need to build them

	// The fix is to audit all callers to this function and replace them with
	// higher-level functions that ask more-specific questions that we can
	// test against our database

	ret = Ledger::loadByHash(hash);
	if (!ret) return ret;
	assert(ret->getHash() == hash);

	boost::recursive_mutex::scoped_lock sl(mLedgersByHash.peekMutex());
	mLedgersByHash.canonicalize(hash, ret);
	if (ret->isAccepted()) mLedgersByIndex[ret->getLedgerSeq()] = ret;
#endif

	return ret;
}

Ledger::pointer LedgerHistory::canonicalizeLedger(Ledger::pointer ledger, bool save)
{
	assert(ledger->isImmutable());
	uint256 h(ledger->getHash());

	if (!save)
	{ // return input ledger if not in map, otherwise, return corresponding map ledger
		Ledger::pointer ret = mLedgersByHash.fetch(h);
		if (ret)
			return ret;
		return ledger;
	}

	// save input ledger in map if not in map, otherwise return corresponding map ledger
	boost::recursive_mutex::scoped_lock sl(mLedgersByHash.peekMutex());
	mLedgersByHash.canonicalize(h, ledger);
	if (ledger->isAccepted()) mLedgersByIndex[ledger->getLedgerSeq()] = ledger;
	return ledger;
}
// vim:ts=4
