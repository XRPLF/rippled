#include "LoadManager.h"

int LoadManager::getCreditRate() const
{
	boost::mutex::scoped_lock sl(mLock);
	return mCreditRate;
}

int LoadManager::getCreditLimit() const
{
	boost::mutex::scoped_lock sl(mLock);
	return mCreditLimit;
}

int LoadManager::getDebitWarn() const
{
	boost::mutex::scoped_lock sl(mLock);
	return mDebitWarn;
}

int LoadManager::getDebitLimit() const
{
	boost::mutex::scoped_lock sl(mLock);
	return mDebitLimit;
}

void LoadManager::setCreditRate(int r)
{
	boost::mutex::scoped_lock sl(mLock);
	mCreditRate = r;
}

void LoadManager::setCreditLimit(int r)
{
	boost::mutex::scoped_lock sl(mLock);
	mCreditLimit = r;
}

void LoadManager::setDebitWarn(int r)
{
	boost::mutex::scoped_lock sl(mLock);
	mDebitWarn = r;
}

void LoadManager::setDebitLimit(int r)
{
	boost::mutex::scoped_lock sl(mLock);
	mDebitLimit = r;
}

void LoadManager::canonicalize(LoadSource& source, const time_t now) const
{
	if (source.mLastUpdate != now)
	{
		if (source.mLastUpdate < now)
		{
			source.mBalance += mCreditRate * (now - source.mLastUpdate);
			if (source.mBalance > mCreditLimit)
				source.mBalance = mCreditLimit;
		}
		source.mLastUpdate = now;
	}
}

bool LoadManager::shouldWarn(LoadSource& source) const
{
	time_t now = time(NULL);
	boost::mutex::scoped_lock sl(mLock);

	canonicalize(source, now);
	if (source.isPrivileged() || (source.mBalance < mDebitWarn) || (source.mLastWarning == now))
		return false;
	source.mLastWarning = now;
	return true;
}

bool LoadManager::shouldCutoff(LoadSource& source) const
{
	time_t now = time(NULL);
	boost::mutex::scoped_lock sl(mLock);

	canonicalize(source, now);
	return !source.isPrivileged() && (source.mBalance < mDebitLimit);
}

bool LoadManager::adjust(LoadSource& source, int credits) const
{
	time_t now = time(NULL);
	boost::mutex::scoped_lock sl(mLock);

	// We do it this way in case we want to add exponential decay later
	canonicalize(source, now);
	source.mBalance += credits;
	if (source.mBalance > mCreditLimit)
		source.mBalance = mCreditLimit;

	if (source.isPrivileged()) // privileged sources never warn/cutoff
		return false;

	if (source.mBalance < mDebitLimit) // over-limit
		return true;

	if ((source.mBalance < mDebitWarn) && (source.mLastWarning != now)) // need to warn
		return true;

	return false;
}

uint64 LoadTrack::scaleFee(uint64 fee)
{
	static uint64 midrange(0x00000000FFFFFFFF);
	int factor = (mLocalTxnLoadFee > mRemoteTxnLoadFee) ? mLocalTxnLoadFee : mRemoteTxnLoadFee;

	if (fee > midrange)			// large fee, divide first
		return (fee >> 16) * factor;
	else						// small fee, multiply first
		return (fee * factor) >> 16;
}

void LoadTrack::raiseRemoteFee()
{
	if (mRemoteTxnLoadFee < mLocalTxnLoadFee) // make sure this fee takes effect
		mRemoteTxnLoadFee = mLocalTxnLoadFee;

	if (mRemoteTxnLoadFee < 268435456) // no more than a million
		mRemoteTxnLoadFee += (mRemoteTxnLoadFee >> 4); // increment by 1/16th
}

void LoadTrack::lowerRemoteFee()
{
	mLocalTxnLoadFee -= (mLocalTxnLoadFee >> 4); // reduce by 1/16th

	if (mLocalTxnLoadFee < 256)
		mLocalTxnLoadFee = 256;
}

void LoadTrack::raiseLocalFee()
{
	if (mLocalTxnLoadFee < mLocalTxnLoadFee) // make sure this fee takes effect
		mLocalTxnLoadFee = mLocalTxnLoadFee;

	if (mLocalTxnLoadFee < 268435456) // no more than a million
		mLocalTxnLoadFee += (mLocalTxnLoadFee >> 4); // increment by 1/16th
}

void LoadTrack::lowerLocalFee()
{
	mLocalTxnLoadFee -= (mLocalTxnLoadFee >> 4); // reduce by 1/16th

	if (mLocalTxnLoadFee < 256)
		mLocalTxnLoadFee = 256;
}

// vim:ts=4
