#include "TransactionQueue.h"

bool TXQueue::addEntryForSigCheck(TXQEntry::ref entry)
{ // we always dispatch a thread to check the signature
	boost::mutex::scoped_lock sl(mLock);

	return mTxMap.insert(valueType(entry->getID(), entry)).second;
}

bool TXQueue::addEntryForExecution(TXQEntry::ref entry)
{
	boost::mutex::scoped_lock sl(mLock);

	entry->mSigChecked = true;
	if (!mTxMap.insert(valueType(entry->getID(), entry)).second)
		mTxMap.left.find(entry->getID())->second->mSigChecked = true;

	if (mRunning)
		return false;

	mRunning = true;
	return true; // A thread needs to handle this account
}

void TXQueue::removeEntry(const uint256& id)
{
	boost::mutex::scoped_lock sl(mLock);

	mTxMap.left.erase(id);
}

void TXQueue::getJob(TXQEntry::pointer &job)
{
	boost::mutex::scoped_lock sl(mLock);

	if (job)
		mTxMap.left.erase(job->getID());

	mapType::left_map::iterator it = mTxMap.left.begin();
	if (it == mTxMap.left.end() || !it->second->mSigChecked)
		job.reset();
	else job = it->second;
}

bool TXQueue::stopProcessing()
{ // returns true if a new thread must be dispatched
	boost::mutex::scoped_lock sl(mLock);

	mapType::left_map::iterator it = mTxMap.left.begin();
	return (it != mTxMap.left.end()) && it->second->mSigChecked;
}
