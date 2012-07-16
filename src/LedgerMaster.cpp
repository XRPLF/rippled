
#include "LedgerMaster.h"

#include <boost/foreach.hpp>

#include "Application.h"
#include "NewcoinAddress.h"
#include "Conversion.h"
#include "Log.h"

uint32 LedgerMaster::getCurrentLedgerIndex()
{
	return mCurrentLedger->getLedgerSeq();
}

bool LedgerMaster::addHeldTransaction(Transaction::pointer transaction)
{ // returns true if transaction was added
	boost::recursive_mutex::scoped_lock ml(mLock);
	return mHeldTransactionsByID.insert(std::make_pair(transaction->getID(), transaction)).second;
}

void LedgerMaster::pushLedger(Ledger::pointer newLedger)
{
	// Caller should already have properly assembled this ledger into "ready-to-close" form --
	// all candidate transactions must already be appled
	Log(lsINFO) << "PushLedger: " << newLedger->getHash().GetHex();
	ScopedLock sl(mLock);
	if (!!mFinalizedLedger)
	{
		mFinalizedLedger->setClosed();
		Log(lsTRACE) << "Finalizes: " << mFinalizedLedger->getHash().GetHex();
	}
	mFinalizedLedger = mCurrentLedger;
	mCurrentLedger = newLedger;
	mEngine.setLedger(newLedger);
}

void LedgerMaster::pushLedger(Ledger::pointer newLCL, Ledger::pointer newOL)
{
	assert(newLCL->isClosed() && newLCL->isAccepted());
	assert(!newOL->isClosed() && !newOL->isAccepted());

	if (newLCL->isAccepted())
	{
		mLedgerHistory.addAcceptedLedger(newLCL);
		Log(lsINFO) << "StashAccepted: " << newLCL->getHash().GetHex();
	}

	mFinalizedLedger = newLCL;
	ScopedLock sl(mLock);
	mCurrentLedger = newOL;
	mEngine.setLedger(newOL);
}

void LedgerMaster::switchLedgers(Ledger::pointer lastClosed, Ledger::pointer current)
{
	mFinalizedLedger = lastClosed;
	mFinalizedLedger->setClosed();
	mFinalizedLedger->setAccepted();

	mCurrentLedger = current;
	assert(!mCurrentLedger->isClosed());
	mEngine.setLedger(mCurrentLedger);
}

void LedgerMaster::storeLedger(Ledger::pointer ledger)
{
	mLedgerHistory.addLedger(ledger);
}

void LedgerMaster::beginWobble()
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	assert(!mWobbleLedger);
	mWobbleLedger = boost::make_shared<Ledger>(boost::ref(*mCurrentLedger), true);
	mEngine.setDefaultLedger(mCurrentLedger);
	mEngine.setAlternateLedger(mWobbleLedger);
}

void LedgerMaster::closeTime()
{ // swap current and wobble ledgers
	boost::recursive_mutex::scoped_lock sl(mLock);
	assert(mCurrentLedger && mWobbleLedger);
	std::swap(mCurrentLedger, mWobbleLedger);
	mEngine.setDefaultLedger(mCurrentLedger);
	mEngine.setAlternateLedger(mWobbleLedger);
}

Ledger::pointer LedgerMaster::endWobble()
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	assert(mWobbleLedger && mCurrentLedger);
	Ledger::pointer ret = mWobbleLedger;
	mWobbleLedger = Ledger::pointer();
	mEngine.setAlternateLedger(Ledger::pointer());
	return ret;
}

TransactionEngineResult LedgerMaster::doTransaction(const SerializedTransaction& txn, uint32 targetLedger,
	TransactionEngineParams params)
{
	Ledger::pointer ledger = mEngine.getTransactionLedger(targetLedger);
	TransactionEngineResult result = mEngine.applyTransaction(txn, params, ledger);
	theApp->getOPs().pubTransaction(ledger, txn, result);
	return result;
}


// vim:ts=4
