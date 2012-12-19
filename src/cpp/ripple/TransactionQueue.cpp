#include "TransactionQueue.h"

bool TXQueue::addEntryForSigCheck(TXQEntry::ref entry)
{ // we always dispatch a thread to check the signature
	boost::mutex::scoped_lock sl(mLock);
	mQueue[entry->getAccount()].push_back(entry);
	return true; // we always need to dispatch a thread to check the signature
}

bool TXQueue::addEntryForExecution(TXQEntry::ref entry, bool isNew)
{
	boost::mutex::scoped_lock sl(mLock);
	entry->mSigChecked = true;

	if (isNew)
		mQueue[entry->getAccount()].push_back(entry);

	if (mQueue.count(entry->getAccount()) != 0)
		return false; // A thread is already handling this account

	mThreads.insert(entry->getAccount());
	return true; // A thread needs to handle this account
}

void TXQueue::removeEntry(TXQEntry::ref entry)
{
	boost::mutex::scoped_lock sl(mLock);

	boost::unordered_map<RippleAddress, listType>::iterator mIt = mQueue.find(entry->getAccount());
	if (mIt == mQueue.end())
		return;

	listType& txList = mIt->second;
	for (listType::iterator listIt = txList.begin(), listEnd = txList.end(); listIt != listEnd; ++listIt)
		if (*listIt == entry)
		{
			txList.erase(listIt);
			if (txList.empty())
				mQueue.erase(mIt);
			return;
		}
}

TXQEntry::pointer TXQueue::getJob(const RippleAddress& account, TXQEntry::ref finished)
{
	boost::mutex::scoped_lock sl(mLock);

	assert(mQueue.count(account) != 0);

	boost::unordered_map<RippleAddress, listType>::iterator mIt = mQueue.find(account);
	if (mIt != mQueue.end())
	{
		listType& txList = mIt->second;
		if (txList.empty())
		{
			assert(!finished);
			mQueue.erase(mIt);
		}
		else
		{
			TXQEntry::pointer e = txList.front();
			if (finished)
			{
				assert(e == finished); // We should have done the head job in this list
				txList.pop_front();
				if (txList.empty()) // No more jobs for this account
				{
					e.reset();
					mQueue.erase(mIt);
				}
				else
					e = txList.front();
			}

			if (e && e->getSigChecked()) // The next job is ready to do
				return e;
		}
	}
	else
		assert(!finished); // If we finished a job, it should be there

	// No job to do now, release the thread
	mThreads.erase(account);
	return TXQEntry::pointer();	
}
