
#include "LedgerMaster.h"

#include <boost/foreach.hpp>

#include "Application.h"
#include "NewcoinAddress.h"
#include "Log.h"

uint32 LedgerMaster::getCurrentLedgerIndex()
{
	return mCurrentLedger->getLedgerSeq();
}

bool LedgerMaster::addHeldTransaction(const Transaction::pointer& transaction)
{ // returns true if transaction was added
	boost::recursive_mutex::scoped_lock ml(mLock);
	return mHeldTransactionsByID.insert(std::make_pair(transaction->getID(), transaction)).second;
}

void LedgerMaster::pushLedger(Ledger::ref newLedger)
{
	// Caller should already have properly assembled this ledger into "ready-to-close" form --
	// all candidate transactions must already be applied
	Log(lsINFO) << "PushLedger: " << newLedger->getHash();
	boost::recursive_mutex::scoped_lock ml(mLock);
	if (!!mFinalizedLedger)
	{
		mFinalizedLedger->setClosed();
		Log(lsTRACE) << "Finalizes: " << mFinalizedLedger->getHash();
	}
	mFinalizedLedger = mCurrentLedger;
	mCurrentLedger = newLedger;
	mEngine.setLedger(newLedger);
	if (mLastFullLedger && (newLedger->getParentHash() == mLastFullLedger->getHash()))
		mLastFullLedger = newLedger;
}

void LedgerMaster::pushLedger(Ledger::ref newLCL, Ledger::ref newOL)
{
	assert(newLCL->isClosed() && newLCL->isAccepted());
	assert(!newOL->isClosed() && !newOL->isAccepted());

	if (newLCL->isAccepted())
	{
		assert(newLCL->isClosed());
		assert(newLCL->isImmutable());
		mLedgerHistory.addAcceptedLedger(newLCL, false);
		if (mLastFullLedger && (newLCL->getParentHash() == mLastFullLedger->getHash()))
			mLastFullLedger = newLCL;
		Log(lsINFO) << "StashAccepted: " << newLCL->getHash();
	}

	boost::recursive_mutex::scoped_lock ml(mLock);
	mFinalizedLedger = newLCL;
	mCurrentLedger = newOL;
	mEngine.setLedger(newOL);
}

void LedgerMaster::switchLedgers(Ledger::ref lastClosed, Ledger::ref current)
{
	assert(lastClosed && current);

	{
		boost::recursive_mutex::scoped_lock ml(mLock);
		mFinalizedLedger = lastClosed;
		mFinalizedLedger->setClosed();
		mFinalizedLedger->setAccepted();
		mCurrentLedger = current;
	}

	assert(!mCurrentLedger->isClosed());
	mEngine.setLedger(mCurrentLedger);
}

void LedgerMaster::storeLedger(Ledger::ref ledger)
{
	mLedgerHistory.addLedger(ledger);
	if (ledger->isAccepted())
		mLedgerHistory.addAcceptedLedger(ledger, false);
}


Ledger::pointer LedgerMaster::closeLedger()
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	Ledger::pointer closingLedger = mCurrentLedger;
	mCurrentLedger = boost::make_shared<Ledger>(boost::ref(*closingLedger), true);
	mEngine.setLedger(mCurrentLedger);
	return closingLedger;
}

TER LedgerMaster::doTransaction(const SerializedTransaction& txn, TransactionEngineParams params)
{
	TER result = mEngine.applyTransaction(txn, params);
	theApp->getOPs().pubTransaction(mEngine.getLedger(), txn, result);
	return result;
}

void LedgerMaster::checkLedgerGap(Ledger::ref ledger)
{
	boost::recursive_mutex::scoped_lock sl(mLock);

	if (ledger->getParentHash() == mLastFullLedger->getHash())
	{
		mLastFullLedger = ledger;
		return;
	}

	if (theApp->getMasterLedgerAcquire().hasSet())
		return;

	if (ledger->getLedgerSeq() < mLastFullLedger->getLedgerSeq())
		return;

	// we have a gap or discontinuity
	theApp->getMasterLedgerAcquire().makeSet(mLastFullLedger, ledger);

}

// vim:ts=4
