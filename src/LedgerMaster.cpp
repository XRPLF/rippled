
#include "LedgerMaster.h"

#include <boost/foreach.hpp>

#include "Application.h"
#include "RippleAddress.h"
#include "Log.h"

SETUP_LOG();

uint32 LedgerMaster::getCurrentLedgerIndex()
{
	return mCurrentLedger->getLedgerSeq();
}

void LedgerMaster::addHeldTransaction(const Transaction::pointer& transaction)
{ // returns true if transaction was added
	boost::recursive_mutex::scoped_lock ml(mLock);
	mHeldTransactions.push_back(transaction->getSTransaction());
}

void LedgerMaster::pushLedger(Ledger::ref newLedger)
{
	// Caller should already have properly assembled this ledger into "ready-to-close" form --
	// all candidate transactions must already be applied
	cLog(lsINFO) << "PushLedger: " << newLedger->getHash();
	boost::recursive_mutex::scoped_lock ml(mLock);
	if (!!mFinalizedLedger)
	{
		mFinalizedLedger->setClosed();
		cLog(lsTRACE) << "Finalizes: " << mFinalizedLedger->getHash();
	}
	mFinalizedLedger = mCurrentLedger;
	mCurrentLedger = newLedger;
	mEngine.setLedger(newLedger);
}

void LedgerMaster::pushLedger(Ledger::ref newLCL, Ledger::ref newOL, bool fromConsensus)
{
	assert(newLCL->isClosed() && newLCL->isAccepted());
	assert(!newOL->isClosed() && !newOL->isAccepted());

	if (newLCL->isAccepted())
	{
		assert(newLCL->isClosed());
		assert(newLCL->isImmutable());
		mLedgerHistory.addAcceptedLedger(newLCL, fromConsensus);
		cLog(lsINFO) << "StashAccepted: " << newLCL->getHash();
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


Ledger::pointer LedgerMaster::closeLedger(bool recover)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	Ledger::pointer closingLedger = mCurrentLedger;

	if (recover)
	{
		int recovers = 0;
		for (CanonicalTXSet::iterator it = mHeldTransactions.begin(), end = mHeldTransactions.end(); it != end; ++it)
		{
			try
			{
				TER result = mEngine.applyTransaction(*it->second, tapOPEN_LEDGER);
				if (isTepSuccess(result))
					++recovers;
			}
			catch (...)
			{
				cLog(lsWARNING) << "Held transaction throws";
			}
		}
		tLog(recovers != 0, lsINFO) << "Recovered " << recovers << " held transactions";
		mHeldTransactions.reset(closingLedger->getHash());
	}

	mCurrentLedger = boost::make_shared<Ledger>(boost::ref(*closingLedger), true);
	mEngine.setLedger(mCurrentLedger);

	return closingLedger;
}

TER LedgerMaster::doTransaction(const SerializedTransaction& txn, TransactionEngineParams params)
{
	TER result = mEngine.applyTransaction(txn, params);
	theApp->getOPs().pubProposedTransaction(mEngine.getLedger(), txn, result);
	return result;
}

void LedgerMaster::acquireMissingLedger(const uint256& ledgerHash, uint32 ledgerSeq)
{
	mMissingLedger = theApp->getMasterLedgerAcquire().findCreate(ledgerHash);
	if (mMissingLedger->isComplete())
	{
		Ledger::pointer lgr = mMissingLedger->getLedger();
		if (lgr && (lgr->getLedgerSeq() == ledgerSeq))
			missingAcquireComplete(mMissingLedger);
		mMissingLedger.reset();
		return;
	}
	mMissingSeq = ledgerSeq;
	if (mMissingLedger->setAccept())
		mMissingLedger->addOnComplete(boost::bind(&LedgerMaster::missingAcquireComplete, this, _1));
}

void LedgerMaster::missingAcquireComplete(LedgerAcquire::pointer acq)
{
	boost::recursive_mutex::scoped_lock ml(mLock);

	if (acq->isFailed() && (mMissingSeq != 0))
	{
		cLog(lsWARNING) << "Acquire failed for " << mMissingSeq;
	}

	mMissingLedger.reset();
	mMissingSeq = 0;

	if (!acq->isFailed())
	{
		setFullLedger(acq->getLedger());
		acq->getLedger()->pendSave(false);
	}
}

void LedgerMaster::setFullLedger(Ledger::ref ledger)
{
	boost::recursive_mutex::scoped_lock ml(mLock);

	mCompleteLedgers.setValue(ledger->getLedgerSeq());

	if ((ledger->getLedgerSeq() != 0) && mCompleteLedgers.hasValue(ledger->getLedgerSeq() - 1))
	{ // we think we have the previous ledger, double check
		Ledger::pointer prevLedger = getLedgerBySeq(ledger->getLedgerSeq() - 1);
		if (prevLedger && (prevLedger->getHash() != ledger->getParentHash()))
		{
			cLog(lsWARNING) << "Ledger " << ledger->getLedgerSeq() << " invalidates prior ledger";
			mCompleteLedgers.clearValue(prevLedger->getLedgerSeq());
		}
	}

	if (mMissingLedger && mMissingLedger->isComplete())
		mMissingLedger.reset();

	if (mMissingLedger || !theConfig.FULL_HISTORY)
		return;

	if (Ledger::getPendingSaves() > 3)
	{
		cLog(lsINFO) << "Too many pending ledger saves";
		return;
	}

	// see if there's a ledger gap we need to fill
	if (!mCompleteLedgers.hasValue(ledger->getLedgerSeq() - 1))
	{
		cLog(lsINFO) << "We need the ledger before the ledger we just accepted";
		acquireMissingLedger(ledger->getParentHash(), ledger->getLedgerSeq() - 1);
	}
	else
	{
		uint32 prevMissing = mCompleteLedgers.prevMissing(ledger->getLedgerSeq());
		if (prevMissing != RangeSet::RangeSetAbsent)
		{
			cLog(lsINFO) << "Ledger " << prevMissing << " is missing";
			assert(!mCompleteLedgers.hasValue(prevMissing));
			Ledger::pointer nextLedger = getLedgerBySeq(prevMissing + 1);
			if (nextLedger)
				acquireMissingLedger(nextLedger->getParentHash(), nextLedger->getLedgerSeq() - 1);
			else
			{
				mCompleteLedgers.clearValue(prevMissing);
				cLog(lsWARNING) << "We have a gap we can't fix: " << prevMissing + 1;
			}
		}
	}
}

// vim:ts=4
