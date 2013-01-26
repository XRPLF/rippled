#include "TransactionQueue.h"

#include <boost/foreach.hpp>

void TXQEntry::addCallbacks(const TXQEntry& otherEntry)
{
	BOOST_FOREACH(const stCallback& callback, otherEntry.mCallbacks)
		mCallbacks.push_back(callback);
}

void TXQEntry::doCallbacks(TER result)
{
		BOOST_FOREACH(const stCallback& callback, mCallbacks)
			callback(mTxn, result);
}

bool TXQueue::addEntryForSigCheck(TXQEntry::ref entry)
{ // we always dispatch a thread to check the signature
	boost::mutex::scoped_lock sl(mLock);

	if (!mTxMap.insert(valueType(entry->getID(), entry)).second)
	{
		if (!entry->mCallbacks.empty())
			mTxMap.left.find(entry->getID())->second->addCallbacks(*entry);
		return false;
	}
	return true;
}

bool TXQueue::addEntryForExecution(TXQEntry::ref entry)
{
	boost::mutex::scoped_lock sl(mLock);

	entry->mSigChecked = true;

	std::pair<mapType::iterator, bool> it = mTxMap.insert(valueType(entry->getID(), entry));
	if (!it.second)
	{ // There was an existing entry
		it.first->right->mSigChecked = true;
		if (!entry->mCallbacks.empty())
			it.first->right->addCallbacks(*entry);
	}

	if (mRunning)
		return false;

	mRunning = true;
	return true; // A thread needs to handle this account
}

TXQEntry::pointer TXQueue::removeEntry(const uint256& id)
{
	TXQEntry::pointer ret;

	boost::mutex::scoped_lock sl(mLock);

	mapType::left_map::iterator it = mTxMap.left.find(id);
	if (it != mTxMap.left.end())
	{
		ret = it->second;
		mTxMap.left.erase(it);
	}

	return ret;
}

void TXQueue::getJob(TXQEntry::pointer &job)
{
	boost::mutex::scoped_lock sl(mLock);
	assert(mRunning);

	if (job)
		mTxMap.left.erase(job->getID());

	mapType::left_map::iterator it = mTxMap.left.begin();
	if (it == mTxMap.left.end() || !it->second->mSigChecked)
	{
		job.reset();
		mRunning = false;
	}
	else
		job = it->second;
}

bool TXQueue::stopProcessing(TXQEntry::ref finishedJob)
{ // returns true if a new thread must be dispatched
	boost::mutex::scoped_lock sl(mLock);
	assert(mRunning);

	mTxMap.left.erase(finishedJob->getID());

	mapType::left_map::iterator it = mTxMap.left.begin();
	if ((it != mTxMap.left.end()) && it->second->mSigChecked)
		return true;

	mRunning = false;
	return false;
}
