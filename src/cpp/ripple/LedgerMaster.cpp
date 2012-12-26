
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
				bool didApply;
				mEngine.applyTransaction(*it->second, tapOPEN_LEDGER, didApply);
				if (didApply)
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
	bool didApply;
	TER result = mEngine.applyTransaction(txn, params, didApply);
	// CHECKME: Should we call this even on gross failures?
	theApp->getOPs().pubProposedTransaction(mEngine.getLedger(), txn, result);
	return result;
}

bool LedgerMaster::haveLedgerRange(uint32 from, uint32 to)
{
	uint32 prevMissing = mCompleteLedgers.prevMissing(to + 1);
	return (prevMissing == RangeSet::RangeSetAbsent) || (prevMissing < from);
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

static bool shouldAcquire(uint32 currentLedger, uint32 ledgerHistory, uint32 candidateLedger)
{
	bool ret;
	if (candidateLedger >= currentLedger)
		ret = true;
	else ret = (currentLedger - candidateLedger) <= ledgerHistory;
	cLog(lsDEBUG) << "Missing ledger " << candidateLedger << (ret ? " will" : " will NOT") << " be acquired";
	return ret;
}

void LedgerMaster::resumeAcquiring()
{
	boost::recursive_mutex::scoped_lock ml(mLock);
	if (!mTooFast)
		return;
	mTooFast = false;

	if (mMissingLedger && (mMissingLedger->isComplete() || mMissingLedger->isFailed()))
		mMissingLedger.reset();

	if (mMissingLedger || !theConfig.LEDGER_HISTORY)
	{
		tLog(mMissingLedger, lsDEBUG) << "Fetch already in progress, not resuming";
		return;
	}

	uint32 prevMissing = mCompleteLedgers.prevMissing(mFinalizedLedger->getLedgerSeq());
	if (prevMissing == RangeSet::RangeSetAbsent)
	{
		cLog(lsDEBUG) << "no prior missing ledger, not resuming";
		return;
	}
	if (shouldAcquire(mCurrentLedger->getLedgerSeq(), theConfig.LEDGER_HISTORY, prevMissing))
	{
		cLog(lsINFO) << "Resuming at " << prevMissing;
		assert(!mCompleteLedgers.hasValue(prevMissing));
		Ledger::pointer nextLedger = getLedgerBySeq(prevMissing + 1);
		if (nextLedger)
			acquireMissingLedger(nextLedger->getParentHash(), nextLedger->getLedgerSeq() - 1);
		else
		{
			mCompleteLedgers.clearValue(prevMissing);
			cLog(lsWARNING) << "We have a gap at: " << prevMissing + 1;
		}
	}
}

void LedgerMaster::setFullLedger(Ledger::ref ledger)
{ // A new ledger has been accepted as part of the trusted chain
	cLog(lsDEBUG) << "Ledger " << ledger->getLedgerSeq() << " accepted :" << ledger->getHash();

	boost::recursive_mutex::scoped_lock ml(mLock);

	mCompleteLedgers.setValue(ledger->getLedgerSeq());

	if ((ledger->getLedgerSeq() != 0) && mCompleteLedgers.hasValue(ledger->getLedgerSeq() - 1))
	{ // we think we have the previous ledger, double check
		Ledger::pointer prevLedger = getLedgerBySeq(ledger->getLedgerSeq() - 1);
		if (!prevLedger)
		{
			cLog(lsWARNING) << "Ledger " << ledger->getLedgerSeq() - 1 << " missing";
			mCompleteLedgers.clearValue(ledger->getLedgerSeq() - 1);
		}
		else if (prevLedger->getHash() != ledger->getParentHash())
		{
			cLog(lsWARNING) << "Ledger " << ledger->getLedgerSeq() << " invalidates prior ledger";
			mCompleteLedgers.clearValue(prevLedger->getLedgerSeq());
		}
	}

	if (mMissingLedger && (mMissingLedger->isComplete() || mMissingLedger->isFailed()))
		mMissingLedger.reset();

	if (mMissingLedger || !theConfig.LEDGER_HISTORY)
	{
		tLog(mMissingLedger, lsDEBUG) << "Fetch already in progress, " << mMissingLedger->getTimeouts() << " timeouts";
		return;
	}

	if (Ledger::getPendingSaves() > 2)
	{
		mTooFast = true;
		cLog(lsINFO) << "Too many pending ledger saves";
		return;
	}

	// see if there's a ledger gap we need to fill
	if (!mCompleteLedgers.hasValue(ledger->getLedgerSeq() - 1))
	{
		if (!shouldAcquire(mCurrentLedger->getLedgerSeq(), theConfig.LEDGER_HISTORY, ledger->getLedgerSeq() - 1))
			return;
		cLog(lsDEBUG) << "We need the ledger before the ledger we just accepted: " << ledger->getLedgerSeq() - 1;
		acquireMissingLedger(ledger->getParentHash(), ledger->getLedgerSeq() - 1);
	}
	else
	{
		uint32 prevMissing = mCompleteLedgers.prevMissing(ledger->getLedgerSeq());
		if (prevMissing == RangeSet::RangeSetAbsent)
		{
			cLog(lsDEBUG) << "no prior missing ledger";
			return;
		}
		cLog(lsTRACE) << "Ledger " << prevMissing << " is missing";
		if (shouldAcquire(mCurrentLedger->getLedgerSeq(), theConfig.LEDGER_HISTORY, prevMissing))
		{
			cLog(lsINFO) << "Ledger " << prevMissing << " is needed";
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
