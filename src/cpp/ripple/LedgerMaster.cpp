
#include "LedgerMaster.h"

#include <boost/foreach.hpp>

#include "Application.h"
#include "RippleAddress.h"
#include "Log.h"

SETUP_LOG();

#define MIN_VALIDATION_RATIO	150		// 150/256ths of validations of previous ledger
#define MAX_LEDGER_GAP			100		// Don't catch up more than 100 ledgers  (cannot exceed 256)

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

	{
		boost::recursive_mutex::scoped_lock ml(mLock);
		mFinalizedLedger = newLCL;
		mCurrentLedger = newOL;
		mEngine.setLedger(newOL);
	}
	checkAccept(newLCL->getHash(), newLCL->getLedgerSeq());
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
	checkAccept(lastClosed->getHash(), lastClosed->getLedgerSeq());
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

void LedgerMaster::asyncAccept(Ledger::pointer ledger)
{
	uint32 seq = ledger->getLedgerSeq();
	uint256 prevHash = ledger->getParentHash();

	while (seq > 0)
	{
		{
			boost::recursive_mutex::scoped_lock ml(mLock);
			mCompleteLedgers.setValue(seq);
			--seq;
			if (mCompleteLedgers.hasValue(seq))
				break;
		}

		uint256 tHash, pHash;
		if (!Ledger::getHashesByIndex(seq, tHash, pHash) || (tHash != prevHash))
			break;
		prevHash = pHash;
	}

	resumeAcquiring();
}

bool LedgerMaster::acquireMissingLedger(Ledger::ref origLedger, const uint256& ledgerHash, uint32 ledgerSeq)
{ // return: false = already gave up recently
	if (mTooFast)
		return true;

	Ledger::pointer ledger = mLedgerHistory.getLedgerBySeq(ledgerSeq);
	if (Ledger::getHashByIndex(ledgerSeq) == ledgerHash)
	{
		cLog(lsDEBUG) << "Ledger hash found in database";
		mTooFast = true;
		theApp->getJobQueue().addJob(jtPUBLEDGER, boost::bind(&LedgerMaster::asyncAccept, this, ledger));
		return true;
	}

	if (theApp->getMasterLedgerAcquire().isFailure(ledgerHash))
		return false;

	mMissingLedger = theApp->getMasterLedgerAcquire().findCreate(ledgerHash);
	if (mMissingLedger->isComplete())
	{
		Ledger::pointer lgr = mMissingLedger->getLedger();
		if (lgr && (lgr->getLedgerSeq() == ledgerSeq))
			missingAcquireComplete(mMissingLedger);
		mMissingLedger.reset();
		return true;
	}
	else if (mMissingLedger->isDone())
	{
		mMissingLedger.reset();
		return false;
	}
	mMissingSeq = ledgerSeq;
	if (mMissingLedger->setAccept())
	{
		if (!mMissingLedger->addOnComplete(boost::bind(&LedgerMaster::missingAcquireComplete, this, _1)))
			theApp->getIOService().post(boost::bind(&LedgerMaster::missingAcquireComplete, this, mMissingLedger));
	}

	if (theApp->getMasterLedgerAcquire().getFetchCount() < 3)
	{
		int count = 0;
		typedef std::pair<uint32, uint256> u_pair;

		std::vector<u_pair> vec = origLedger->getLedgerHashes();
		BOOST_REVERSE_FOREACH(const u_pair& it, vec)
		{
			if ((count < 2) && (it.first < ledgerSeq) &&
				!mCompleteLedgers.hasValue(it.first) && !theApp->getMasterLedgerAcquire().find(it.second))
			{
				++count;
				theApp->getMasterLedgerAcquire().findCreate(it.second);
			}
		}
	}

	return true;
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

	if (acq->isComplete())
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

	if (mMissingLedger && mMissingLedger->isDone())
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
		cLog(lsTRACE) << "Resuming at " << prevMissing;
		assert(!mCompleteLedgers.hasValue(prevMissing));
		Ledger::pointer nextLedger = getLedgerBySeq(prevMissing + 1);
		if (nextLedger)
			acquireMissingLedger(nextLedger, nextLedger->getParentHash(), nextLedger->getLedgerSeq() - 1);
		else
		{
			mCompleteLedgers.clearValue(prevMissing);
			cLog(lsWARNING) << "We have a gap at: " << prevMissing + 1;
		}
	}
}

void LedgerMaster::fixMismatch(Ledger::ref ledger)
{
	int invalidate = 0;

	mMissingLedger.reset();
	mMissingSeq = 0;

	for (uint32 lSeq = ledger->getLedgerSeq() - 1; lSeq > 0; --lSeq)
		if (mCompleteLedgers.hasValue(lSeq))
		{
			uint256 hash = ledger->getLedgerHash(lSeq);
			if (hash.isNonZero())
			{ // try to close the seam
				Ledger::pointer otherLedger = getLedgerBySeq(lSeq);
				if (otherLedger && (otherLedger->getHash() == hash))
				{ // we closed the seam
					tLog(invalidate != 0, lsWARNING) << "Match at " << lSeq << ", " <<
						invalidate << " prior ledgers invalidated";
					return;
				}
			}
			mCompleteLedgers.clearValue(lSeq);
			++invalidate;
		}

	// all prior ledgers invalidated
	tLog(invalidate != 0, lsWARNING) << "All " << invalidate << " prior ledgers invalidated";
}

void LedgerMaster::setFullLedger(Ledger::ref ledger)
{ // A new ledger has been accepted as part of the trusted chain
	cLog(lsDEBUG) << "Ledger " << ledger->getLedgerSeq() << " accepted :" << ledger->getHash();

	boost::recursive_mutex::scoped_lock ml(mLock);

	mCompleteLedgers.setValue(ledger->getLedgerSeq());

	if ((ledger->getLedgerSeq() != 0) && mCompleteLedgers.hasValue(ledger->getLedgerSeq() - 1))
	{ // we think we have the previous ledger, double check
		Ledger::pointer prevLedger = getLedgerBySeq(ledger->getLedgerSeq() - 1);
		if (!prevLedger || (prevLedger->getHash() != ledger->getParentHash()))
		{
			cLog(lsWARNING) << "Acquired ledger invalidates previous ledger: " <<
				(prevLedger ? "hashMismatch" : "missingLedger");
			fixMismatch(ledger);
		}
	}

	if (mMissingLedger && mMissingLedger->isDone())
	{
		if (mMissingLedger->isFailed())
			theApp->getMasterLedgerAcquire().dropLedger(mMissingLedger->getHash());
		mMissingLedger.reset();
	}

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
		acquireMissingLedger(ledger, ledger->getParentHash(), ledger->getLedgerSeq() - 1);
	}
	else
	{
		uint32 prevMissing = mCompleteLedgers.prevMissing(ledger->getLedgerSeq());
		if (prevMissing == RangeSet::RangeSetAbsent)
		{
			cLog(lsDEBUG) << "no prior missing ledger";
			return;
		}
		if (shouldAcquire(mCurrentLedger->getLedgerSeq(), theConfig.LEDGER_HISTORY, prevMissing))
		{
			cLog(lsDEBUG) << "Ledger " << prevMissing << " is needed";
			assert(!mCompleteLedgers.hasValue(prevMissing));
			Ledger::pointer nextLedger = getLedgerBySeq(prevMissing + 1);
			if (nextLedger)
				acquireMissingLedger(ledger, nextLedger->getParentHash(), nextLedger->getLedgerSeq() - 1);
			else
			{
				mCompleteLedgers.clearValue(prevMissing);
				cLog(lsWARNING) << "We have a gap we can't fix: " << prevMissing + 1;
			}
		}
	}
}

void LedgerMaster::checkAccept(const uint256& hash)
{
	Ledger::pointer ledger = mLedgerHistory.getLedgerByHash(hash);
	if (ledger)
		checkAccept(hash, ledger->getLedgerSeq());
}

void LedgerMaster::checkAccept(const uint256& hash, uint32 seq)
{ // Can we advance the last fully accepted ledger? If so, can we publish?
	boost::recursive_mutex::scoped_lock ml(mLock);

	if (mValidLedger && (seq <= mValidLedger->getLedgerSeq()))
		return;

	int minVal = mMinValidations;

	if (mLastValidateHash.isNonZero())
	{
		int val = theApp->getValidations().getTrustedValidationCount(mLastValidateHash);
		val *= MIN_VALIDATION_RATIO;
		val /= 256;
		if (val > minVal)
			minVal = val;
	}

	if (theConfig.RUN_STANDALONE)
		minVal = 0;
	else if (theApp->getOPs().isNeedNetworkLedger())
		minVal = 1;

	if (theApp->getValidations().getTrustedValidationCount(hash) < minVal) // nothing we can do
		return;

	cLog(lsINFO) << "Advancing accepted ledger to " << seq << " with >= " << minVal << " validations";

	mLastValidateHash = hash;
	mLastValidateSeq = seq;

	Ledger::pointer ledger = mLedgerHistory.getLedgerByHash(hash);
	if (!ledger)
		return;
	mValidLedger = ledger;

	tryPublish();
}

void LedgerMaster::tryPublish()
{
	boost::recursive_mutex::scoped_lock ml(mLock);
	assert(mValidLedger);

	if (!mPubLedger)
	{
		mPubLedger = mValidLedger;
		mPubLedgers.push_back(mValidLedger);
	}
	else if (mValidLedger->getLedgerSeq() > (mPubLedger->getLedgerSeq() + MAX_LEDGER_GAP))
	{
		mPubLedger = mValidLedger;
		mPubLedgers.push_back(mValidLedger);
	}
	else if (mValidLedger->getLedgerSeq() > mPubLedger->getLedgerSeq())
	{
		for (uint32 seq = mPubLedger->getLedgerSeq() + 1; seq <= mValidLedger->getLedgerSeq(); ++seq)
		{
			cLog(lsDEBUG) << "Trying to publish ledger " << seq;

			Ledger::pointer ledger;
			uint256 hash;

			if (seq == mValidLedger->getLedgerSeq())
			{
				ledger = mValidLedger;
				hash = ledger->getHash();
			}
			else
			{
				hash = mValidLedger->getLedgerHash(seq);
				if (hash.isZero())
				{
					cLog(lsFATAL) << "Ledger: " << mValidLedger->getLedgerSeq() << " does not have hash for " <<
						seq;
					assert(false);
				}
				ledger = mLedgerHistory.getLedgerByHash(hash);
			}

			if (ledger)
			{
				mPubLedger = ledger;
				mPubLedgers.push_back(ledger);
			}
			else
			{
				if (theApp->getMasterLedgerAcquire().isFailure(hash))
				{
					cLog(lsFATAL) << "Unable to acquire a recent validated ledger";
				}
				else
				{
					LedgerAcquire::pointer acq = theApp->getMasterLedgerAcquire().findCreate(hash);
					if (!acq->isDone())
					{
						acq->setAccept();
						break;
					}
					else if (acq->isComplete() && !acq->isFailed())
					{
						mPubLedger = acq->getLedger();
						mPubLedgers.push_back(mPubLedger);
					}
					else
						cLog(lsWARNING) << "Failed to acquire a published ledger";
				}
			}
		}
	}

	if (!mPubLedgers.empty() && !mPubThread)
	{
		theApp->getOPs().clearNeedNetworkLedger();
		mPubThread = true;
		mTooFast = false;
		theApp->getJobQueue().addJob(jtPUBLEDGER, boost::bind(&LedgerMaster::pubThread, this));
	}
}

void LedgerMaster::pubThread()
{
	std::list<Ledger::pointer> ledgers;

	while (1)
	{
		ledgers.clear();

		{
			boost::recursive_mutex::scoped_lock ml(mLock);
			mPubLedgers.swap(ledgers);
			if (ledgers.empty())
			{
				mPubThread = false;
				return;
			}
		}

		BOOST_FOREACH(Ledger::ref l, ledgers)
		{
			cLog(lsDEBUG) << "Publishing ledger " << l->getLedgerSeq();
			setFullLedger(l); // OPTIMIZEME: This is actually more work than we need to do
			theApp->getOPs().pubLedger(l);
			BOOST_FOREACH(callback& c, mOnValidate)
			{
				c(l);
			}
		}
	}
}

// vim:ts=4
