
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
	Log(lsINFO) << "PushAccept: " << newLCL->getHash().GetHex();

	ScopedLock sl(mLock);
	if (mFinalizedLedger && mFinalizedLedger->isAccepted())
	{
		mLedgerHistory.addAcceptedLedger(mFinalizedLedger);
		Log(lsINFO) << "StashAccepted: " << mFinalizedLedger->getHash().GetHex();
	}
	mFinalizedLedger = newLCL;
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

Ledger::pointer LedgerMaster::closeTime()
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	assert(mCurrentLedger && mWobbleLedger);
	Ledger::pointer ret = mCurrentLedger;
	mCurrentLedger = mWobbleLedger;
	mEngine.setLedger(mCurrentLedger);
	mWobbleLedger = ret;
	return ret;
}

void LedgerMaster::beginWobble()
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	assert(!mWobbleLedger);
	mWobbleLedger = mCurrentLedger;
	mCurrentLedger = boost::make_shared<Ledger>(mCurrentLedger);
	mEngine.setLedger(mCurrentLedger);
}

Ledger::pointer LedgerMaster::endWobble()
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	assert(mWobbleLedger && mCurrentLedger);
	Ledger::pointer ret = mWobbleLedger;
	mWobbleLedger = Ledger::pointer();
	return ret;
}

// vim:ts=4
