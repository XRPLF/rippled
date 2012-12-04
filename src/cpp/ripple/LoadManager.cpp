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
